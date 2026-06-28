#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <unordered_map>

namespace net {

/// 事件处理器:某个 fd 就绪时被调用,传入就绪的事件位(EPOLLIN/EPOLLOUT/...)。
using Handler = std::function<void(std::uint32_t events)>;

/// 一个最小的 Reactor:epoll 事件循环 + fd→handler 注册表。
///
/// 所有 handler 都在 run() 的线程上同步执行(单线程 Reactor)——这是它"免锁"
/// 的关键:同一时刻只有一个 handler 在跑,共享状态不会被并发改。多线程安全靠
/// stop()(用 eventfd 唤醒阻塞中的 epoll_wait)。
///
/// 配套 documents/vol8-domains/networking/lab0-mini-reactor.md。
class Reactor {
  public:
    Reactor();
    ~Reactor();

    Reactor(const Reactor&) = delete;
    Reactor& operator=(const Reactor&) = delete;

    /// 注册 fd:关心 events,就绪时调 h。fd 必须是有效的、且尚未注册。
    void add(int fd, std::uint32_t events, Handler h);

    /// 修改已注册 fd 关心的事件(如 LT→ET、加 EPOLLOUT)。
    void modify(int fd, std::uint32_t events);

    /// 注销 fd(从兴趣表移除 + 删 handler)。
    void remove(int fd);

    /// 运行事件循环,阻塞直到 stop() 被调用。
    void run();

    /// 请求停止(可从别的线程或信号 handler 调用;eventfd 写唤醒 epoll_wait)。
    void stop();

  private:
    int ep_fd_{-1};
    int stop_fd_{-1}; // eventfd,用于唤醒
    std::atomic<bool> stop_{false};
    std::unordered_map<int, Handler> handlers_;
};

} // namespace net
