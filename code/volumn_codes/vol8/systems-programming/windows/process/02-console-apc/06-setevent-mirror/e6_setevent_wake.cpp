// E6:handler 里最省事的安全动作 = 只 SetEvent(Linux self-pipe trick 的 Windows 镜像)。
//     MSDN 口径:CLOSE/LOGOFF/SHUTDOWN 三个事件期间"Console functions, or any C run-time
//     functions that call console functions, may not work reliably" —— 所以 handler 里
//     只做 SetEvent 一句,打印交给主线程。主线程在 WaitForSingleObject 上等它。
#define WIN32_LEAN_AND_MEAN
#include <cstdio>
#include <windows.h>

static HANDLE g_wake;
static volatile LONG g_n = 0;

static BOOL WINAPI handler(DWORD) {
    SetEvent(g_wake); // 全部工作就这一句;不 printf、不碰控制台
    InterlockedIncrement(&g_n);
    return TRUE;
}

static ULONGLONG tick() {
    return GetTickCount64();
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("[E6] main tid=%lu\n", GetCurrentThreadId());
    SetConsoleCtrlHandler(handler, TRUE);
    SetConsoleCtrlHandler(NULL, FALSE); // 复位 WSL interop 继承的忽略位
    g_wake = CreateEventA(NULL, FALSE, FALSE, NULL);

    for (int round = 1; round <= 2; round++) {
        DWORD ev = round == 1 ? CTRL_C_EVENT : CTRL_BREAK_EVENT;
        printf("[%d] main 进 WaitForSingleObject(wake, 5000),然后自己给自己发 %s\n", round,
               round == 1 ? "CTRL_C_EVENT" : "CTRL_BREAK_EVENT");
        ULONGLONG t0 = tick();
        Sleep(80);
        GenerateConsoleCtrlEvent(ev, 0);
        DWORD r = WaitForSingleObject(g_wake, 5000);
        printf("    %llums 后醒:ret=%lu(0=句柄有信号)handler 共被调 %ld 次 —— 它只干了 SetEvent\n",
               (unsigned long long)(tick() - t0), r, g_n);
        g_n = 0;
    }
    CloseHandle(g_wake);
    printf("[E6] done —— 事件线程只 SetEvent,决策与 IO 全回主线程:与 self-pipe 同构\n");
    return 0;
}
