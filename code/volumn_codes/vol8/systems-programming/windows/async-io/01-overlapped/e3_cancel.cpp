// e3_cancel.cpp —— E3 CancelIo / CancelIoEx:把在途的读撤下来
//
// 文件读太快(缓存秒完),要观察「在途」,得用一根不投喂就不完成的命名管道。四组:
//   [1] 单个在途读 → CancelIoEx(定向) → GetOverlappedResult 收 995(ERROR_OPERATION_ABORTED)
//   [2] 两个在途读 → CancelIo(全量) → 两个都收 995
//   [3] 两个在途读 → CancelIoEx 只点 A → A 收 995,B 不受影响、照常吃数据完成
//   [4] 撤完管道还活着:再投一发,写入方投喂,照常完成 —— 取消不留残骸
// 前情:file-io/05(锁篇)在 LockFileEx 的等待上用过 CancelIoEx→995,本篇是它在读请求上的正主。
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

const char* gle_name(DWORD e) {
    switch (e) {
        case ERROR_IO_PENDING:
            return "ERROR_IO_PENDING";
        case ERROR_IO_INCOMPLETE:
            return "ERROR_IO_INCOMPLETE(尚未完成,996)";
        case ERROR_OPERATION_ABORTED:
            return "ERROR_OPERATION_ABORTED";
        case ERROR_NOT_FOUND:
            return "ERROR_NOT_FOUND";
        default:
            return "(其他)";
    }
}

