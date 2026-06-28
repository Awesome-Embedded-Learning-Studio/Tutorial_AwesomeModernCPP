// Lab 0 测试:mini Reactor echo server 的 MS1-4 对抗性验收。
// 配套 documents/vol8-domains/networking/lab0-mini-reactor.md。
// 用法:先在 src/reactor.cpp 里(对照 reactor.hpp)实现 Reactor,再跑这些测试。
#include <catch2/catch_test_macros.hpp>

#include "net/reactor.hpp"
#include "net/unique_fd.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <future>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr std::uint16_t kPortBase = 14000; // 每个用例用不同端口,避免 TIME_WAIT 互撞

bool set_nonblock(int fd) {
    int fl = ::fcntl(fd, F_GETFL, 0);
    return fl >= 0 && ::fcntl(fd, F_SETFL, fl | O_NONBLOCK) >= 0;
}

int make_listener(std::uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(port);
    ::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a));
    ::listen(fd, 64);
    set_nonblock(fd);
    return fd;
}

int connect_client(std::uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    ::inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    ::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a));
    return fd;
}

// 带超时的读:在 timeout_ms 内尽量读,返回累计读到的字节数。
std::size_t read_with_timeout(int fd, std::size_t want, int timeout_ms) {
    std::size_t got = 0;
    std::vector<char> buf(4096);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (got < want) {
        int remain = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          deadline - std::chrono::steady_clock::now())
                                          .count());
        if (remain <= 0)
            break;
        ::pollfd p{fd, POLLIN, 0};
        int r = ::poll(&p, 1, remain);
        if (r <= 0)
            break;
        ssize_t n = ::read(fd, buf.data(), buf.size());
        if (n <= 0)
            break;
        got += static_cast<std::size_t>(n);
    }
    return got;
}

// 一个正确的 echo 连接 handler:循环读到 EAGAIN,每段循环写到发完。
// captures reactor 以便 EOF 时 remove 自己。
net::Handler make_echo_handler(net::Reactor& r, int conn_fd) {
    return [&r, conn_fd](std::uint32_t) {
        std::array<char, 4096> buf; // 栈上缓冲,每次调用一份(不用 thread_local,避免生命周期坑)
        for (;;) {
            ssize_t n = ::read(conn_fd, buf.data(), buf.size());
            if (n > 0) {
                std::size_t off = 0;
                while (off < static_cast<std::size_t>(n)) {
                    ssize_t w =
                        ::write(conn_fd, buf.data() + off, static_cast<std::size_t>(n) - off);
                    if (w < 0) {
                        if (errno == EINTR)
                            continue;
                        break; // EAGAIN 等:简化处理,丢这段
                    }
                    off += static_cast<std::size_t>(w);
                }
            } else if (n == 0) {
                r.remove(conn_fd);
                ::close(conn_fd);
                return;
            } else {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    return; // 读空,等下次
                if (errno == EINTR)
                    continue;
                r.remove(conn_fd);
                ::close(conn_fd);
                return;
            }
        }
    };
}
} // namespace

