---
chapter: 6
cpp_standard:
- 20
description: 理解 I/O 多路复用（epoll/io_uring）的工作原理，构建协程驱动的事件循环，打通异步 I/O 的最后一公里
difficulty: advanced
order: 4
platform: host
prerequisites:
- promise_type 与 awaitable
- CPU cache 与 OS 线程
reading_time_minutes: 25
related:
- 协程 Echo Server 实战
tags:
- host
- cpp-modern
- advanced
- coroutine
- 异步编程
title: 异步 I/O 与事件循环
---
# 异步 I/O 与事件循环

前面我们搞清楚了 C++20 协程的内部机制——`promise_type` 控制生命周期，awaiter/awaitable 控制挂起与恢复，调度器通过 `await_suspend` 拿到协程 handle 来管理执行时机。但说实话，到目前为止我们写的调度器只是一个"就绪队列"——它不知道什么叫"等数据到达"，不知道什么叫"等网络连接就绪"，更不知道什么叫"等定时器到期"。

协程本身不解决 I/O 问题——它只是一个控制流工具。真正让异步 I/O 变得高效的是操作系统提供的 I/O 多路复用机制。本篇要做的事情就是：把协程和操作系统的 I/O 多路复用机制接通，构建一个能处理真实网络 I/O 的事件循环。

## 环境说明

从这篇开始，我们正式进入 Linux 特定的领域。本篇所有涉及 I/O 多路复用的代码都依赖 Linux 的 epoll API，无法在 Windows 或 macOS 上直接编译运行。我们的测试环境是 Linux 2.6+（epoll 从 2.6 内核开始提供，如果你对 io_uring 感兴趣则需要 5.1+），编译器使用 GCC 13+ 或 Clang 17+，编译选项为 `-std=c++20`。需要提醒的是，epoll 是 Linux 特有的 API——macOS 上对应的是 kqueue，Windows 上对应的是 IOCP，思路是一致的，但 API 完全不同。我们会在后面简要提及其他平台的方案。

## 阻塞 I/O vs 非阻塞 I/O

在讲 I/O 多路复用之前，我们需要先从系统调用层面搞清楚"阻塞"和"非阻塞"到底是什么意思。

在 Unix/Linux 里，默认情况下所有的文件描述符（file descriptor，fd）都是阻塞模式的。当你对一个 TCP socket 调用 `read()` 时，如果接收缓冲区里没有数据，`read()` 会让当前线程**进入睡眠状态**，直到有数据到达（或者连接关闭、出错）。这种行为叫做"阻塞 I/O"（blocking I/O）。

阻塞 I/O 在单连接场景下没什么问题——你发一个请求，等响应，处理响应，循环往复。但当你需要同时处理几千个连接时，问题就来了：如果一个连接上没有数据到达，整个线程就被卡住了，其他连接全都在排队等待。一个线程只能处理一个阻塞的连接，那处理 10000 个连接就需要 10000 个线程——这显然不可持续。

> 笔者第一次写高并发网络服务的时候就掉进了这个坑——一个线程一个连接，连接数一上来，线程切换的开销比实际干活的开销还大，CPU 全在忙着做上下文切换。

解决方案的第一步是把 socket 设置为非阻塞模式：

```cpp
#include <fcntl.h>
#include <unistd.h>

void set_nonblocking(int fd)
{
    int kFlags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, kFlags | O_NONBLOCK);
}
```

在非阻塞模式下，`read()` 的行为完全不同：如果缓冲区里没有数据，`read()` 不会睡眠，而是立刻返回 `-1`，并设置 `errno` 为 `EAGAIN`（或者 `EWOULDBLOCK`——在 Linux 上它们是同一个值）。这告诉你"现在没有数据可读，稍后再试"。

这听起来很好，但接下来问题来了：你拿到 `EAGAIN` 之后怎么办？

