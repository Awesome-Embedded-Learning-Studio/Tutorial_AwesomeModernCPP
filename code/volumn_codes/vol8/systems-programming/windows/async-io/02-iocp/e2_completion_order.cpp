// e2_completion_order.cpp —— IOCP E2 投递 N 发的完成次序:端口收的是完成序,不是投递序
//
// 六根命名管道,读端按 1..6 投递,写端反着投喂(6 先,每根隔 60ms)——与 OVERLAPPED 篇
// e5 完全同一套场景,收割这边换成单线程 GQCS 循环:每个完成包里的 OVERLAPPED 指针
// 自报家门,完成序与投递序当场对不上号。
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
        ConnectNamedPipe(srv, nullptr);
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
    HANDLE port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);

    std::vector<Pipe> pipes;
    std::vector<OVERLAPPED> ovs(kN);
    std::vector<std::uint64_t> vals(kN, 0);
    for (int i = 0; i < kN; ++i) {
        int delay = (kN - i) * 60; // 写端 6..1 投喂
        pipes.push_back(make_pipe("wasync_ic2_" + std::to_string(i) + "_" +
                                      std::to_string(GetCurrentProcessId()),
                                  delay, i + 1, go, connected));
        CreateIoCompletionPort(pipes[i].cli, port, (ULONG_PTR)(i + 1), 0); // key 就用投递序号
    }
    while (connected.load() < kN)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    LOG("六发在途读按 1..6 投递,全部挂同一端口(写端将按 6..1 投喂,间隔 60ms)");
    for (int i = 0; i < kN; ++i) {
        DWORD got = 0;
        SetLastError(0);
        BOOL ok = ReadFile(pipes[i].cli, &vals[i], 8, &got, &ovs[i]);
        LOG("  投递#%d: ret=%d gle=%lu", i + 1, (int)ok, GetLastError());
    }
    go.store(true);

    LOG("收割循环:单线程 GQCS 六连取");
    std::vector<int> order;
    for (int k = 0; k < kN; ++k) {
        DWORD bytes = 0;
        ULONG_PTR key = 0;
        LPOVERLAPPED pov = nullptr;
        BOOL g = GetQueuedCompletionStatus(port, &bytes, &key, &pov, 5000);
        int who = -1;
        for (int i = 0; i < kN; ++i)
            if (pov == &ovs[i])
                who = i;
        order.push_back(who + 1);
        LOG("  完成第 %d 个:是投递#%d(GQCS ret=%d bytes=%lu key=%lu 值=%llu)", k + 1, who + 1,
            (int)g, bytes, (unsigned long)key, who >= 0 ? (unsigned long long)vals[who] : 0ull);
    }
    std::printf("[%6ld ms]  投递序: 1 2 3 4 5 6;完成序: ", ms_now());
    for (int o : order)
        std::printf("%d ", o);
    std::printf("\n");
    std::fflush(stdout);

    for (auto& p : pipes)
        p.writer.join();
    CloseHandle(port);
    LOG("iocp-e2 完");
    return 0;
}
