// Reactor 参考实现(答案版)。Lab 的任务是:读完 reactor.hpp 这个接口,
// 自己在 src/ 里重写一遍,让 tests/ 全绿。这个文件是给你对照的参考答案。
#include "net/reactor.hpp"

#include <array>
#include <cerrno>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

namespace net {

Reactor::Reactor() {
    ep_fd_ = ::epoll_create1(0);
    stop_fd_ = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    // 把 stop_fd_ 也挂进 epoll,这样 stop() 往它写一字节就能唤醒阻塞中的 epoll_wait
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = stop_fd_;
    ::epoll_ctl(ep_fd_, EPOLL_CTL_ADD, stop_fd_, &ev);
}

Reactor::~Reactor() {
    if (ep_fd_ >= 0)
        ::close(ep_fd_);
    if (stop_fd_ >= 0)
        ::close(stop_fd_);
}

void Reactor::add(int fd, std::uint32_t events, Handler h) {
    handlers_[fd] = std::move(h);
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    ::epoll_ctl(ep_fd_, EPOLL_CTL_ADD, fd, &ev);
}

void Reactor::modify(int fd, std::uint32_t events) {
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    ::epoll_ctl(ep_fd_, EPOLL_CTL_MOD, fd, &ev);
}

void Reactor::remove(int fd) {
    handlers_.erase(fd);
    ::epoll_ctl(ep_fd_, EPOLL_CTL_DEL, fd, nullptr);
}

void Reactor::stop() {
    stop_.store(true, std::memory_order_relaxed);
    std::uint64_t one = 1;
    ::write(stop_fd_, &one, sizeof(one)); // 唤醒 epoll_wait
}

void Reactor::run() {
    std::array<epoll_event, 64> events;
    while (!stop_.load(std::memory_order_relaxed)) {
        int n = ::epoll_wait(ep_fd_, events.data(), static_cast<int>(events.size()), -1);
        if (n < 0) {
            if (errno == EINTR)
                continue; // 被信号打断,重试
            break;        // 真出错,退出
        }
        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            if (fd == stop_fd_) {
                std::uint64_t v; // 清掉 eventfd 的计数
                ::read(stop_fd_, &v, sizeof(v));
                continue; // stop_ 会在 while 条件里被检查
            }
            auto it = handlers_.find(fd);
            if (it != handlers_.end()) {
                // 拷一份 handler 再调:handler 里若 remove 自己(EOF 时常见),
                // 会从 handlers_ 擦除正在执行的这个 std::function,直接调 it->second
                // 就是 use-after-free。先拷一份,本次调用就安全了。
                Handler h = it->second;
                h(events[i].events);
            }
        }
    }
}

} // namespace net
