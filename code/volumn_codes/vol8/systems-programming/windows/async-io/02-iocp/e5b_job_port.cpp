// e5b_job_port.cpp —— IOCP E5b Job 对象挂端口:进程的死讯以完成包送达(兑现 process/01 的前向指针)
//
// process/01 末尾点过一件事:「把它关联到 IOCP,进程的创建与退出会以完成包的形式异步
// 送达」,细节留给了本章。本实验兑现:Job 挂端口 → 建子进程(cmd /c exit 7)→ GQCS
// 收 Job 消息。文档对字段落点的表述容易读岔(消息号在 lpOverlAPPED 里,进程号走哪个
// 字段各说各的),本实验把 bytes/key/ov 三个数全打印出来,拿已知 pid 对号入座。
// 环境:Win11 26200 / MSYS2 UCRT64 g++ 16.1.0,-std=c++20 -Wall -Wextra
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <thread>

namespace {

long g_t0;
long ms_now() {
    return (long)(GetTickCount64() - (ULONGLONG)g_t0);
}
#define LOG(...)                             \
    do {                                     \
        std::printf("[%6ld ms] ", ms_now()); \
        std::printf(__VA_ARGS__);            \
        std::printf("\n");                   \
        std::fflush(stdout);                 \
    } while (0)

const char* job_msg_name(ULONGLONG v) {
    switch ((DWORD)(UINT_PTR)v) {
        case JOB_OBJECT_MSG_END_OF_JOB_TIME:
            return "END_OF_JOB_TIME";
        case JOB_OBJECT_MSG_END_OF_PROCESS_TIME:
            return "END_OF_PROCESS_TIME";
        case JOB_OBJECT_MSG_ACTIVE_PROCESS_LIMIT:
            return "ACTIVE_PROCESS_LIMIT";
        case JOB_OBJECT_MSG_ACTIVE_PROCESS_ZERO:
            return "ACTIVE_PROCESS_ZERO(组里没有活进程了)";
        case JOB_OBJECT_MSG_NEW_PROCESS:
            return "NEW_PROCESS(新进程进组)";
        case JOB_OBJECT_MSG_EXIT_PROCESS:
            return "EXIT_PROCESS(组内进程退场)";
        case JOB_OBJECT_MSG_ABNORMAL_EXIT_PROCESS:
            return "ABNORMAL_EXIT_PROCESS";
        default:
            return "(其他消息)";
    }
}

} // namespace

int wmain() {
    g_t0 = (long)GetTickCount64();
    HANDLE port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    HANDLE job = CreateJobObjectW(nullptr, nullptr);

    JOBOBJECT_ASSOCIATE_COMPLETION_PORT acp{};
    acp.CompletionKey = (PVOID)(UINT_PTR)0x99;
    acp.CompletionPort = port;
    BOOL ok =
        SetInformationJobObject(job, JobObjectAssociateCompletionPortInformation, &acp, sizeof acp);
    LOG("Job 挂端口: SetInformationJobObject ret=%d(key=0x99)", (int)ok);

    STARTUPINFOW si{};
    PROCESS_INFORMATION pi{};
    wchar_t cmd[] = L"C:\\Windows\\System32\\cmd.exe /c exit 7";
    BOOL cp = CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr,
                             nullptr, &si, &pi);
    if (!cp) {
        LOG("CreateProcessW 失败 gle=%lu", GetLastError());
        return 1;
    }
    AssignProcessToJobObject(job, pi.hProcess);
    LOG("子进程已建(pause 状态,pid=%lu),已入 Job,现在放行", pi.dwProcessId);
    ResumeThread(pi.hThread);

    // 收消息:退场码 7 的短命子进程,消息在毫秒级连着来
    for (int i = 0; i < 12; ++i) {
        DWORD bytes = 0;
        ULONG_PTR key = 0;
        LPOVERLAPPED pov = nullptr;
        BOOL g = GetQueuedCompletionStatus(port, &bytes, &key, &pov, 3000);
        if (!g && pov == nullptr) {
            LOG("GQCS 空手而归(超时)gle=%lu,收队", GetLastError());
            break;
        }
        LOG("GQCS: ret=%d bytes=%lu key=0x%lx ov=0x%llx → %s", (int)g, bytes, (unsigned long)key,
            (unsigned long long)(UINT_PTR)pov, job_msg_name((ULONGLONG)bytes));
        if ((DWORD)(UINT_PTR)pov != 0)
            LOG("  ↑ lpOverlapped 里装的是 pid=%lu(十进制)", (unsigned long)(UINT_PTR)pov);
        if (bytes == JOB_OBJECT_MSG_ACTIVE_PROCESS_ZERO) {
            LOG("ACTIVE_PROCESS_ZERO 到了,收队");
            break;
        }
    }

    DWORD ec = STILL_ACTIVE;
    WaitForSingleObject(pi.hProcess, 3000);
    GetExitCodeProcess(pi.hProcess, &ec);
    LOG("子进程退场码 = %lu(建的时候让它 exit 7);已知 pid=%lu —— 拿这两个数对上面的字段落点", ec,
        pi.dwProcessId);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(job);
    CloseHandle(port);
    LOG("iocp-e5b 完");
    return 0;
}
