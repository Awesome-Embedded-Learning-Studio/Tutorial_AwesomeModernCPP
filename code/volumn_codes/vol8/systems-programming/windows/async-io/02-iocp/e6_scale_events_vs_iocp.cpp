// e6_scale_events_vs_iocp.cpp —— IOCP E6 事件式 OVERLAPPED 与完成端口的规模对照
//
// 同一件事做两遍:一根管道上挂 100 发 1 字节在途读,写端一口气投 100 字节。
//   A(事件式):100 枚手动重置事件;WFMO 一次至多 64 枚,只能分两段(64+36)轮着等;
//              每次醒来不知道谁好了,得把 100 枚事件挨个 WaitForSingleObject(ev,0) 扫一遍
//   B(IOCP): 一枚端口;GQCS 循环 100 次,每次取出的就是完成的那发,零扫描
// 统计:各自收完 100 发的墙钟时间、事件式的总扫描次数(每次扫描 = 100 枚探针)与
// 空转收割轮数、两边句柄数。连跑 5 轮取稳定口径。
// OVERLAPPED 篇 e4 摸到的 64 墙,在这里变成事件式绕不开的工程成本;IOCP 生来没有这堵墙。
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

constexpr int kN = 100;
constexpr int kChunk = 64; // WFMO 一段的容量(MAXIMUM_WAIT_OBJECTS)

// 一根管道:写端 connect 后等 go,go 后一次性投 kN 字节
struct Pipe {
    HANDLE srv = INVALID_HANDLE_VALUE;
    HANDLE cli = INVALID_HANDLE_VALUE;
    std::thread writer;
};

Pipe make_pipe(const std::string& name, std::atomic<bool>& go) {
    Pipe p;
    std::string path = "\\\\.\\pipe\\" + name;
    p.srv = CreateNamedPipeA(path.c_str(), PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED,
                             PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 8192, 8192, 0,
                             nullptr);
    if (p.srv == INVALID_HANDLE_VALUE) {
        std::printf("CreateNamedPipeA 失败 gle=%lu\n", GetLastError());
        std::exit(1);
    }
    HANDLE srv = p.srv;
    p.writer = std::thread([srv, &go]() {
        ConnectNamedPipe(srv, nullptr);
        while (!go.load(std::memory_order_acquire))
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        char data[kN];
        for (int i = 0; i < kN; ++i)
            data[i] = (char)('a' + i % 26);
        DWORD n = 0;
        OVERLAPPED ov{};
        WriteFile(srv, data, kN, &n, &ov); // 一口气 100 字节
    });
    for (;;) {
        p.cli = CreateFileA(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
        if (p.cli != INVALID_HANDLE_VALUE)
            break;
        if (GetLastError() != ERROR_PIPE_BUSY) {
            std::printf("开门失败 gle=%lu\n", GetLastError());
            std::exit(1);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return p;
}

struct RoundStat {
    long ms = 0;
    long long scans = 0; // 事件探针总次数(每次扫 = 逐枚 WaitForSingleObject(ev,0))
    int wake_rounds = 0; // 收割轮数(WFMO 醒来的次数)
    int handled = 0;
};

RoundStat round_events(int round) {
    std::atomic<bool> go{false};
    Pipe P = make_pipe(
        "wasync_ic6_ev_" + std::to_string(round) + "_" + std::to_string(GetCurrentProcessId()), go);
    std::vector<HANDLE> evs(kN);
    std::vector<OVERLAPPED> ovs(kN);
    std::vector<char> buf(kN);
    std::vector<char> taken(kN, 0);
    for (int i = 0; i < kN; ++i) {
        evs[i] = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        ovs[i].hEvent = evs[i];
    }
    for (int i = 0; i < kN; ++i) {
        DWORD got = 0;
        ReadFile(P.cli, &buf[i], 1, &got, &ovs[i]);
    }
    RoundStat st;
    int collected = 0;
    long t0 = ms_now();
    go.store(true);
    while (collected < kN) {
        // 两段轮等:64 + 36,这是 64 墙逼出来的结构
        for (DWORD base = 0; base < (DWORD)kN; base += kChunk) {
            DWORD cnt = (kN - base) > kChunk ? (DWORD)kChunk : (DWORD)(kN - base);
            WaitForMultipleObjects(cnt, &evs[base], FALSE, 100);
            ++st.wake_rounds;
        }
        // 全量扫描:醒来只说明「某一段里有好的」,谁好还得挨个问
        for (int i = 0; i < kN; ++i) {
            ++st.scans;
            if (WaitForSingleObject(evs[i], 0) == WAIT_OBJECT_0) {
                DWORD got = 0;
                if (!taken[i] && GetOverlappedResult(P.cli, &ovs[i], &got, FALSE)) {
                    taken[i] = 1;
                    ++collected;
                }
                ResetEvent(evs[i]);
            }
        }
    }
    st.handled = collected;
    st.ms = ms_now() - t0;
    P.writer.join();
    for (auto h : evs)
        CloseHandle(h);
    CloseHandle(P.cli);
    CloseHandle(P.srv);
    return st;
}

RoundStat round_iocp(int round) {
    std::atomic<bool> go{false};
    Pipe P = make_pipe(
        "wasync_ic6_ic_" + std::to_string(round) + "_" + std::to_string(GetCurrentProcessId()), go);
    HANDLE port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    CreateIoCompletionPort(P.cli, port, (ULONG_PTR)5, 0);
    std::vector<OVERLAPPED> ovs(kN);
    std::vector<char> buf(kN);
    for (int i = 0; i < kN; ++i) {
        DWORD got = 0;
        ReadFile(P.cli, &buf[i], 1, &got, &ovs[i]);
    }
    RoundStat st;
    long t0 = ms_now();
    go.store(true);
    for (int k = 0; k < kN; ++k) {
        DWORD bytes = 0;
        ULONG_PTR key = 0;
        LPOVERLAPPED pov = nullptr;
        BOOL g = GetQueuedCompletionStatus(port, &bytes, &key, &pov, 2000);
        if (g || pov)
            ++st.handled; // 每包恰好对应一发
    }
    st.ms = ms_now() - t0;
    P.writer.join();
    CloseHandle(P.cli);
    CloseHandle(P.srv);
    CloseHandle(port);
    return st;
}

} // namespace

int main() {
    g_t0 = (long)GetTickCount64();
    LOG("100 发在途 1 字节读,事件式(WFMO 分段 64+36)与 IOCP(单端口循环)各收 5 轮");
    LOG("每轮:写端一次性投 100 字节,读端从投满那一刻起计时");

    for (int r = 1; r <= 5; ++r) {
        RoundStat a = round_events(r);
        RoundStat b = round_iocp(r);
        LOG("第%d轮: 事件式 %3ld ms / 扫描 %lld 次探针 / 收割轮 %d / 完成 %d;"
            " IOCP %3ld ms / 零扫描 / 完成 %d / 句柄 100 事件 vs 1 端口",
            r, a.ms, a.scans, a.wake_rounds, a.handled, b.ms, b.handled);
    }
    LOG("口径注记:两边的毫秒都由管道 I/O 主导,量级相当是常态;差别在结构 —— "
        "事件式醒来后必须全量重扫(100 枚 × 收割轮数),IOCP 每次取出的就是完成的那发;"
        "100>64,事件式还被 64 墙逼成分段轮询,IOCP 一枚端口没有这道坎");
    LOG("iocp-e6 完");
    return 0;
}