最朴素的做法是轮询——写一个死循环不停地 `read()`，直到有数据为止。但这会让 CPU 100% 空转，什么有用的事情都没干，纯粹在浪费电。轮询是所有方案里最差的——它既浪费 CPU 资源，又不能保证及时响应（你可能在上一次 `read()` 返回 `EAGAIN` 之后 0.1 毫秒数据就到了，但你的循环可能因为调度问题等了好几毫秒才再次 `read()`）。

那有没有一种方式能让我们"在没有数据的时候去睡觉，有数据的时候被唤醒"？这就是 I/O 多路复用要做的事情。

## I/O 多路复用

I/O 多路复用（I/O multiplexing）的核心思想很简单：你把一堆 fd 交给操作系统，告诉它"我关心这些 fd 上的哪些事件（可读、可写、异常）"，然后你去睡觉。当其中任何一个 fd 上发生了你关心的事件时，操作系统把你唤醒，告诉你"这几个 fd 就绪了"。你去处理它们，然后再把 fd 交回去，继续睡觉。周而复始。

这样，一个线程就能高效地管理成千上万个连接——在没有事件的时候，线程安安静静地睡觉，不消耗 CPU；在事件到来时，线程被唤醒，处理就绪的连接。

### 从 select 到 poll 到 epoll

Linux 上 I/O 多路复用经历了三代演进：`select` → `poll` → `epoll`。

`select` 是最早的方案（POSIX 标准，所有 Unix 都支持）。它的接口大概是这样的：你传给它三个 fd_set（读、写、异常），每个 fd_set 是一个位数组，每一位代表一个 fd。`select` 最多能监视 1024 个 fd（`FD_SETSIZE` 宏定义），而且每次调用都需要把整个 fd_set 从用户态拷贝到内核态，返回时再拷回来——当 fd 数量很多时，这个拷贝开销非常大。更糟糕的是，返回后你不知道哪些 fd 就绪了，必须遍历整个 fd_set 去检查。

`poll` 改进了 `select` 的一些问题——它用 `pollfd` 结构体数组代替了位数组，不再有 1024 的 fd 数量限制。但核心问题没变：每次调用还是要把所有 fd 信息从用户态拷贝到内核态，返回后还是要遍历所有 fd。

真正的革命是 `epoll`（Linux 2.5.44 引入）。epoll 把"注册 fd"和"等待事件"分成了两步：你先用 `epoll_ctl` 把关心的 fd 注册到内核里（内核内部维护一个红黑树，增删改查都是 O(log n)），然后反复调用 `epoll_wait` 来等待事件。内核只返回那些**实际就绪**的 fd，不需要遍历。epoll 在 fd 数量大但活跃 fd 少的场景下（这正是高并发网络服务的典型场景）性能远超 select/poll。

### epoll 的三个核心 API

epoll 一共就三个系统调用，我们来逐一过一遍。

**`epoll_create1(flags)`** 创建一个 epoll 实例，返回一个 epoll fd。这个 fd 就是一个"监视器"——你后续把要监视的 socket fd 注册到这个 epoll fd 上。`flags` 通常传 `EPOLL_CLOEXEC`（在 exec 时自动关闭 epoll fd）。

```cpp
#include <sys/epoll.h>

int epfd = epoll_create1(EPOLL_CLOEXEC);
if (epfd < 0) {
    perror("epoll_create1");
    return -1;
}
```

**`epoll_ctl(epfd, op, fd, &event)`** 用来注册、修改或删除对某个 fd 的监视。`op` 可以是 `EPOLL_CTL_ADD`（添加）、`EPOLL_CTL_MOD`（修改）、`EPOLL_CTL_DEL`（删除）。`event` 是一个 `epoll_event` 结构体，包含你关心的事件类型和一个 `data` 字段（你可以往里面塞任何数据，epoll 不解释它，原封不动地还给你）。

```cpp
struct epoll_event ev;
ev.events = EPOLLIN;        // 关心"可读"事件
ev.data.fd = socket_fd;     // 把 socket fd 存在 data 里

// 把 socket_fd 注册到 epoll 实例上
epoll_ctl(epfd, EPOLL_CTL_ADD, socket_fd, &ev);
```

