// E3 驱动:四种死法对照 —— return / ExitProcess / TerminateProcess / CTRL_BREAK
//   观察点:atexit 兑现?stdout 缓冲落盘?DLL 收到 detach?退出码多少?
// 布置:子进程 stdout 重定向到文件(全缓冲,标记只在真 flush 时落盘);
//       stderr 走管道,驱动在子进程退出后整段取回(转录不会和驱动输出互相穿插)。
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <windows.h>

static std::wstring dir_of_self() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p = buf;
    return p.substr(0, p.find_last_of(L'\\'));
}

static void run_case(const wchar_t* mode, int idx) {
    printf("\n----- case %ls -----\n", mode);
    wchar_t tb[MAX_PATH];
    GetTempPathW(MAX_PATH, tb);
    std::wstring base = std::wstring(tb) + L"vol8_e3";
    CreateDirectoryW(base.c_str(), nullptr);
    std::wstring outfile = base + L"\\out_" + std::to_wstring(idx) + L".txt";
    DeleteFileW(outfile.c_str());

    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE out = CreateFileW(outfile.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             &sa, CREATE_ALWAYS, 0, nullptr);
    HANDLE pipe_r = nullptr, pipe_w = nullptr;
    CreatePipe(&pipe_r, &pipe_w, &sa, 0);
    SetHandleInformation(pipe_r, HANDLE_FLAG_INHERIT, 0); // 读端不继承

    std::wstring ev_name =
        L"Local\\vol8e3-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(idx);
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, ev_name.c_str());

    std::wstring app = dir_of_self() + L"\\e3_child.exe";
    std::wstring cmdline =
        L"\"" + app + L"\" " + mode + L" \"" + dir_of_self() + L"\\e3_dll.dll\" " + ev_name;
    std::vector<wchar_t> cmdbuf(cmdline.begin(), cmdline.end());
    cmdbuf.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = out;   // 子 stdout -> 文件(全缓冲)
    si.hStdError = pipe_w; // 子 stderr -> 管道,死后统一取回
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(app.c_str(), cmdbuf.data(), nullptr, nullptr, TRUE,
                        CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &si, &pi)) {
        printf("CreateProcessW failed err=%lu\n", GetLastError());
        CloseHandle(out);
        CloseHandle(ready);
        CloseHandle(pipe_r);
        CloseHandle(pipe_w);
        return;
    }
    CloseHandle(pipe_w); // 驱动这份写端放手,子进程退出后管道自然 EOF

    DWORD w = WaitForSingleObject(ready, 5000);
    printf("[driver] child pid=%lu ready handshake: 0x%lX\n", pi.dwProcessId, (unsigned long)w);

    if (wcscmp(mode, L"normal") == 0) {
        // 自然 return,驱动不动手
    } else if (wcscmp(mode, L"exitprocess") == 0) {
        // 子进程自己直调 ExitProcess
    } else if (wcscmp(mode, L"hang") == 0) {
        printf("[driver] TerminateProcess(h, 31337) -- no chance to clean up\n");
        TerminateProcess(pi.hProcess, 31337);
    } else if (wcscmp(mode, L"ctrlbreak") == 0) {
        printf(
            "[driver] GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pid) -- the catchable 'signal'\n");
        if (!GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pi.dwProcessId))
            printf("[driver] GenerateConsoleCtrlEvent failed err=%lu\n", GetLastError());
    }

    w = WaitForSingleObject(pi.hProcess, 10000);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    if (w == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 0xDEAD0001);
        printf("[driver] TIMEOUT, driver had to shoot it\n");
    }
    printf("[driver] exit code=%lu (0x%08lX)\n", code, code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(ready);
    CloseHandle(out);

    // 取回子进程 stderr 全录(管道顺序 = 写入顺序)
    std::string transcript;
    char buf[512];
    DWORD n = 0;
    while (ReadFile(pipe_r, buf, sizeof(buf) - 1, &n, nullptr) && n > 0) {
        buf[n] = '\0';
        transcript += buf;
    }
    CloseHandle(pipe_r);
    printf("[driver] child stderr transcript (verbatim, in order):\n");
    fputs(transcript.c_str(), stdout); // 本就按行结尾,直接倾倒
    printf("[driver] end transcript\n");

    // 读回子进程 stdout 文件:标记在不在 = 缓冲有没有被刷
    HANDLE rf = CreateFileW(outfile.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr, OPEN_EXISTING, 0, nullptr);
    char fbuf[256]{};
    n = 0;
    if (rf != INVALID_HANDLE_VALUE) {
        ReadFile(rf, fbuf, sizeof(fbuf) - 1, &n, nullptr);
        CloseHandle(rf);
    }
    printf("[driver] child stdout file (%lu bytes): \"%s\"\n", n, fbuf);
    printf("[driver] stdout marker survived? %s\n",
           n > 0 ? "YES -> buffer was flushed" : "NO  -> buffer died with the process");
}

int wmain() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    printf("E3 TerminateProcess vs graceful exit | driver pid=%lu\n", GetCurrentProcessId());
    printf("evidence per case: stderr transcript (dll/atexit lines) + stdout marker + exit code\n");
    run_case(L"normal", 1);
    run_case(L"exitprocess", 2);
    run_case(L"hang", 3);
    run_case(L"ctrlbreak", 4);
    printf("\nE3 done.\n");
    return 0;
}
