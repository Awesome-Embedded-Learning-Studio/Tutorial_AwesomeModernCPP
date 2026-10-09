// e4_pqcs_shutdown.cpp —— IOCP E4 PostQueuedCompletionStatus:端口自己当唤醒通道
//
// Linux 侧 Reactor(networking/03)用一枚 eventfd 把睡在 epoll_wait 里的循环叫醒;
// process/02 的控制台 handler 里咱们只做 SetEvent。Windows 这边的同构物就是
// PostQueuedCompletionStatus:端口不挂任何句柄,本身就是一条队列,主线程往里塞
// 自定义消息,睡在 GQCS 里的工线程被精确叫醒 —— 优雅关闭的信号通道就是这么搭的。
//   [1] 三条工线程睡死在 GQCS(INFINITE),主线程 PQCS 投一枚关停哨兵 ×3,逐一叫醒退场
//   [2] PQCS 的三件套(bytes/key/ov)原样透传:塞什么收什么
//   [3] 关停后的 PQCS 不炸也不丢:端口队列照常积压(不读就在)
// 环境:Win11 26200 / MSYS2 UCRT64 g++ 16.1.0,-std=c++20 -Wall -Wextra
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

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

constexpr ULONG_PTR kShutdownKey = (ULONG_PTR)-2;
constexpr UINT_PTR kSentinelOv = 0xDEADBEEFull;

std::atomic<int> g_workers_up{0};

void worker(HANDLE port, int id) {
    g_workers_up.fetch_add(1);
    for (;;) {
        DWORD bytes = 0;
        ULONG_PTR key = 0;
        LPOVERLAPPED pov = nullptr;
        BOOL g = GetQueuedCompletionStatus(port, &bytes, &key, &pov, INFINITE);
        std::printf("[%6ld ms]   工%d 醒来: GQCS ret=%d bytes=%lu key=%s ov=0x%llx (tid=%lu)\n",
                    ms_now(), id, (int)g, bytes, key == kShutdownKey ? "关停哨兵" : "普通",
                    (unsigned long long)(UINT_PTR)pov, GetCurrentThreadId());
        std::fflush(stdout);
        if (key == kShutdownKey) {
            std::printf("[%6ld ms]   工%d 认出关停,退场\n", ms_now(), id);
            std::fflush(stdout);
            return;
        }
    }
}

} // namespace

int main() {
    g_t0 = (long)GetTickCount64();
    HANDLE port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    LOG("裸端口就绪(一枚句柄,没挂任何 I/O 设备)—— 端口自己就是一条完成队列");

    LOG("[1] 三条工线程睡进 GQCS,主线程逐个 PQCS 叫醒");
    std::vector<std::thread> ws;
    for (int i = 0; i < 3; ++i)
        ws.emplace_back(worker, port, i + 1);
    while (g_workers_up.load() < 3)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::this_thread::sleep_for(std::chrono::milliseconds(200)); // 让它们睡实
    LOG("三条线程都已睡进 GQCS,开始投关停包");
    for (int i = 0; i < 3; ++i) {
        BOOL p = PostQueuedCompletionStatus(port, 0, kShutdownKey, (LPOVERLAPPED)kSentinelOv);
        LOG("  PQCS 关停包 #%d: ret=%d", i + 1, (int)p);
    }
    for (auto& w : ws)
        w.join();
    LOG("三条工线程全部退场,join 干净返回");

    LOG("[2] 三件套原样透传:bytes=111 key=888 ov=0xABCD");
    {
        std::atomic<bool> got{false};
        std::thread t([&]() {
            DWORD bytes = 0;
            ULONG_PTR key = 0;
            LPOVERLAPPED pov = nullptr;
            GetQueuedCompletionStatus(port, &bytes, &key, &pov, 2000);
            std::printf("[%6ld ms]   收到: bytes=%lu key=%lu ov=0x%llx —— 塞什么,收什么\n",
                        ms_now(), bytes, (unsigned long)key, (unsigned long long)(UINT_PTR)pov);
            std::fflush(stdout);
            got.store(true);
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        PostQueuedCompletionStatus(port, 111, (ULONG_PTR)888, (LPOVERLAPPED)(UINT_PTR)0xABCD);
        t.join();
        if (!got.load())
            LOG("  (没收着,异常)");
    }

    LOG("[3] 收尾后再投一枚没人读的包,端口不吃亏");
    BOOL p = PostQueuedCompletionStatus(port, 1, (ULONG_PTR)1, (LPOVERLAPPED)(UINT_PTR)1);
    LOG("  PQCS: ret=%d —— 端口没有活线程也接得住,包在队列里等着", (int)p);

    CloseHandle(port);
    LOG("iocp-e4 完");
    return 0;
}
