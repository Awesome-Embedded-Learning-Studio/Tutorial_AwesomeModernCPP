// E2: 退出码与等待
//  [1] 三种退出形态的退出码:正常 return / abort() / 被 TerminateProcess
//  [2] 关闭 hProcess 不等于杀进程(句柄只是观察权),重开仍能等到它
//  [3] WaitForMultipleObjects 多子收尸:任意一个先完成 / 全体收完
// 用法: 无参 = 驱动;"exitcode"|"abort"|"sleepexit <ms> <code>" 为子模式。
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <windows.h>

static std::wstring exe_path() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return buf;
}
static bool spawn(std::wstring cmdline, DWORD flags, PROCESS_INFORMATION* pi) {
    std::vector<wchar_t> buf(cmdline.begin(), cmdline.end());
    buf.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    return CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, flags, nullptr, nullptr,
                          &si, pi) != 0;
}
static DWORD wait_exit(PROCESS_INFORMATION& pi, DWORD timeout_ms, const char** how) {
    DWORD w = WaitForSingleObject(pi.hProcess, timeout_ms);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    if (how)
        *how = (w == WAIT_TIMEOUT) ? "TIMEOUT" : "signaled";
    return code;
}
static void print_code(const char* tag, DWORD code, const char* how) {
    printf("    %-28s exit code=%-11lu (0x%08lX)  wait=%s\n", tag, code, code, how);
}

static int mode_exitcode() {
    Sleep(300);
    return 42;
}
static int mode_abort() {
    // 默认 abort 在 Windows 触发 fail-fast(WER);压掉错误弹窗,行为仍按默认 abort 走
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Sleep(300);
    abort();
}
static int mode_sleepexit(int ms, int code) {
    Sleep(ms);
    return code;
}