**`epoll_wait(epfd, events, max_events, timeout)`** 是真正干活的——它阻塞等待已注册的 fd 上发生事件，返回就绪的 fd 数量。`events` 是一个你提供的数组，epoll 把就绪的事件填进去。`timeout` 是超时时间（毫秒），`-1` 表示无限等待。

```cpp
struct epoll_event events[64];
int n = epoll_wait(epfd, events, 64, -1); // 阻塞等待
for (int i = 0; i < n; ++i) {
    int ready_fd = events[i].data.fd;
    // 处理 ready_fd 上的事件
}
```

这就是 epoll 的全部 API——三个调用，简洁而强大。

### LT vs ET：水平触发与边沿触发

epoll 有两种触发模式：Level Triggered（LT，水平触发，默认模式）和 Edge Triggered（ET，边沿触发，需要设置 `EPOLLET` 标志）。

"水平"和"边沿"这两个词来自电子学——水平触发指的是"只要电平为高就持续触发"，边沿触发指的是"只在电平从低变高的那个瞬间触发一次"。在 epoll 的语境下：

**LT 模式**：只要 fd 上有数据可读（或可写），`epoll_wait` 就会反复通知你。你没有把数据读完也没关系，下次 `epoll_wait` 还会告诉你"这个 fd 还能读"。LT 模式比较简单，不容易出错。

**ET 模式**：只在 fd 的状态发生变化时通知你一次——比如缓冲区从"空"变成"有数据"的那一刻。如果你没有把数据全部读完（读到 `EAGAIN` 为止），下次 `epoll_wait` 不会再通知你，直到又有新数据到达。ET 模式可以减少 `epoll_wait` 的返回次数（一次性处理完所有数据），但编码更复杂，而且**必须使用非阻塞 I/O**，否则可能在循环读的时候阻塞住。

> ⚠️ **ET 模式必须使用非阻塞 I/O。** 因为 ET 模式要求你一次性把数据读完（读到 `EAGAIN`），如果 socket 是阻塞的，最后一次 `read()` 会在没有数据的时候阻塞住，整个事件循环就卡死了。

对于大多数网络应用，LT 模式已经足够好，编程也更简单。ET 模式适合对性能要求极高的场景（比如 Nginx）。我们后面的示例都使用 LT 模式。

### 其他平台的方案

简要提一下其他操作系统的 I/O 多路复用方案，如果你需要在跨平台环境里工作的话。macOS 和 BSD 系列使用 kqueue，思路和 epoll 类似但 API 略有不同，macOS 上的 Nginx 和 Node.js 底层都是 kqueue。Windows 上则是 IOCP（I/O Completion Ports），它采用"完成"模型而不是"就绪"模型——你发起一个异步操作，操作系统在操作完成后通知你，这和 epoll 的"就绪通知"模型有本质区别。Linux 5.1+ 则引入了下一代异步 I/O 方案 io_uring，它使用共享内存环缓冲区（ring buffer）来提交和完成 I/O 操作，避免了传统系统调用的开销，性能比 epoll 更好，但 API 复杂度也更高，目前仍在快速演进中。

关于 io_uring，值得一提的是它和 epoll 的根本区别在于：epoll 是 reactor 模式（告诉你"准备好了，你自己去读写"），io_uring 更接近 proactor 模式（你提交读写请求，内核帮你完成后通过 completion ring 通知你"做完了"——不过 io_uring 也支持轮询模式，不完全等同于经典的 proactor）。io_uring 的性能在高并发场景下通常优于 epoll，因为它减少了系统调用次数——你可以把多个 I/O 操作打包提交到 ring buffer 里，内核批量处理，完成后再通过 completion ring 通知你。但 epoll 的生态更成熟，文档更丰富，大多数生产环境仍然在使用它。我们这里选择 epoll 作为教学载体，也是因为它的概念更直观、API 更简单。

## 事件循环模式