// ============ MS1:事件循环 + 单连接 echo(以及多个顺序连接)============
TEST_CASE("MS1: reactor runs and echoes a connection") {
    constexpr std::uint16_t port = kPortBase + 1;
    net::Reactor r;
    int lfd = make_listener(port);
    r.add(lfd, EPOLLIN, [&]() {
        net::Handler accept_h;
        accept_h = [&](std::uint32_t) {
            for (;;) {
                int c = ::accept(lfd, nullptr, nullptr);
                if (c < 0) {
                    if (errno == EINTR)
                        continue;
                    break;
                }
                set_nonblock(c);
                r.add(c, EPOLLIN, make_echo_handler(r, c));
            }
        };
        return accept_h;
    }());

    std::thread loop([&] { r.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 单连接
    {
        net::UniqueFd c{connect_client(port)};
        const char* msg = "hello-ms1";
        ::write(c.get(), msg, std::strlen(msg));
        std::size_t got = read_with_timeout(c.get(), std::strlen(msg), 1000);
        REQUIRE(got == std::strlen(msg));
    }
    // 再来一个顺序连接
    {
        net::UniqueFd c{connect_client(port)};
        const char* msg = "second-ms1";
        ::write(c.get(), msg, std::strlen(msg));
        std::size_t got = read_with_timeout(c.get(), std::strlen(msg), 1000);
        REQUIRE(got == std::strlen(msg));
    }

    r.stop();
    loop.join();
    ::close(lfd);
}

// ============ MS2:N 个并发客户端全部正确 echo(TSan 下应无 race)============
TEST_CASE("MS2: concurrent clients all echo correctly") {
    constexpr std::uint16_t port = kPortBase + 2;
    constexpr int kN = 16;
    net::Reactor r;
    int lfd = make_listener(port);
    r.add(lfd, EPOLLIN, [&](std::uint32_t) {
        for (;;) {
            int c = ::accept(lfd, nullptr, nullptr);
            if (c < 0) {
                if (errno == EINTR)
                    continue;
                break;
            }
            set_nonblock(c);
            r.add(c, EPOLLIN, make_echo_handler(r, c));
        }
    });

    std::thread loop([&] { r.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::atomic<int> ok{0};
    std::vector<std::thread> clients;
    for (int i = 0; i < kN; ++i) {
        clients.emplace_back([&, i] {
            net::UniqueFd c{connect_client(port)};
            std::string msg = "client-" + std::to_string(i);
            ::write(c.get(), msg.data(), msg.size());
            if (read_with_timeout(c.get(), msg.size(), 1500) == msg.size())
                ok.fetch_add(1);
        });
    }
    for (auto& t : clients)
        t.join();
    REQUIRE(ok.load() == kN);

    r.stop();
    loop.join();
    ::close(lfd);
}

// ============ MS3(对抗性):ET 模式 + 大 burst 不丢数据 ============
// 连接注册成 EPOLLET | EPOLLIN,handler 必须循环读到 EAGAIN,否则一次只读 4KB、
// 剩下的卡在缓冲区里 ET 不再通知——就是 02 篇那个"丢 87KB"的坑。
TEST_CASE("MS3: ET mode echoes a large burst without data loss") {
    constexpr std::uint16_t port = kPortBase + 3;
    constexpr std::size_t kBurst = 100000;
    net::Reactor r;
    int lfd = make_listener(port);
    r.add(lfd, EPOLLIN, [&](std::uint32_t) {
        for (;;) {
            int c = ::accept(lfd, nullptr, nullptr);
            if (c < 0) {
                if (errno == EINTR)
                    continue;
                break;
            }
            set_nonblock(c);
            // ★关键:连接也用 EPOLLET,handler 必须循环读到 EAGAIN
            r.add(c, EPOLLIN | EPOLLET, make_echo_handler(r, c));
        }
    });

    std::thread loop([&] { r.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    net::UniqueFd c{connect_client(port)};
    std::string payload(kBurst, 'A');
    std::size_t sent = 0;
    while (sent < kBurst) { // 客户端把 100KB 全发出去
        ssize_t w = ::write(c.get(), payload.data() + sent, kBurst - sent);
        if (w <= 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        sent += static_cast<std::size_t>(w);
    }
    std::size_t got = read_with_timeout(c.get(), kBurst, 3000);
    REQUIRE(sent == kBurst);
    REQUIRE(got == kBurst); // ← 对抗性验收:一字节都不能少

    r.stop();
    loop.join();
    ::close(lfd);
}

// ============ MS4:优雅关闭——stop() 后 run() 必须返回,不挂死 ============
TEST_CASE("MS4: stop() unblocks run() without hanging") {
    constexpr std::uint16_t port = kPortBase + 4;
    net::Reactor r;
    int lfd = make_listener(port);
    r.add(lfd, EPOLLIN, [&](std::uint32_t) {
        for (;;) {
            int c = ::accept(lfd, nullptr, nullptr);
            if (c < 0) {
                if (errno == EINTR)
                    continue;
                break;
            }
            set_nonblock(c);
            r.add(c, EPOLLIN, make_echo_handler(r, c));
        }
    });

    // 在独立线程里跑 run(),用 future 做"带超时的 join"
    std::future<void> fut = std::async(std::launch::async, [&] { r.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 先连一个客户端制造"有活动"的状态,再 stop,模拟生产中"带载关闭"
    net::UniqueFd c{connect_client(port)};
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    r.stop(); // 从主线程请求停止
    auto status = fut.wait_for(std::chrono::seconds(2));
    REQUIRE(status == std::future_status::ready); // ← 验收:2s 内 run() 必须返回

    ::close(lfd);
}
