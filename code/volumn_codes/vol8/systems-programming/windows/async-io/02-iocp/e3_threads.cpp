// e3_threads.cpp —— IOCP E3 多线程并发取同一端口:每个完成包只落到一个线程手里
//
// 四个工线程全部阻塞在 GQCS(INFINITE),主线程用 PostQueuedCompletionStatus 分两波投
// 8 个「合成完成包」(间隔 50ms):谁醒了、拿了哪包、一人一次不重复,记录在案。
// 再造一枚并发值=1 的端口做对照:同样 4 线程 8 包,醒法有没有被并发值捏住,时序说话。
// 注意:并发值是调度提示不是硬闸,实测解读留有余地。
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

struct Stat {
    std::atomic<int> taken{0};
    std::atomic<long> first_take{0};
};

void worker(HANDLE port, Stat* st, std::atomic<bool>* stop, int id) {
    for (;;) {
        DWORD bytes = 0;
        ULONG_PTR key = 0;
        LPOVERLAPPED pov = nullptr;
        BOOL g = GetQueuedCompletionStatus(port, &bytes, &key, &pov, INFINITE);
        if (!g && pov == nullptr) { // 超时/异常醒来,没包
            if (stop->load())
                return;
            continue;
        }
        if (key == (ULONG_PTR)-1) { // 关停标记
            std::printf("[%6ld ms]   工%d 收到关停标记,收工 (tid=%lu)\n", ms_now(), id,
                        GetCurrentThreadId());
            std::fflush(stdout);
            return;
        }
        int n = st->taken.fetch_add(1) + 1;
        if (n == 1)
            st->first_take.store(ms_now());
        std::printf("[%6ld ms]   工%d 拿到包#%d key=%lu bytes=%lu (tid=%lu)\n", ms_now(), id, n,
                    (unsigned long)key, bytes, GetCurrentThreadId());
        std::fflush(stdout);
    }
}

void run_round(HANDLE port, int nworkers, int npackets, int stagger_ms) {
    Stat st;
    std::atomic<bool> stop{false};
    std::vector<std::thread> ws;
    for (int i = 0; i < nworkers; ++i)
        ws.emplace_back(worker, port, &st, &stop, i + 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(300)); // 等四条线程都睡进 GQCS

    for (int i = 0; i < npackets; ++i) {
        PostQueuedCompletionStatus(port, (DWORD)(100 + i), (ULONG_PTR)(i + 1),
                                   (LPOVERLAPPED)(UINT_PTR)(0xC0DE0000ull + (unsigned long long)i));
        if (stagger_ms > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(stagger_ms));
    }
    while (st.taken.load() < npackets)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    LOG("  %d 包全部有了主,首包被取走时刻 %ld ms", npackets, st.first_take.load());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    for (int i = 0; i < nworkers; ++i)
        PostQueuedCompletionStatus(port, 0, (ULONG_PTR)-1, nullptr);
    for (auto& w : ws)
        w.join();
}

} // namespace

int main() {
    g_t0 = (long)GetTickCount64();
    SYSTEM_INFO si{};
    GetSystemInfo(&si);

    LOG("== 端口A:并发值=0(默认=处理器数,本机 %lu),4 工线程,8 包间隔 50ms ==",
        si.dwNumberOfProcessors);
    HANDLE portA = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    long tA = ms_now();
    run_round(portA, 4, 8, 50);
    LOG("端口A 收完 8 包,历时 %ld ms", ms_now() - tA);

    LOG("== 端口B:并发值=1,同样 4 工线程、8 包间隔 50ms ==");
    HANDLE portB = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
    long tB = ms_now();
    run_round(portB, 4, 8, 50);
    LOG("端口B 收完 8 包,历时 %ld ms —— 与端口A对照,醒法差别见各行 tid 分布", ms_now() - tB);

    LOG("== 端口C:并发值=1,4 工线程,8 包一口气全投(无间隔) ==");
    HANDLE portC = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
    long tC = ms_now();
    run_round(portC, 4, 8, 0);
    LOG("端口C 收完 8 包,历时 %ld ms", ms_now() - tC);

    CloseHandle(portA);
    CloseHandle(portB);
    CloseHandle(portC);
    LOG("iocp-e3 完");
    return 0;
}