在讲代码之前，我们先搞清楚"事件循环"（Event Loop）到底是个什么模式。

事件循环的核心结构就是一个无限循环，每次迭代做三件事：先检查定时器看有没有到期的定时器需要处理，然后调用 `epoll_wait`（或其他 I/O 多路复用机制）阻塞等待就绪的 fd，最后对每个就绪的 fd 分发事件——调用对应的回调函数或者恢复对应的协程。伪代码大概是这个样子：

```cpp
while (运行中) {
    处理到期的定时器();
    n = epoll_wait(..., timeout = 最近定时器的剩余时间);
    for (i = 0; i < n; ++i) {
        处理 events[i] 上的 I/O 事件;
    }
}
```

这就是 Node.js、Nginx、Redis、Chrome、libuv 背后的核心模式。当然，实际的实现要复杂得多（需要处理信号、线程间通信、优雅关闭等），但骨架就是这个循环。

## 协程 + epoll 的衔接

现在我们有了协程（可以挂起和恢复的函数），有了 epoll（可以高效等待 I/O 事件的系统调用），问题是怎么把它们接起来。

关键洞察在上篇末尾已经提到了：**awaiter 的 `await_suspend` 是调度器集成的桥梁**。整个流程是这样的——当协程 `co_await` 一个 I/O 操作（比如 `async_read(socket, buffer)`）时，awaiter 的 `await_suspend` 被调用，它把协程的 `std::coroutine_handle` 存到某个地方，同时把 socket fd 注册到 epoll 上。然后 `await_suspend` 返回，协程挂起，控制权回到事件循环。事件循环调用 `epoll_wait` 阻塞等待 I/O 事件，当 socket 上有数据到达时 `epoll_wait` 返回，事件循环从 `epoll_event.data` 里取出协程 handle，调用 `handle.resume()` 恢复协程执行，此时 `await_resume()` 返回读取到的数据，协程从 `co_await` 表达式处继续往下走。

关键技巧在于：**把 `coroutine_handle` 存到 `epoll_event.data` 里**。`epoll_event.data` 是一个 `union`，可以存一个 `void*` 指针，也可以存一个 `int` fd。`coroutine_handle` 可以安全地转换为 `void*`（通过 `handle.address()`），也可以从 `void*` 转回来（通过 `std::coroutine_handle<>::from_address()`）。

现在我们来看具体的代码实现。

## 一个最小的事件循环实现

我们要实现一个能处理 TCP accept + read 的最小事件循环。整个实现大概 200 行代码，但涵盖了协程 + epoll 的所有核心概念。

### 第一步：事件循环骨架

先搭一个最基本的事件循环类，封装 epoll 的创建、注册、等待和分发。

```cpp
#include <coroutine>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cerrno>

/// 事件循环——封装 epoll 操作
class EventLoop {
public:
    EventLoop()
        : kEpollFd(epoll_create1(EPOLL_CLOEXEC))
    {
        if (kEpollFd < 0) {
            perror("epoll_create1");
            std::abort();
        }
    }

    ~EventLoop() { close(kEpollFd); }

    /// 注册 fd 到 epoll，关联一个协程 handle
    void add_reader(int fd, uint32_t events,
                    std::coroutine_handle<> handle)
    {
        struct epoll_event ev;
        ev.events = events;
        // 关键：把 coroutine_handle 存到 epoll_event.data 里
        ev.data.ptr = handle.address();
        if (epoll_ctl(kEpollFd, EPOLL_CTL_ADD, fd, &ev) < 0) {
            // fd 可能已经注册过了（比如 accept 循环重复使用同一个 listen_fd），
            // 改用 MOD 更新关联的 handle 和事件
            epoll_ctl(kEpollFd, EPOLL_CTL_MOD, fd, &ev);
        }
    }

    /// 从 epoll 移除 fd
    void remove(int fd)
    {
        epoll_ctl(kEpollFd, EPOLL_CTL_DEL, fd, nullptr);
    }

    /// 运行事件循环
    void run()
    {
        struct epoll_event events[64];
        std::puts("=== 事件循环启动 ===");

        while (kRunning) {
            // 等待 I/O 事件，超时 1 秒
            int n = epoll_wait(kEpollFd, events, 64, 1000);
            if (n < 0) {
                if (errno == EINTR) {
                    continue; // 被信号中断，重试
                }
                perror("epoll_wait");
                break;
            }

            for (int i = 0; i < n; ++i) {
                // 从 epoll_event.data 恢复 coroutine_handle
                auto handle = std::coroutine_handle<>::from_address(
                    events[i].data.ptr
                );
                if (handle && !handle.done()) {
                    handle.resume(); // 恢复协程
                }
            }
        }

        std::puts("=== 事件循环结束 ===");
    }

    void stop() { kRunning = false; }

private:
    int kEpollFd;
    bool kRunning = true;
};
```

