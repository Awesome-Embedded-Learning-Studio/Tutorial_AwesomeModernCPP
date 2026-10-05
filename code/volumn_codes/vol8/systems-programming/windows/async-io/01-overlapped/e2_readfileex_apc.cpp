// e2_readfileex_apc.cpp —— E2 ReadFileEx 完成例程:只在可警告等待里跑(兑现 process/02 的前向指针)
//
// process/02(APC 篇)引过文档原话:ReadFileEx 们的完成通知 "are implemented using an APC
// as the completion notification callback mechanism",当时把请它出场留给了本章。本实验就是
// 那次约定:两根命名管道,写入线程分别在各自 go 之后 120/160 ms 投喂数据,主线程投递
// ReadFileEx 后分三阶段观察完成例程:
//   阶段1 500ms 不可警告等待(WaitForSingleObjectEx ..., FALSE)→ 例程执行数应为 0:
//         数据早已落进管道,例程还在队列里睡觉 —— 不进可警告等待,完成例程一个都不跑
//   阶段2 进入可警告等待(TRUE)→ 两条例程同 tick 连跑(FIFO),等待以 192(WAIT_IO_COMPLETION)提前返回
//   阶段3 单发一发:数据在投递后约 250ms 到,例程跟着数据醒,还是 192
// 附带探针:ReadFileEx 不碰 OVERLAPPED.hEvent(塞个哨兵值进去,读回原样)。
// 等待对象全程用一枚永不置位的手动重置事件,排除 stdio 意外就绪的干扰。
// 环境:Win11 26200 / MSYS2 UCRT64 g++ 16.1.0,-std=c++20 -Wall -Wextra
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
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

std::atomic<int> g_cr_count{0};
std::atomic<int> g_cr_serial{0};
std::atomic<int> g_cr_order[3]{{0}, {0}, {0}}; // 例程到达序
std::atomic<long> g_cr_time[3]{{0}, {0}, {0}};

void CALLBACK completion_routine(DWORD err, DWORD bytes, LPOVERLAPPED ov) {
    int idx = (int)(LONG_PTR)ov->hEvent - 1; // 塞进 hEvent 的哨兵:例程编号
    g_cr_order[idx].store(++g_cr_serial);
    g_cr_time[idx].store(ms_now());
    g_cr_count.fetch_add(1);
    std::printf("[%6ld ms]   例程#%d 跑起来了: err=%lu bytes=%lu (tid=%lu)\n", ms_now(), idx + 1,
                err, bytes, GetCurrentThreadId());
    std::fflush(stdout);
}

// 一根「服务端写、客户端异步读」的管道:writer 线程 connect 后等各自的 go,再按 delay 投喂
struct Pipe {
    HANDLE srv = INVALID_HANDLE_VALUE;
    HANDLE cli = INVALID_HANDLE_VALUE;
    std::thread writer;
};