static void section1() {
    printf("== [1] 三种退出形态:GetExitCodeProcess 读到什么 ==\n");
    PROCESS_INFORMATION pi{};
    const char* how = "";

    printf("(a) child returns 42 from main\n");
    if (spawn(exe_path() + L" exitcode", 0, &pi)) {
        DWORD c = wait_exit(pi, 15000, &how);
        print_code("normal return 42:", c, how);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    printf("(b) child calls abort()  [Linux: WIFSIGNALED + WTERMSIG=6, 根本不是退出码]\n");
    if (spawn(exe_path() + L" abort", 0, &pi)) {
        print_code("abort():", wait_exit(pi, 15000, &how), how);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    printf("(c) parent TerminateProcess(h, 4660)\n");
    if (spawn(exe_path() + L" sleepexit 8000 0", 0, &pi)) {
        Sleep(200);
        TerminateProcess(pi.hProcess, 4660);
        print_code("terminated 4660:", wait_exit(pi, 5000, &how), how);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    printf("(d) STILL_ACTIVE 陷阱:进程还没退,GetExitCodeProcess 给 259\n");
    if (spawn(exe_path() + L" sleepexit 900 77", 0, &pi)) {
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        printf("    queried while running     exit code=%lu (259 = STILL_ACTIVE)\n", code);
        print_code("after real exit:", wait_exit(pi, 15000, &how), how);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

static void section2() {
    printf("\n== [2] 句柄只是观察权:CloseHandle(hProcess) 后进程还活着 ==\n");
    PROCESS_INFORMATION pi{};
    if (!spawn(exe_path() + L" sleepexit 2500 5", 0, &pi)) {
        printf("spawn failed\n");
        return;
    }
    DWORD pid = pi.dwProcessId;
    CloseHandle(pi.hProcess); // 立刻扔掉唯一观察权
    CloseHandle(pi.hThread);
    printf("handles closed for pid=%lu; sleeping 300ms...\n", pid);
    Sleep(300);
    HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) {
        printf("OpenProcess failed err=%lu\n", GetLastError());
        return;
    }
    printf("OpenProcess(pid) succeeded -> process is STILL ALIVE 300ms after handle closed\n");
    DWORD w = WaitForSingleObject(h, 10000);
    DWORD code = 0;
    GetExitCodeProcess(h, &code);
    printf("waited on reopened handle -> 0x%lX, exit code=%lu (child chose 5)\n", (unsigned long)w,
           code);
    printf(
        "=> closing a handle drops OUR view, not the process; kernel object dies on LAST handle\n");
    CloseHandle(h);
}

static void section3() {
    printf("\n== [3] WaitForMultipleObjects:三个孩子赛跑 ==\n");
    PROCESS_INFORMATION pis[3]{};
    int codes[3] = {11, 22, 33};
    int dur[3] = {1800, 500, 1200}; // 预期完成顺序: 1(500ms) -> 2(1200ms) -> 0(1800ms)
    for (int i = 0; i < 3; i++)
        spawn(exe_path() + L" sleepexit " + std::to_wstring(dur[i]) + L" " +
                  std::to_wstring(codes[i]),
              0, &pis[i]);
    HANDLE hs[3] = {pis[0].hProcess, pis[1].hProcess, pis[2].hProcess};

    // (a) 逐个 WaitForSingleObject(h,0) 轮询:得到真实完成时刻顺序
    ULONGLONG t0 = GetTickCount64();
    bool done[3] = {false, false, false};
    printf("(a) chronological finish order (poll each with timeout 0):\n");
    for (int fin = 0; fin < 3;) {
        for (int i = 0; i < 3; i++) {
            if (!done[i] && WaitForSingleObject(hs[i], 0) == WAIT_OBJECT_0) {
                done[i] = true;
                fin++;
                DWORD c = 0;
                GetExitCodeProcess(hs[i], &c);
                printf("    t+%llums child[%d] done, exit=%lu\n",
                       (unsigned long long)(GetTickCount64() - t0), i, c);
            }
        }
        Sleep(50);
    }

    // (b) 一把 WaitForMultipleObjects 等任意一个(低索引优先的语义)
    for (int i = 0; i < 3; i++)
        spawn(exe_path() + L" sleepexit " + std::to_wstring(dur[i]) + L" " +
                  std::to_wstring(codes[i]),
              0, &pis[i]);
    // pis 已重灌;hs 重建
    HANDLE hs2[3] = {pis[0].hProcess, pis[1].hProcess, pis[2].hProcess};
    printf("(b) single WFMO(bWaitAll=FALSE) blocks until ANY signaled:\n");
    t0 = GetTickCount64();
    DWORD w = WaitForMultipleObjects(3, hs2, FALSE, INFINITE);
    printf("    returned 0x%lX (=WAIT_OBJECT_0+%lu, child index) at t+%llums\n", (unsigned long)w,
           (unsigned long)(w - WAIT_OBJECT_0), (unsigned long long)(GetTickCount64() - t0));

    // (c) 全收:bWaitAll=TRUE,一次等待收完三个
    printf("(c) WFMO(bWaitAll=TRUE) drains all, then collect by handle order:\n");
    t0 = GetTickCount64();
    w = WaitForMultipleObjects(3, hs2, TRUE, 15000);
    printf("    all done at t+%llums (ret 0x%lX); exit codes in HANDLE order:\n",
           (unsigned long long)(GetTickCount64() - t0), (unsigned long)w);
    for (int i = 0; i < 3; i++) {
        DWORD c = 0;
        GetExitCodeProcess(hs2[i], &c);
        printf("    child[%d] exit=%lu\n", i, c);
        CloseHandle(pis[i].hProcess);
        CloseHandle(pis[i].hThread);
    }
    printf("    => waitpid(-1) 返回\"某个\"结束者还要再问是谁;WFMO 的返回值直接给出索引\n");
}

int wmain(int argc, wchar_t** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc >= 2) {
        if (wcscmp(argv[1], L"exitcode") == 0)
            return mode_exitcode();
        if (wcscmp(argv[1], L"abort") == 0)
            return mode_abort();
        if (wcscmp(argv[1], L"sleepexit") == 0 && argc == 4)
            return mode_sleepexit(_wtoi(argv[2]), _wtoi(argv[3]));
        return 1;
    }
    // 家长先压错误弹窗(子进程继承 error mode,abort 的 WER 不至于挂住无人值守跑批)
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    printf("E2 exit codes & waiting | parent pid=%lu\n", GetCurrentProcessId());
    section1();
    section2();
    section3();
    printf("\nE2 done.\n");
    return 0;
}