你会发现，`add_reader` 方法做的就是把 `coroutine_handle` 的地址存到 `epoll_event.data.ptr` 里。这是整个设计最核心的一步——它让 epoll 事件和协程之间建立了一对一的映射关系。当 `epoll_wait` 返回一个事件时，我们可以直接从 `data.ptr` 恢复出对应的协程 handle，然后 `resume()` 它。

### 第二步：协程任务类型

接下来定义一个协程任务类型，它的 `promise_type` 配合我们的事件循环。

```cpp
/// 异步 I/O 任务类型
struct IoTask {
    struct promise_type {
        IoTask get_return_object()
        {
            return IoTask{
                std::coroutine_handle<promise_type>::from_promise(*this)
            };
        }

        std::suspend_always initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { std::terminate(); }
    };

    std::coroutine_handle<promise_type> handle;
};
```

### 第三步：异步 accept

当一个客户端连接到来时，我们需要 accept 它。在协程化的世界里，accept 变成了一个 `co_await async_accept(listen_fd)` 的操作——如果暂时没有连接，协程挂起，等到 epoll 通知 listen_fd 可读时再恢复。

```cpp
/// 全局事件循环实例
EventLoop g_event_loop;

/// 设置 socket 为非阻塞
void set_nonblocking(int fd)
{
    int kFlags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, kFlags | O_NONBLOCK);
}

/// 异步 accept 的 awaiter
struct AsyncAcceptAwaiter {
    int kListenFd;

    explicit AsyncAcceptAwaiter(int listen_fd)
        : kListenFd(listen_fd) {}

    bool await_ready() noexcept
    {
        // 先尝试非阻塞 accept，看是否已经有等待的连接
        return false; // 简化处理，总是挂起
    }

    void await_suspend(std::coroutine_handle<> handle)
    {
        // 把 listen_fd 注册到 epoll，监听可读事件（新连接到达）
        // 把协程 handle 存到 epoll_event.data 里
        g_event_loop.add_reader(
            kListenFd,
            EPOLLIN,
            handle
        );
    }

    int await_resume()
    {
        // 协程恢复时，执行 accept 拿到新连接
        struct sockaddr_in client_addr {};
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = ::accept(
            kListenFd,
            reinterpret_cast<struct sockaddr*>(&client_addr),
            &addr_len
        );
        if (client_fd >= 0) {
            set_nonblocking(client_fd);
        }
        return client_fd;
    }
};

/// 协程化的 accept 函数
AsyncAcceptAwaiter async_accept(int listen_fd)
{
    return AsyncAcceptAwaiter(listen_fd);
}
```

这里有一个精妙之处：`await_suspend` 里我们注册了 epoll 事件，但此时还没有调用 `accept`——因为还没有新连接。等到 epoll 通知 listen_fd 可读时（意味着有新连接到达），事件循环恢复协程，`await_resume` 才执行真正的 `accept`。这比传统回调式代码清晰得多。

### 第四步：异步 read

read 的模式和 accept 几乎一样——先注册到 epoll，等数据到达后再真正执行 `read`。

