// E5:APC 的正用 —— 掐断「卡在可警告等待里」的线程:QueueUserAPC 让 WaitForSingleObjectEx
//     提前返回 WAIT_IO_COMPLETION(=0xC0=192=STATUS_USER_APC),线程随后自己收尾退出;
//     对照:不可警告的 WaitForSingleObject 掐不动,APC 排队干等,线程退出时直接作废。
//     IOCP 钩子:GetQueuedCompletionStatus 的完成投递底层就是这套 APC 机制(ch04 展开)。
#define WIN32_LEAN_AND_MEAN
#include <cstdio>
#include <windows.h>

static HANDLE g_never;                // 真句柄、永远无信号,直到收尾 SetEvent
static volatile LONG g_reason_w1 = 0; // w1 的 APC 是否跑过、带了什么
static volatile LONG g_reason_w2 = 0; // w2 的 APC 是否跑过

static ULONGLONG tick() {
    return GetTickCount64();
}

static void WINAPI wake_w1(ULONG_PTR arg) {
    g_reason_w1 = (LONG)arg;
    printf("    [w1 的 APC] t=%llu 在 w1 原线程里跑,reason=%lu —— 它把等待掐断\n",
           (unsigned long long)tick(), (unsigned long)arg);
}

static void WINAPI wake_w2(ULONG_PTR arg) {
    g_reason_w2 = (LONG)arg; // 预期:永远不会执行
    printf("    [w2 的 APC] 不该出现的一行\n");
}

static DWORD WINAPI w1_body(void*) {
    printf("[w1] tid=%lu 卡进 WaitForSingleObjectEx(never, INFINITE, /*alertable=*/TRUE)\n",
           GetCurrentThreadId());
    ULONGLONG t0 = tick();
    DWORD r = WaitForSingleObjectEx(g_never, INFINITE, TRUE);
    printf(
        "[w1] t=%llu 提前返回 ret=%lu(WAIT_IO_COMPLETION=%ld),原计划等到世界末日,只撑了 %llums\n",
        (unsigned long long)tick(), r, WAIT_IO_COMPLETION, (unsigned long long)(tick() - t0));
    printf("[w1] 收尾返回 0 —— 自己退的,没人 TerminateThread\n");
    return 0;
}

static DWORD WINAPI w2_body(void*) {
    printf("[w2] tid=%lu 卡进 WaitForSingleObject(never, INFINITE) —— 不可警告\n",
           GetCurrentThreadId());
    DWORD r = WaitForSingleObject(g_never, INFINITE);
    printf("[w2] t=%llu 醒了 ret=%lu:唤醒我的是 SetEvent,不是 APC(g_reason_w2 仍是 %ld)\n",
           (unsigned long long)tick(), r, g_reason_w2);
    return 0; // 线程退出 → 积压 APC 作废
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("[E5] main tid=%lu\n", GetCurrentThreadId());
    g_never = CreateEventA(NULL, FALSE, FALSE, NULL);

    printf("[1] w1 卡在可警告等待;main 睡 400ms 后 QueueUserAPC(wake_w1, reason=42)\n");
    HANDLE h1 = CreateThread(NULL, 0, w1_body, NULL, 0, NULL);
    Sleep(400);
    QueueUserAPC(wake_w1, h1, 42);
    DWORD w = WaitForSingleObject(h1, 3000);
    DWORD ec = 0;
    GetExitCodeThread(h1, &ec);
    printf("    main 观察:w1 join=%lu(0=回来了),线程退出码=%lu,reason_w1=%ld(42=APC 真跑了)\n", w,
           ec, g_reason_w1);
    CloseHandle(h1);

    printf("[2] 对照 w2 卡在【不可警告】等待;main 排 APC(reason=7)再观察 800ms\n");
    HANDLE h2 = CreateThread(NULL, 0, w2_body, NULL, 0, NULL);
    Sleep(400);
    QueueUserAPC(wake_w2, h2, 7);
    Sleep(800);
    w = WaitForSingleObject(h2, 0);
    printf("    800ms 后:w2 状态=%lu(258=STILL_WAITING 还卡着),reason_w2=%ld(0=APC 没跑)\n", w,
           g_reason_w2);
    SetEvent(g_never); // 只放得出 w2(w1 已退):证明解铃还得真句柄
    Sleep(500);
    w = WaitForSingleObject(h2, 2000);
    GetExitCodeThread(h2, &ec);
    printf("    SetEvent 后 w2 join=%lu 退出码=%lu,reason_w2=%ld(仍 0:线程退出时积压 APC 作废)\n",
           w, ec, g_reason_w2);
    CloseHandle(h2);
    CloseHandle(g_never);

    printf("[E5] done —— APC 只掐得动可警告等待;线程没进过可警告点就退出,排进去的 APC 白排\n");
    return 0;
}
