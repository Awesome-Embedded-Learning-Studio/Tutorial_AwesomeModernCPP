// E4:APC 队列 —— QueueUserAPC 只把函数【排进目标线程的队列】,目标线程不进可警告等待就不执行;
//     可警告 = SleepEx/WaitForSingleObjectEx 带 bAlertable=TRUE。两种等待的二态时序 + 队列 FIFO。
#define WIN32_LEAN_AND_MEAN
#include <cstdio>
#include <windows.h>

static HANDLE g_start;
static volatile LONG g_done = 0;
static DWORD g_worker_tid = 0;

static ULONGLONG tick() {
    return GetTickCount64();
}

static void WINAPI apc_first(ULONG_PTR arg) {
    printf("    [APC#1] t=%llu tid=%lu arg=%lu(与 worker tid 同 = 复用原线程)\n",
           (unsigned long long)tick(), GetCurrentThreadId(), (unsigned long)arg);
    InterlockedIncrement(&g_done);
}

static void WINAPI apc_second(ULONG_PTR arg) {
    printf("    [APC#2] t=%llu tid=%lu arg=%lu\n", (unsigned long long)tick(), GetCurrentThreadId(),
           (unsigned long)arg);
    InterlockedIncrement(&g_done);
}

static DWORD WINAPI worker(void*) {
    g_worker_tid = GetCurrentThreadId();
    printf("[worker] tid=%lu 就绪,先卡在 WaitForSingleObject(start)(不可警告)\n", g_worker_tid);
    WaitForSingleObject(g_start, INFINITE);
    printf("[worker] t=%llu 进入 SleepEx(600, FALSE) —— 不可警告睡眠 600ms\n",
           (unsigned long long)tick());
    ULONGLONG t0 = tick();
    DWORD r1 = SleepEx(600, FALSE);
    printf("[worker] t=%llu 醒来(ret=%lu),此刻 APC 执行数=%ld(排队≠执行,1 的证据)\n",
           (unsigned long long)tick(), r1, g_done);
    printf("[worker] t=%llu 进入 SleepEx(5000, TRUE) —— 可警告睡眠:队列里的 APC 立刻开闸\n",
           (unsigned long long)tick());
    DWORD r2 = SleepEx(5000, TRUE);
    printf(
        "[worker] t=%llu 返回 ret=%lu(WAIT_IO_COMPLETION=%ld),实际只睡了 %llums,APC 执行数=%ld\n",
        (unsigned long long)tick(), r2, WAIT_IO_COMPLETION, (unsigned long long)(tick() - t0),
        g_done);
    return 0;
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("[E4] main tid=%lu\n", GetCurrentThreadId());
    g_start = CreateEventA(NULL, FALSE, FALSE, NULL);
    HANDLE h = CreateThread(NULL, 0, worker, NULL, 0, NULL);
    Sleep(150); // worker 已就绪并卡在 start 上(不可警告)

    BOOL q1 = QueueUserAPC(apc_first, h, 111);
    BOOL q2 = QueueUserAPC(apc_second, h, 222);
    printf("[main] t=%llu 两条 APC 排完(q1=%d q2=%d):worker 正卡在不可警告等待,一条都没跑\n",
           (unsigned long long)tick(), q1, q2);
    SetEvent(g_start); // 放 worker 去先睡不可警告的 600ms
    WaitForSingleObject(h, INFINITE);
    CloseHandle(h);
    CloseHandle(g_start);
    printf("[E4] done —— 读输出顺序:APC#1 恒在 APC#2 前(队列 FIFO);\n"
           "      两次 APC 的 tid == worker tid(对照控制台事件的【新】线程,APC 复用原线程)\n");
    return 0;
}