Pipe make_pipe(const std::string& name, int delay_ms, std::atomic<bool>& go,
               std::atomic<int>& connected) {
    Pipe p;
    std::string path = "\\\\.\\pipe\\" + name;
    p.srv = CreateNamedPipeA(path.c_str(), PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED,
                             PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0,
                             nullptr);
    if (p.srv == INVALID_HANDLE_VALUE) {
        std::printf("CreateNamedPipeA 失败 gle=%lu\n", GetLastError());
        std::exit(1);
    }
    HANDLE srv = p.srv;
    p.writer = std::thread([path, srv, delay_ms, &go, &connected]() {
        if (!ConnectNamedPipe(srv, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED) {
            std::printf("ConnectNamedPipe 失败 gle=%lu\n", GetLastError());
        }
        connected.fetch_add(1);
        while (!go.load(std::memory_order_acquire))
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        std::uint64_t v = (std::uint64_t)delay_ms;
        DWORD n = 0;
        OVERLAPPED ov{}; // 服务端实例带 OVERLAPPED 位,同步写也得给个 OVERLAPPED
        WriteFile(srv, &v, 8, &n, &ov);
    });
    for (;;) {
        p.cli = CreateFileA(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
        if (p.cli != INVALID_HANDLE_VALUE)
            break;
        if (GetLastError() != ERROR_PIPE_BUSY) {
            std::printf("客户端开门失败 gle=%lu\n", GetLastError());
            std::exit(1);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return p;
}

} // namespace

int main() {
    g_t0 = (long)GetTickCount64();
    HANDLE never = CreateEventW(nullptr, TRUE, FALSE, nullptr); // 永不置位,只当等待对象
    std::atomic<bool> goAB{false}, goC{false};
    std::atomic<int> connected{0};

    Pipe A =
        make_pipe("wasync_e2_A_" + std::to_string(GetCurrentProcessId()), 120, goAB, connected);
    Pipe B =
        make_pipe("wasync_e2_B_" + std::to_string(GetCurrentProcessId()), 160, goAB, connected);
    while (connected.load() < 2)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    OVERLAPPED ovA{}, ovB{};
    ovA.hEvent = (HANDLE)(LONG_PTR)1; // ReadFileEx 不用 hEvent,正好拿来当例程编号哨兵
    ovB.hEvent = (HANDLE)(LONG_PTR)2;
    std::uint64_t vA = 0, vB = 0;

    LOG("阶段0: 投递两个 ReadFileEx(投递序 A→B),放行写入方(120/160 ms 后投喂)");
    BOOL rA = ReadFileEx(A.cli, &vA, 8, &ovA, completion_routine);
    BOOL rB = ReadFileEx(B.cli, &vB, 8, &ovB, completion_routine);
    goAB.store(true);
    LOG("ReadFileEx 返回: A=%d B=%d;此刻例程执行数=%d(投递不等于执行)", (int)rA, (int)rB,
        g_cr_count.load());

    LOG("阶段1: 500ms 不可警告等待(FALSE)—— 数据会在这 500ms 里到,例程该一个都不跑");
    DWORD w1 = WaitForSingleObjectEx(never, 500, FALSE);
    LOG("WaitForSingleObjectEx(...,500,FALSE) 返回 %lu(258=超时),此刻例程执行数=%d —— "
        "数据在管道里,例程在睡觉",
        (unsigned long)w1, g_cr_count.load());

    LOG("阶段2: 进入可警告等待(TRUE,限期 3000)—— 两条积压例程该同 tick 连跑");
    long t_wait = ms_now();
    DWORD w2 = WaitForSingleObjectEx(never, 3000, TRUE);
    LOG("可警告等待在 %ld ms 返回 %lu(192=WAIT_IO_COMPLETION),等待历时 %ld ms,例程执行数=%d",
        t_wait, (unsigned long)w2, ms_now() - t_wait, g_cr_count.load());
    LOG("到达序: A=%d B=%d(FIFO);A 例程时刻=%ld ms,B 例程时刻=%ld ms;vA=%llu vB=%llu",
        g_cr_order[0].load(), g_cr_order[1].load(), g_cr_time[0].load(), g_cr_time[1].load(),
        (unsigned long long)vA, (unsigned long long)vB);
    LOG("哨兵探针: ovA.hEvent 读回 %ld, ovB.hEvent 读回 %ld —— ReadFileEx 没碰过它们",
        (long)(LONG_PTR)ovA.hEvent, (long)(LONG_PTR)ovB.hEvent);

    LOG("阶段3: 单发一发,数据约在投递后 250ms 到");
    Pipe C = make_pipe("wasync_e2_C_" + std::to_string(GetCurrentProcessId()), 250, goC, connected);
    while (connected.load() < 3)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    OVERLAPPED ovC{};
    ovC.hEvent = (HANDLE)(LONG_PTR)3; // 哨兵:例程编号 3
    std::uint64_t vC = 0;
    BOOL rC = ReadFileEx(C.cli, &vC, 8, &ovC, completion_routine);
    t_wait = ms_now();
    goC.store(true);
    DWORD w3 = WaitForSingleObjectEx(never, 3000, TRUE);
    LOG("ReadFileEx 返回 %d;可警告等待 %ld ms 起跑,数据 250ms 后到,例程在 %ld ms 跑、等待返回 "
        "%lu;vC=%llu",
        (int)rC, t_wait, g_cr_time[2].load(), (unsigned long)w3, (unsigned long long)vC);
    LOG("此刻例程执行总数=%d(1+1+1)", g_cr_count.load());

    A.writer.join();
    B.writer.join();
    C.writer.join();
    LOG("e2 完");
    return 0;
}
