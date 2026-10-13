// e5_close_inflight.cpp —— IOCP E5 在途未决时关句柄:完成包的下场
//
// 文档的劝告是「在途 I/O 未收尾前别关句柄,更别释放 OVERLAPPED」;劝告之外,实测看看:
//   [1] 野路子:读在途,直接 CloseHandle —— GQCS(6s 限期)到底收不收得到那发请求的
//       完成包(收 995?收 232 断管?还是压根不来、超时 258?),Win11 26200 上数字说话
//   [2] 正路子:先 CancelIoEx 撤单、等 GQCS 收到 995 的完成包,再关句柄 —— 干净
// OVERLAPPED 全程保活(全局存储),不给「驱动写进已释放内存」的机会,只观察通知语义。
// 环境:Win11 26200 / MSYS2 UCRT64 g++ 16.1.0,-std=c++20 -Wall -Wextra
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <chrono>
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
        case ERROR_OPERATION_ABORTED:
            return "ERROR_OPERATION_ABORTED";
        case ERROR_BROKEN_PIPE:
            return "ERROR_BROKEN_PIPE";
        case WAIT_TIMEOUT:
            return "WAIT_TIMEOUT";
        case ERROR_IO_PENDING:
            return "ERROR_IO_PENDING";
        default:
            return "(其他)";
    }
}

// 服务端只 connect 不投喂
struct PipePair {
    HANDLE srv = INVALID_HANDLE_VALUE;
    HANDLE cli = INVALID_HANDLE_VALUE;
    OVERLAPPED ov{}; // 全局保活,绝不提前释放
    std::thread writer;
};

PipePair make_unfed_pipe(const std::string& name) {
    PipePair p;
    std::string path = "\\\\.\\pipe\\" + name;
    p.srv = CreateNamedPipeA(path.c_str(), PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED,
                             PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0,
                             nullptr);
    HANDLE srv = p.srv;
    p.writer = std::thread([srv]() {
        ConnectNamedPipe(srv, nullptr);
        for (;;)
            std::this_thread::sleep_for(std::chrono::milliseconds(50)); // 永不投喂
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

} // namespace

int main() {
    g_t0 = (long)GetTickCount64();
    HANDLE port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);

    LOG("== [1] 野路子:读在途,直接 CloseHandle ==");
    {
        PipePair P = make_unfed_pipe("wasync_ic5_wild_" + std::to_string(GetCurrentProcessId()));
        CreateIoCompletionPort(P.cli, port, (ULONG_PTR)1, 0);
        std::uint64_t v = 0;
        DWORD got = 0;
        SetLastError(0);
        BOOL ok = ReadFile(P.cli, &v, 8, &got, &P.ov);
        LOG("投一发在途读: ret=%d gle=%lu(%s),然后直接关句柄", (int)ok, GetLastError(),
            gle_name(GetLastError()));
        CloseHandle(P.cli);
        LOG("CloseHandle 已返回(句柄值这会儿已经作废),GQCS 等它的完成包,限期 6000ms");

        DWORD bytes = 0;
        ULONG_PTR key = 0;
        LPOVERLAPPED pov = nullptr;
        long tw = ms_now();
        BOOL g = GetQueuedCompletionStatus(port, &bytes, &key, &pov, 6000);
        DWORD e = GetLastError();
        LOG("GQCS: ret=%d 历时 %ld ms gle=%lu(%s) pov=%s —— 野路子的下场,实测口径", (int)g,
            ms_now() - tw, e, gle_name(e), pov == &P.ov ? "就是那发读" : "(空/别的东西)");
        P.writer.detach();
        CloseHandle(P.srv);
    }

    LOG("== [2] 正路子:CancelIoEx 撤单 → 等 995 完成包 → 再关 ==");
    {
        PipePair P = make_unfed_pipe("wasync_ic5_proper_" + std::to_string(GetCurrentProcessId()));
        CreateIoCompletionPort(P.cli, port, (ULONG_PTR)2, 0);
        std::uint64_t v = 0;
        DWORD got = 0;
        SetLastError(0);
        BOOL ok = ReadFile(P.cli, &v, 8, &got, &P.ov);
        LOG("投一发在途读: ret=%d gle=%lu(%s)", (int)ok, GetLastError(), gle_name(GetLastError()));
        BOOL c = CancelIoEx(P.cli, &P.ov);
        LOG("CancelIoEx(定向): ret=%d", (int)c);
        DWORD bytes = 0;
        ULONG_PTR key = 0;
        LPOVERLAPPED pov = nullptr;
        long tw = ms_now();
        BOOL g = GetQueuedCompletionStatus(port, &bytes, &key, &pov, 3000);
        DWORD e = GetLastError();
        LOG("GQCS: ret=%d 历时 %ld ms gle=%lu(%s) pov=%s key=%lu —— 撤单也有完成包,995 送到",
            (int)g, ms_now() - tw, e, gle_name(e), pov == &P.ov ? "就是那发读" : "(空)",
            (unsigned long)key);
        CloseHandle(P.cli);
        LOG("收完 995 再 CloseHandle:干干净净");
        P.writer.detach();
        CloseHandle(P.srv);
    }

    CloseHandle(port);
    LOG("iocp-e5 完");
    return 0;
}