```cpp
/// 异步 read 的 awaiter
struct AsyncReadAwaiter {
    int kFd;
    void* kBuffer;
    std::size_t kSize;
    ssize_t kResult; // 读取结果
    bool kSuspended; // 是否经历过挂起

    AsyncReadAwaiter(int fd, void* buffer, std::size_t size)
        : kFd(fd), kBuffer(buffer), kSize(size), kResult(0),
          kSuspended(false) {}

    bool await_ready() noexcept
    {
        // 先尝试非阻塞 read
        kResult = ::read(kFd, kBuffer, kSize);
        if (kResult >= 0) {
            return true; // 读到了数据，不需要挂起
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return false; // 暂时没数据，需要挂起等 epoll 通知
        }
        return true; // 出错了，不挂起，让 await_resume 处理
    }

    void await_suspend(std::coroutine_handle<> handle)
    {
        kSuspended = true;
        // 注册到 epoll，等待 fd 可读
        g_event_loop.add_reader(kFd, EPOLLIN, handle);
    }

    ssize_t await_resume()
    {
        if (kSuspended) {
            // 挂起后恢复，epoll 通知 fd 可读，执行真正的 read
            kResult = ::read(kFd, kBuffer, kSize);
        }
        return kResult;
    }
};

/// 协程化的 read 函数
AsyncReadAwaiter async_read(int fd, void* buffer, std::size_t size)
{
    return AsyncReadAwaiter(fd, buffer, size);
}
```

你会发现，`await_ready` 里我们先尝试了一次非阻塞 `read`。如果数据已经到了，就直接返回，省去了注册 epoll 和挂起/恢复的开销。这就是 `await_ready` 作为"快速路径优化"的价值——在大多数情况下，如果你能提前判断操作是否已完成，就应该在 `await_ready` 里做。

### 第五步：把它们组装起来

现在我们有了一个完整的事件循环、协程任务类型、异步 accept 和异步 read。接下来把它们组装成一个可以接受 TCP 连接并读取数据的程序。

```cpp
/// 创建监听 socket
int create_listen_socket(uint16_t port)
{
    int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return -1;
    }

    // 设置 SO_REUSEADDR，允许端口复用
    int kOpt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR,
               &kOpt, sizeof(kOpt));

    set_nonblocking(listen_fd);

    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (::bind(listen_fd,
               reinterpret_cast<struct sockaddr*>(&addr),
               sizeof(addr)) < 0) {
        perror("bind");
        close(listen_fd);
        return -1;
    }

    if (::listen(listen_fd, 128) < 0) {
        perror("listen");
        close(listen_fd);
        return -1;
    }

    return listen_fd;
}

/// 处理单个客户端连接的协程
IoTask handle_client(int client_fd)
{
    char buffer[1024];
    std::printf("[协程] 新连接 fd=%d\n", client_fd);

    while (true) {
        // 异步读取数据（保留 1 字节给 '\0' 终止符）
        auto n = co_await async_read(client_fd, buffer, sizeof(buffer) - 1);

        if (n <= 0) {
            if (n == 0) {
                std::printf("[协程] 客户端关闭连接 fd=%d\n", client_fd);
            } else {
                std::printf("[协程] 读取错误 fd=%d\n", client_fd);
            }
            close(client_fd);
            co_return;
        }

        // 简单回显：把读到的数据打印出来
        buffer[n] = '\0';
        std::printf("[协程] 收到数据 fd=%d: %s", client_fd, buffer);

        // 注意：这里也应该用 async_write，但为了简洁先用同步 write
        // 在 LT 模式下同步 write 对于小数据量通常是没问题的
        ::write(client_fd, buffer, n);
    }
}

/// 接受新连接的协程
IoTask accept_loop(int listen_fd)
{
    std::printf("[协程] 开始监听，等待连接...\n");

    while (true) {
        // 异步 accept——没有新连接时协程挂起
        int client_fd = co_await async_accept(listen_fd);

        if (client_fd < 0) {
            std::printf("[协程] accept 失败\n");
            continue;
        }

        std::printf("[协程] 接受新连接 fd=%d\n", client_fd);

        // 启动一个新的协程来处理这个连接
        // 注意：这里创建的协程需要手动管理生命周期
        auto task = handle_client(client_fd);
        // 立即启动 handle_client 协程
        task.handle.resume();
    }
}

int main()
{
    uint16_t kPort = 8080;

    int listen_fd = create_listen_socket(kPort);
    if (listen_fd < 0) {
        return 1;
    }

    std::printf("服务器启动，监听端口 %d\n", kPort);

    // 创建 accept 循环协程
    auto acceptor = accept_loop(listen_fd);
    // 手动启动（因为 initial_suspend 返回 suspend_always）
    acceptor.handle.resume();

    // 运行事件循环
    g_event_loop.run();

    // 清理
    close(listen_fd);
    return 0;
}
```