// 服务端写、客户端异步读的管道;writer connect 后等 go,go 置位后 delay 毫秒投喂 8 字节
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
    p.writer = std::thread([srv, delay_ms, &go, &connected]() {
        if (!ConnectNamedPipe(srv, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED) {
            std::printf("ConnectNamedPipe 失败 gle=%lu\n", GetLastError());
        }
        connected.fetch_add(1);
        while (!go.load(std::memory_order_acquire))
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        std::uint64_t v = (std::uint64_t)delay_ms;
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

DWORD post_read(HANDLE h, OVERLAPPED& ov, void* buf) {
    DWORD got = 0;
    SetLastError(0);
    BOOL ok = ReadFile(h, buf, 8, &got, &ov);
    DWORD e = GetLastError();
    if (ok)
        return 0;
    return e; // 997 = 在途
}

} // namespace

int main() {
    g_t0 = (long)GetTickCount64();
    std::atomic<bool> go{false};
    std::atomic<int> connected{0};
    std::uint64_t v = 0;
    DWORD got = 0;

    LOG("== [1] 定向撤单:CancelIoEx 指名道姓 ==");
    {
        std::atomic<bool> g1{false};
        Pipe P = make_pipe("wasync_e3_1_" + std::to_string(GetCurrentProcessId()), 100000, g1,
                           connected);
        while (connected.load() < 1)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        OVERLAPPED ov{};
        DWORD e = post_read(P.cli, ov, &v);
        LOG("投读: gle=%lu(%s) —— 无数据,在途", e, gle_name(e));
        BOOL c = CancelIoEx(P.cli, &ov);
        LOG("CancelIoEx(定向): ret=%d", (int)c);
        BOOL g = GetOverlappedResult(P.cli, &ov, &got, TRUE);
        LOG("GOR: ret=%d gle=%lu(%s) —— 请求被撤,以 995 收场", (int)g, GetLastError(),
            gle_name(GetLastError()));
        P.writer.detach(); // 这根管道永不投喂,writer 在 go 轮询里睡着,进程退场一并带走
        CloseHandle(P.cli);
        CloseHandle(P.srv);
    }

    LOG("== [2] 全量撤单:CancelIo 清光这把句柄上的在途 ==");
    {
        std::atomic<bool> g2{false};
        Pipe P = make_pipe("wasync_e3_2_" + std::to_string(GetCurrentProcessId()), 100000, g2,
                           connected);
        while (connected.load() < 2)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        OVERLAPPED ovA{}, ovB{};
        DWORD eA = post_read(P.cli, ovA, &v);
        DWORD eB = post_read(P.cli, ovB, &v);
        LOG("两发在途读: A gle=%lu B gle=%lu", eA, eB);
        BOOL c = CancelIo(P.cli);
        LOG("CancelIo(不带 OVERLAPPED,全量): ret=%d", (int)c);
        BOOL gA = GetOverlappedResult(P.cli, &ovA, &got, TRUE);
        DWORD e1 = GetLastError();
        BOOL gB = GetOverlappedResult(P.cli, &ovB, &got, TRUE);
        DWORD e2 = GetLastError();
        LOG("GOR(A): ret=%d gle=%lu(%s); GOR(B): ret=%d gle=%lu(%s) —— 一勺烩,都是 995", (int)gA,
            e1, gle_name(e1), (int)gB, e2, gle_name(e2));
        P.writer.detach(); // 同上,永不投喂
        CloseHandle(P.cli);
        CloseHandle(P.srv);
    }

    LOG("== [3] 定向撤 A,放过 B:B 照常吃数据完成 ==");
    {
        std::atomic<bool> g3{false};
        Pipe P =
            make_pipe("wasync_e3_3_" + std::to_string(GetCurrentProcessId()), 150, g3, connected);
        while (connected.load() < 3)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        HANDLE evB = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        OVERLAPPED ovA{}, ovB{};
        ovB.hEvent = evB; // B 给上事件,等它有据可依
        std::uint64_t vA = 0, vB = 0;
        DWORD eA = post_read(P.cli, ovA, &vA);
        DWORD eB = post_read(P.cli, ovB, &vB);
        LOG("两发在途读: A gle=%lu(无事件) B gle=%lu(带事件)", eA, eB);
        CancelIoEx(P.cli, &ovA);
        LOG("CancelIoEx 只点 A");
        g3.store(true); // 150ms 后投喂,B 顺理成章完成;A 已被撤,数据落不进 A
        BOOL gA = GetOverlappedResult(P.cli, &ovA, &got, TRUE);
        DWORD ea = GetLastError();
        DWORD wb = WaitForSingleObject(evB, 3000);
        BOOL gB = GetOverlappedResult(P.cli, &ovB, &got, FALSE);
        LOG("GOR(A): ret=%d gle=%lu(%s); B 的事件等待 %lu 后 GOR ret=%d got=%lu vB=%llu —— "
            "撤单不影响邻居",
            (int)gA, ea, gle_name(ea), (unsigned long)wb, (int)gB, got, (unsigned long long)vB);
        P.writer.join();
        CloseHandle(evB);
        CloseHandle(P.cli);
        CloseHandle(P.srv);
    }

    LOG("== [3b] 探针:OVERLAPPED.hEvent=NULL 的在途读,GOR(bWait=TRUE) 靠什么等 ==");
    {
        std::atomic<bool> g3b{false};
        Pipe P =
            make_pipe("wasync_e3_3b_" + std::to_string(GetCurrentProcessId()), 200, g3b, connected);
        while (connected.load() < 4)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        OVERLAPPED ov{};
        std::uint64_t v = 0;
        post_read(P.cli, ov, &v); // ov.hEvent=NULL
        g3b.store(true);          // 200ms 后才有数据
        long tw = ms_now();
        BOOL g = GetOverlappedResult(P.cli, &ov, &got, TRUE); // 没事件,只能指望句柄的信号位
        DWORD e = GetLastError();
        LOG("GOR(bWait=TRUE) 等了 %ld ms 返回: ret=%d gle=%lu(%s) —— "
            "单发在途时它落在句柄信号上,真等到了数据",
            ms_now() - tw, (int)g, e, gle_name(e));
        LOG("注:句柄信号整把只有一枚,多发行共享它准不准,见 e5 第二部分的两发在途实测");
        P.writer.join();
        CloseHandle(P.cli);
        CloseHandle(P.srv);
    }

    LOG("== [4] 撤完之后:管道还活着,再投一发照常完成 ==");
    {
        std::atomic<bool> g4{false};
        Pipe P = make_pipe("wasync_e3_4_" + std::to_string(GetCurrentProcessId()), 100000, g4,
                           connected);
        while (connected.load() < 5)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        OVERLAPPED ov1{};
        post_read(P.cli, ov1, &v);
        CancelIoEx(P.cli, &ov1);
        GetOverlappedResult(P.cli, &ov1, &got, TRUE);
        LOG("第一发已撤(995),同句柄再投一发");
        P.writer.detach(); // 永不投喂
        CloseHandle(P.cli);
        CloseHandle(P.srv);

        std::atomic<bool> g4b{false};
        std::atomic<int> c2{0};
        Pipe Q = make_pipe("wasync_e3_4b_" + std::to_string(GetCurrentProcessId()), 80, g4b, c2);
        while (c2.load() < 1)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        OVERLAPPED ov2{};
        std::uint64_t v2 = 0;
        DWORD e2 = post_read(Q.cli, ov2, &v2);
        g4b.store(true);
        BOOL g = GetOverlappedResult(Q.cli, &ov2, &got, TRUE);
        LOG("第二发: 投递 gle=%lu(%s),GOR ret=%d got=%lu v2=%llu —— 句柄没被取消弄坏", e2,
            gle_name(e2), (int)g, got, (unsigned long long)v2);
        Q.writer.join();
        CloseHandle(Q.cli);
        CloseHandle(Q.srv);
    }

    LOG("== [5] 线程归属:别的线程发的读,CancelIo(仅本线程)撤不动,CancelIoEx(全线程)才撤得动 ==");
    {
        std::atomic<bool> g5{false};
        Pipe P = make_pipe("wasync_e3_5_" + std::to_string(GetCurrentProcessId()), 100000, g5,
                           connected);
        while (connected.load() < 6)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        OVERLAPPED ov{};
        std::uint64_t v5 = 0;
        std::atomic<bool> posted{false};
        std::atomic<bool> worker_go{false};
        std::thread issuer([&]() {
            DWORD e = post_read(P.cli, ov, &v5); // 这发读归 worker 线程
            LOG("worker(tid=%lu)投读: gle=%lu(%s)", GetCurrentThreadId(), e, gle_name(e));
            posted.store(true);
            while (!worker_go.load())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        });
        while (!posted.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));

        BOOL c1 = CancelIo(P.cli); // 主线程调,但它没在这把句柄上发过任何请求
        BOOL g1 = GetOverlappedResult(P.cli, &ov, &got, FALSE);
        DWORD e1g = GetLastError();
        LOG("主线程 CancelIo: ret=%d;GOR(FALSE): ret=%d gle=%lu(%s) —— 在途纹丝不动,CancelIo "
            "只认调用线程的请求",
            (int)c1, (int)g1, e1g, gle_name(e1g));
        BOOL c2r = CancelIoEx(P.cli, nullptr); // 不点名单 = 全线程
        BOOL g2r = GetOverlappedResult(P.cli, &ov, &got, TRUE);
        DWORD e2g = GetLastError();
        LOG("主线程 CancelIoEx(不点名): ret=%d;GOR(TRUE): ret=%d gle=%lu(%s) —— 跨线程也照撤",
            (int)c2r, (int)g2r, e2g, gle_name(e2g));
        worker_go.store(true);
        issuer.join();
        P.writer.detach(); // 永不投喂
        CloseHandle(P.cli);
        CloseHandle(P.srv);
    }
    (void)go;
    LOG("e3 完");
    return 0;
}
