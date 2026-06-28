// Modern C++ echo server —— RAII UniqueFd + std::expected + 每连接一线程。
// 配套 documents/vol8-domains/networking/01-modern-socket-wrapping.md。
// 这是 00 传统版的"现代化 + 加并发"版本:close 不可能漏、错误带上下文、能扛并发客户端。
// 但"每连接一线程"扛不住 C10K——文章结尾实测给你看。
#include "unique_fd.hpp"

#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <expected>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <print>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace {

constexpr std::uint16_t kPort = 13013;
constexpr int kBacklog = 64;
constexpr std::size_t kBufSize = 4096;

/// 带 errno + 上下文的错误,供 std::expected 携带(替代 00 版散落的 errno + perror)。
struct SysError {
    int errno_value;
    std::string context;
};

/// socket + bind + listen 三步。任一失败把 errno 和失败步骤塞进 SysError 返回。
std::expected<UniqueFd, SysError> make_listener(std::uint16_t port) {
    int raw = ::socket(AF_INET, SOCK_STREAM, 0);
    if (raw < 0)
        return std::unexpected(SysError{errno, "socket"});
    UniqueFd fd{raw};

    int yes = 1;
    if (::setsockopt(fd.get(), SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0)
        return std::unexpected(SysError{errno, "setsockopt(SO_REUSEADDR)"});

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (::bind(fd.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
        return std::unexpected(SysError{errno, "bind"});

    if (::listen(fd.get(), kBacklog) < 0)
        return std::unexpected(SysError{errno, "listen"});

    return fd;
}

/// 一条连接的 echo:循环读到对端关闭(返回 0)或出错。
/// conn 按值传入——线程独占这条 fd,函数返回时 UniqueFd 析构自动 close(不可能漏)。
void handle_session(UniqueFd conn) {
    std::array<char, kBufSize> buf;
    for (;;) {
        ssize_t n = ::read(conn.get(), buf.data(), buf.size());
        if (n == 0)
            break; // 对端正常关闭
        if (n < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        std::size_t off = 0;
        while (off < static_cast<std::size_t>(n)) {
            ssize_t w = ::write(conn.get(), buf.data() + off, static_cast<std::size_t>(n) - off);
            if (w < 0) {
                if (errno == EINTR)
                    continue;
                return;
            }
            off += static_cast<std::size_t>(w);
        }
    }
}

} // namespace

int main() {
    std::signal(SIGPIPE, SIG_IGN);

    auto listener = make_listener(kPort);
    if (!listener) {
        std::print(stderr, "listen failed: {} (errno {})\n", listener.error().context,
                   listener.error().errno_value);
        return 1;
    }
    std::print("modern echo server on 0.0.0.0:{} (pid {})\n", kPort, ::getpid());

    for (;;) {
        int raw = ::accept(listener->get(), nullptr, nullptr);
        if (raw < 0) {
            if (errno == EINTR)
                continue;
            std::print(stderr, "accept: errno {}\n", errno);
            continue;
        }
        UniqueFd conn{raw};
        // 每个连接 spawn 一个线程;conn 用 move 交给线程,主循环立刻回去 accept。
        std::thread{handle_session, std::move(conn)}.detach();
    }
}
