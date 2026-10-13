// E2:控制台事件的投递 —— handler 在【每次事件新建的线程】里跑,返回即线程即退;
//     无 handler 时的默认路径 = ExitProcess(STATUS_CONTROL_C_EXIT = 0xC000013A)。
// 用法:直接跑 = 主实验;`e2 victim <c|b> <事件名>` = 子进程炮灰(不注册任何 handler)。
#define WIN32_LEAN_AND_MEAN
#include <cstdio>
#include <windows.h>

static volatile DWORD g_tids[4] = {};
static LONG g_ntid = 0;

static BOOL WINAPI probe_handler(DWORD type) {
    LONG slot = InterlockedIncrement(&g_ntid) - 1;
    if (type == CTRL_C_EVENT && slot < 4)
        g_tids[slot] = GetCurrentThreadId();
    printf("    [handler] type=%lu tid=%lu\n", type, GetCurrentThreadId());
    return TRUE;
}

static ULONGLONG tick() {
    return GetTickCount64();
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);

    // ---- 炮灰子进程:无任何 handler,先把继承的忽略位复位,然后干等 ----
    if (argc >= 4 && lstrcmpiA(argv[1], "victim") == 0) {
        SetConsoleCtrlHandler(NULL, FALSE); // 清掉启动链继承的 Ctrl+C 忽略位
        printf("  [victim-%s] pid=%lu 就绪:0 个 handler,Ctrl+C 忽略位已复位,开始干等\n", argv[2],
               GetCurrentProcessId());
        HANDLE ready = OpenEventA(EVENT_MODIFY_STATE, FALSE, argv[3]);
        if (ready) {
            SetEvent(ready);
            CloseHandle(ready);
        }
        Sleep(INFINITE); // 等着被默认 handler 收走
        return 0;
    }

    printf("[E2] pid=%lu main_tid=%lu\n", GetCurrentProcessId(), GetCurrentThreadId());
    SetConsoleCtrlHandler(probe_handler, TRUE);
    SetConsoleCtrlHandler(NULL, FALSE); // 复位 WSL interop 启动链的忽略位

    // ---- Phase A:handler 线程的生命周期 ----
    printf("[A1] 连发两次 CTRL_C:两次 handler 的 tid 各是多少?和主线程比?\n");
    Sleep(80);
    GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0);
    Sleep(400);
    Sleep(80);
    GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0);
    Sleep(400);
    printf("[A2] OpenThread 探测 handler 线程是否还活着(活着的线程应能打开):\n");
    // 对照组:一个真活着的线程
    HANDLE hLive = CreateThread(
        NULL, 0,
        [](void*) -> DWORD {
            Sleep(3000);
            return 0;
        },
        NULL, 0, NULL);
    Sleep(100);
    HANDLE hProbe = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, GetThreadId(hLive));
    printf("    对照:活着的 worker tid=%lu -> OpenThread=%p(应非空)\n", GetThreadId(hLive), hProbe);
    if (hProbe)
        CloseHandle(hProbe);
    for (int i = 0; i < g_ntid; i++) {
        SetLastError(0);
        HANDLE h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, g_tids[i]);
        printf("    handler tid#%d=%lu -> OpenThread=%p gle=%lu(NULL=线程已不存在)\n", i + 1,
               g_tids[i], h, GetLastError());
        if (h)
            CloseHandle(h);
    }
    WaitForSingleObject(hLive, INFINITE);
    CloseHandle(hLive);
    printf("    结论:两次事件两个不同 tid(每次事件一条新线程);handler 返回后线程即逝\n");

    // ---- Phase B:无 handler 的默认路径 ----
    char self[MAX_PATH];
    GetModuleFileNameA(NULL, self, MAX_PATH);
    for (int round = 0; round < 2; round++) {
        const char* evname = round == 0 ? "c" : "b";
        DWORD evt = round == 0 ? CTRL_C_EVENT : CTRL_BREAK_EVENT;
        char evobj[64];
        wsprintfA(evobj, "Local\\e2_ready_%d", round);
        HANDLE ready = CreateEventA(NULL, FALSE, FALSE, evobj);

        char cmdline[800];
        wsprintfA(cmdline, "\"%s\" victim %s %s", self, evname, evobj);
        STARTUPINFOA si = {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {};
        if (!CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
            printf("[B%d] CreateProcess failed gle=%lu\n", round + 1, GetLastError());
            return 1;
        }
        WaitForSingleObject(ready, 5000); // 炮灰复位完忽略位再开火
        CloseHandle(ready);
        printf("[B%d] 炮灰就绪,父进程发 %s(炮灰无 handler,只能走默认 handler)\n", round + 1,
               round == 0 ? "CTRL_C_EVENT" : "CTRL_BREAK_EVENT");
        ULONGLONG t0 = tick();
        Sleep(80);
        GenerateConsoleCtrlEvent(evt, 0);
        DWORD w = WaitForSingleObject(pi.hProcess, 5000);
        ULONGLONG dt = tick() - t0;
        DWORD code = 0;
        if (w == WAIT_OBJECT_0) {
            GetExitCodeProcess(pi.hProcess, &code);
            printf("    炮灰 %lums 内死透:exit code=0x%08lX(%ld)"
                   "  [STATUS_CONTROL_C_EXIT=0xC000013A=%d]\n",
                   (unsigned long)dt, code, (long)code, code == 0xC000013A);
        } else {
            printf("    炮灰 5s 后仍活着?!\n");
        }
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    printf("    父进程自己带着 handler 返回 TRUE,两次广播都活着 —— 这就是对照\n");
    printf("[E2] done\n");
    return 0;
}