这个程序虽然还有不少粗糙的地方（比如 handle_client 协程的生命周期管理、没有实现 async_write 等），但它已经是一个能工作的协程化 TCP 服务器了。让我们回顾一下整个流程：`main()` 创建监听 socket，启动 accept 循环协程，进入事件循环。accept 循环协程执行到 `co_await async_accept(listen_fd)` 时，此时没有新连接，协程挂起，listen_fd 注册到 epoll。事件循环在 `epoll_wait` 上阻塞等待，当客户端连接到来时 epoll 通知 listen_fd 可读，事件循环恢复 accept 协程。accept 协程拿到 client_fd 后启动 handle_client 协程来处理这个连接，然后回到 `co_await async_accept` 继续等待下一个连接。handle_client 协程执行到 `co_await async_read(client_fd, ...)` 时，client_fd 注册到 epoll，协程挂起。数据到达后 epoll 通知 client_fd 可读，事件循环恢复 handle_client 协程，它读取数据、回显，然后回到 `co_await async_read` 继续等待下一批数据。整个过程里，一个线程就管理了所有连接——在没有 I/O 事件的时候线程安安静静地在 `epoll_wait` 上睡眠，有事件时才被唤醒处理。

> ⚠️ **这段代码里有一个生命周期管理的坑。** `handle_client` 返回的 `IoTask` 对象在每次循环迭代结束时就被销毁了，但 `IoTask` 的析构函数什么也不做——`coroutine_handle` 是非拥有句柄，它的析构不会销毁协程帧。这意味着协程帧永远不会被释放（内存泄漏）。由于 `final_suspend` 返回 `suspend_always`，协程完成后帧仍然驻留在堆上，没有人调用 `handle.destroy()`。在生产代码中，你需要一个更完善的任务管理系统来跟踪所有活跃的协程——比如把所有活跃的协程 handle 存到一个容器里，在协程结束时调用 `handle.destroy()` 释放帧并从容器中移除。我们在下一篇的 Echo Server 里会处理这个问题。

### 事件循环里的一个微妙问题

你可能已经注意到了，上面的事件循环有一个问题：`epoll_wait` 返回后，我们恢复协程，但协程在 `await_resume` 里可能会再次调用 `epoll_ctl` 注册新的事件。这意味着在恢复一个协程的过程中，epoll 的兴趣列表可能会被修改——这通常是安全的，因为 `epoll_ctl` 的修改在下一次 `epoll_wait` 时才生效。但如果你在恢复协程的循环中修改了同一个 fd 的事件（比如先注册了 `EPOLLIN`，协程恢复后又改成 `EPOLLOUT`），就需要小心顺序问题了。

在 LT 模式下这通常不是问题，因为 LT 模式是"水平触发"的——只要你还有数据没读完，下次 `epoll_wait` 还会通知你。但在 ET 模式下，如果你在处理事件的过程中修改了 fd 的注册，可能会丢失事件通知。

### await_ready 的快速路径价值

