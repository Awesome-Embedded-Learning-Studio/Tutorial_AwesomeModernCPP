// e5_scatter_events.cpp —— E5 一把句柄六发在途:完成次序散开 + hEvent 的两种给法
//
// 异步 I/O 的真本事不是「不阻塞」,是「投递序 ≠ 完成序」:六根管道,读端按 1..6 投递,
// 写端反着来(6 先写,每根隔 60ms),完成次序该是 6,5,4,3,2,1。收割用
// WaitForMultipleObjects(六枚事件, bWaitAll=FALSE)循环。
// 第二部分:hEvent=NULL 的给法 —— 完成信号落在句柄自己身上,等句柄本身;两发在途时
// 句柄什么时候亮、亮的是哪一发,做一次实测。
// 本实验的同一套管道场景,在 IOCP 篇 e2 里用完成端口再收一遍,两篇互为镜像。
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

struct Pipe {
    HANDLE srv = INVALID_HANDLE_VALUE;
    HANDLE cli = INVALID_HANDLE_VALUE;
    std::thread writer;
};

// writer connect 后等 go,go 后 delay 毫秒投喂 8 字节(值为 tag)
Pipe make_pipe(const std::string& name, int delay_ms, int tag, std::atomic<bool>& go,
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
    p.writer = std::thread([srv, delay_ms, tag, &go, &connected]() {
        if (!ConnectNamedPipe(srv, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED) {
            std::printf("ConnectNamedPipe 失败 gle=%lu\n", GetLastError());
        }
        connected.fetch_add(1);
        while (!go.load(std::memory_order_acquire))
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        std::uint64_t v = (std::uint64_t)tag;
        DWORD n = 0;
        OVERLAPPED ov{};
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
    constexpr int kN = 6;
    std::atomic<bool> go{false};
    std::atomic<int> connected{0};
    std::vector<Pipe> pipes;
    std::vector<OVERLAPPED> ovs(kN);
    std::vector<HANDLE> evs(kN);
    std::vector<std::uint64_t> vals(kN, 0);

    // 写端按 6,5,4,3,2,1 的次序投喂:管道 i(读端投递序 i+1)的写入延迟 = (kN-i)*60
    for (int i = 0; i < kN; ++i) {
        int delay = (kN - i) * 60; // i=0(投递序 1)最晚 360ms;i=5(投递序 6)最早 60ms
        pipes.push_back(make_pipe("wasync_e5_" + std::to_string(i) + "_" +
                                      std::to_string(GetCurrentProcessId()),
                                  delay, i + 1, go, connected));
    }
    while (connected.load() < kN)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    for (int i = 0; i < kN; ++i) {
        evs[i] = CreateEventW(nullptr, TRUE, FALSE, nullptr); // 手动重置(异步 I/O 的要求)
        ovs[i].hEvent = evs[i];
    }
    LOG("六发在途读按投递序 1..6 发出(写端将按 6..1 投喂,间隔 60ms)");
    for (int i = 0; i < kN; ++i) {
        DWORD got = 0;
        SetLastError(0);
        BOOL ok = ReadFile(pipes[i].cli, &vals[i], 8, &got, &ovs[i]);
        LOG("  投递#%d: ret=%d gle=%lu(997=在途)", i + 1, (int)ok, GetLastError());
    }
    go.store(true);

    LOG("收割循环:WFMO(六事件,任一,bWaitAll=FALSE)");
    int done = 0;
    std::vector<int> order;
    while (done < kN) {
        DWORD w = WaitForMultipleObjects(kN, evs.data(), FALSE, 5000);
        if (w == WAIT_FAILED || w == WAIT_TIMEOUT) {
            LOG("WFMO 异常: %lu gle=%lu", w, GetLastError());
            break;
        }
        int idx = (int)(w - WAIT_OBJECT_0);
        DWORD got = 0;
        BOOL g = GetOverlappedResult(pipes[idx].cli, &ovs[idx], &got, FALSE);
        ++done;
        order.push_back(idx + 1);
        LOG("  完成第 %d 个:是投递#%d(GOR ret=%d got=%lu 值=%llu)", done, idx + 1, (int)g, got,
            (unsigned long long)vals[idx]);
        ResetEvent(evs[idx]); // 手动重置,收完亲手归零
    }
    LOG("投递序: 1 2 3 4 5 6;完成序:");
    std::printf("[%6ld ms]  ", ms_now());
    for (int o : order)
        std::printf("%d ", o);
    std::printf("(与写端投喂次序一致,与投递序相反)\n");
    std::fflush(stdout);
    for (auto& p : pipes)
        p.writer.join();

    LOG("== 第二部分:hEvent=NULL,完成信号落在句柄身上 ==");
    {
        std::atomic<bool> g2{false};
        std::atomic<int> c2{0};
        Pipe P =
            make_pipe("wasync_e5_single_" + std::to_string(GetCurrentProcessId()), 200, 77, g2, c2);
        while (c2.load() < 1)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        OVERLAPPED ov{};
        std::uint64_t v = 0;
        DWORD got = 0;
        SetLastError(0);
        BOOL ok = ReadFile(P.cli, &v, 8, &got, &ov); // ov.hEvent 就是 NULL
        LOG("单发在途: ret=%d gle=%lu,直接等【句柄本身】", (int)ok, GetLastError());
        long tw = ms_now();
        g2.store(true);
        DWORD w = WaitForSingleObject(P.cli, 3000);
        LOG("WaitForSingleObject(管道句柄): %ld ms 处返回 %lu —— hEvent=NULL 时句柄自己当信号",
            ms_now() - tw, (unsigned long)w);
        BOOL g = GetOverlappedResult(P.cli, &ov, &got, FALSE);
        LOG("GOR: ret=%d got=%lu 值=%llu", (int)g, got, (unsigned long long)v);
        P.writer.join();

        // 两发在途(都 hEvent=NULL):第一发数据 150ms 到,第二发 450ms 到,看句柄何时亮
        // 专用双投喂管道:writer 投两包,150ms 与再 300ms 后
        std::string path = "\\\\.\\pipe\\wasync_e5_two2_" + std::to_string(GetCurrentProcessId());
        HANDLE srv = CreateNamedPipeA(path.c_str(), PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED,
                                      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 4096,
                                      4096, 0, nullptr);
        std::atomic<bool> gw{false};
        std::thread wr = std::thread([srv, &gw]() {
            ConnectNamedPipe(srv, nullptr);
            while (!gw.load())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            std::uint64_t v1 = 1, v2 = 2;
            DWORD n = 0;
            OVERLAPPED o{};
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            WriteFile(srv, &v1, 8, &n, &o);
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            WriteFile(srv, &v2, 8, &n, &o);
        });
        HANDLE cli = CreateFileA(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
        OVERLAPPED ov1{}, ov2{};
        std::uint64_t va = 0, vb = 0;
        DWORD e1v = 0, e2v = 0;
        ReadFile(cli, &va, 8, &e1v, &ov1);
        e1v = GetLastError();
        ReadFile(cli, &vb, 8, &e2v, &ov2);
        e2v = GetLastError();
        LOG("两发在途(都 hEvent=NULL): gle=%lu/%lu;第一包 150ms 到,第二包 450ms 到", e1v, e2v);
        long tw2 = ms_now();
        gw.store(true);
        DWORD w2 = WaitForSingleObject(cli, 5000);
        long dt = ms_now() - tw2;
        LOG("WaitForSingleObject(句柄)在 %ld ms 返回 %lu —— "
            "对齐两包:亮在全部完成还是任一完成,数字说了算",
            dt, (unsigned long)w2);
        DWORD ga1 = 0, ga2 = 0;
        BOOL r1 = GetOverlappedResult(cli, &ov1, &ga1, FALSE);
        BOOL r2 = GetOverlappedResult(cli, &ov2, &ga2, FALSE);
        LOG("GOR: 第一发 ret=%d 值=%llu;第二发 ret=%d 值=%llu", (int)r1, (unsigned long long)va,
            (int)r2, (unsigned long long)vb);
        wr.join();
        CloseHandle(cli);
        CloseHandle(srv);
    }

    for (auto h : evs)
        CloseHandle(h);
    LOG("e5 完");
    return 0;
}