回头看我们的 `AsyncReadAwaiter`，`await_ready` 里先做了一次非阻塞 `read`。这个设计不是多余的——在很多场景下，数据可能已经到达了（TCP 接收缓冲区里已经有数据），此时不需要挂起协程、注册 epoll、等待通知、恢复协程这一整套流程，直接读就行了。这个快速路径在高性能场景下非常重要，因为省掉了至少一次系统调用（`epoll_ctl`）和两次协程上下文切换。

## 跨平台考量

我们上面所有代码都是基于 Linux epoll 的。如果你需要跨平台支持，有两种常见策略：

第一种是抽象出一个统一的 `IoMultiplexer` 接口，在不同平台上用不同的实现——Linux 上用 epoll，macOS 上用 kqueue，Windows 上用 IOCP。这就是 libuv（Node.js 底层库）和 Boost.Asio 采用的方案。

第二种是使用更高层的抽象——比如 Boost.Asio 的 `io_context`，它已经帮你封装好了平台差异。Asio 从 1.13.0 开始提供 `awaitable<T>`、`use_awaitable` 和 `co_spawn()` 等 C++20 协程支持（1.17.0 起支持 GCC 10 的标准协程实现），你可以用 `co_await` 配合 Asio 的异步操作来写跨平台的异步代码。

对于学习目的，epoll 已经足够让我们理解 I/O 多路复用的核心概念。掌握了 epoll + 协程的模式后，切换到 kqueue 或 IOCP 只是 API 替换的问题。

## 我们的位置

这篇我们打通了协程和操作系统的 I/O 多路复用之间的桥梁。从阻塞 I/O 的问题出发，我们看到了非阻塞 I/O + 轮询为什么不可行，然后介绍了 I/O 多路复用（select → poll → epoll 的演进），重点讲解了 epoll 的三个 API 和 LT/ET 两种触发模式。接着我们把协程和 epoll 接通——通过把 `coroutine_handle` 存到 `epoll_event.data` 里，实现了"epoll 事件通知 → 恢复对应协程"的闭环。最后我们用这些组件搭建了一个能接受 TCP 连接并读取数据的最小事件循环。

但这个事件循环还远不是一个完整的服务器——它缺少优雅的协程生命周期管理、异步 write、错误处理、定时器支持，以及最重要的：一个完整的 Echo Server。下一篇我们要做的事情就是把这些拼图全部拼起来，实现一个功能完整的协程化 Echo Server，让你看到一个"真正能用"的协程网络服务是什么样子的。

> 💡 完整示例代码在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)，访问 `code/volumn_codes/vol5/ch06-async-io-coroutine/`。

## 参考资源

- [epoll(7) — Linux man page](https://man7.org/linux/man-pages/man7/epoll.7.html) — epoll 的官方文档，包含 LT/ET 模式的详细说明
- [The C10K problem — Dan Kegel](http://www.kegel.com/c10k.html) — 经典的"I/O 多路复用"问题分析文章，讨论了各种 I/O 模型的优缺点
- [Blocking I/O, Nonblocking I/O, And Epoll — Eli Klitzke](https://eklitzke.org/blocking-io-nonblocking-io-and-epoll) — 从阻塞 I/O 到非阻塞 I/O 再到 epoll 的完整讲解
- [Coroutines (C++20) — cppreference](https://en.cppreference.com/cpp/language/coroutines) — C++20 协程的语言规范
- [From epoll to io_uring's Multishot Receives](https://codemia.io/blog/path/From-epoll-to-iourings-Multishot-Receives--Why-2025-Is-the-Year-We-Finally-Kill-the-Event-Loop) — 讨论从 epoll 到 io_uring 的演进，以及 2025 年事件循环模型的未来
- [io_uring vs epoll — kernel-internals.org](https://kernel-internals.org/io-uring/io-uring-vs-epoll/) — epoll 和 io_uring 的特性对比
- [C++20 Coroutines: Sketching a Minimal Async Framework — Jeremy Ong](https://jeremyong.com/cpp/2021/01/04/cpp20-coroutines-a-minimal-async-framework/) — 从零搭建协程异步框架的实战参考
