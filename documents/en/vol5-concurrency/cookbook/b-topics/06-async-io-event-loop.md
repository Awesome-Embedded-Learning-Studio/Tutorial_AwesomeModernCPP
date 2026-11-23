---
title: "Asynchronous I/O and Event Loops"
description: "Starting from blocking versus non-blocking I/O, walking the evolution from select and poll to epoll, understanding LT and ET trigger modes, then storing a coroutine_handle inside epoll_event to bridge coroutines and the OS's I/O multiplexing."
chapter: 5
order: 6
tags:
  - host
  - cpp-modern
  - advanced
  - 异步编程
  - coroutine
difficulty: advanced
platform: host
cpp_standard: [20]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
translation:
  source: documents/vol5-concurrency/cookbook/b-topics/06-async-io-event-loop.md
  source_hash: ccf63da95692862bf6d19a79b5c0b2920da32ab825e9fdcbd214d3434d061679
  translated_at: '2026-11-22T13:29:27+00:00'
  engine: glm-5.3
  token_count: 20132
---
# Asynchronous I/O and Event Loops

This piece lives in the readings section, for this reason: a complete coroutine runtime does not enter the required line — Unit 7 of Volume 5 teaches only the coroutine language machinery and minimal task organization, from [Why Suspend and Resume Are Needed](../../chapter07-coroutines/01-why-suspend-resume) to [Organizing Coroutines into Completable Tasks](../../chapter07-coroutines/04-task-type). Event loops, epoll, and real network I/O — that whole set — is application-layer construction beyond the requirement, and the whole piece migrated into the readings section. It is also the foundation of the next piece, [Coroutine Echo Server](07-coroutine-echo-server); read the two back to back and you have one complete line.

Before reading it, you need all of Unit 7: how `promise_type` controls the lifecycle ([How a Minimal Coroutine Comes Together](../../chapter07-coroutines/02-minimal-coroutine)), how the awaiter's three member functions take part in `co_await` ([How co_await Takes Part in Waiting](../../chapter07-coroutines/03-co-await-and-awaiters)), plus the suspension and resumption of the teaching Task type. Network system calls (socket, fcntl) are not prerequisites; this piece explains them on the spot as they appear.

The migration trail: mechanism explanations, code, and verification claims are preserved as they were; what I changed is limited to numbering, frontmatter, and the cross-references pointing at the main text, plus a banned-words-list cleanup. The material's claimed test environment (Linux, GCC 13+ / Clang 17+, `-std=c++20`) remains in the environment note as-is; I did not recompile and re-verify this round.

When Unit 7's main text wrapped up, we already held a working scheduler. But our scheduler is only a "ready queue" — it knows nothing of "waiting for data to arrive", nothing of "waiting for a network connection to be ready", and still less of "waiting for a timer to expire".

Coroutines themselves solve no I/O problem; they are merely a control-flow tool. What truly makes asynchronous I/O efficient is the I/O multiplexing the operating system provides. What this piece does: connect coroutines to the OS's I/O multiplexing, and together we build an event loop that handles real network I/O.

## Environment Note

From here we formally enter Linux-specific territory. All the I/O-multiplexing code in this piece depends on Linux's epoll API and will not compile or run directly on Windows or macOS. The material's claimed test environment is Linux 2.6+ (epoll arrived with the 2.6 kernel; 5.1+ if io_uring interests you), with GCC 13+ or Clang 17+ and `-std=c++20`. A reminder: epoll is Linux-specific; the macOS counterpart is kqueue, the Windows one IOCP — the ideas align, but the APIs differ completely. We will touch on other platforms' schemes briefly later.

## Blocking I/O vs Non-Blocking I/O

Before discussing I/O multiplexing, we need to get clear at the system-call level what "blocking" and "non-blocking" actually mean.

In Unix/Linux, every file descriptor (fd) is in blocking mode by default. When you call `read()` on a TCP socket and the receive buffer holds no data, `read()` puts the current thread **to sleep** until data arrives (or the connection closes, or an error occurs). That is "blocking I/O".

Blocking I/O is fine in single-connection scenarios — send a request, await the response, process it, repeat. But when thousands of connections must be handled simultaneously, trouble arrives: no data on one connection and the whole thread is stuck, every other connection queuing behind it. One thread serves one blocking connection, so 10000 connections need 10000 threads — clearly unsustainable.

> The first time I wrote a high-concurrency network service I fell right here — one thread per connection; once the connection count climbed, thread-switching overhead exceeded the actual work, with the CPU fully busy doing context switches.

The first move of the solution: put the socket into non-blocking mode.

```cpp
#include <fcntl.h>
#include <unistd.h>

void set_nonblocking(int fd)
{
    int kFlags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, kFlags | O_NONBLOCK);
}
```

In non-blocking mode `read()` behaves completely differently: with no data in the buffer, it does not sleep but returns `-1` immediately, setting `errno` to `EAGAIN` (or `EWOULDBLOCK` — the same value on Linux). It tells you "nothing to read right now, try again later".

Sounds good — but the next question follows: what do you do after receiving `EAGAIN`?

The plainest approach is polling — a busy loop calling `read()` until data shows up. But that spins the CPU at 100% doing nothing useful, pure wasted electricity. Polling is the worst of all schemes — it wastes CPU and still cannot guarantee timely response (data may arrive 0.1 ms after your last `read()` returned `EAGAIN`, yet your loop may not call `read()` again for several milliseconds thanks to scheduling).

So is there a way for us to "sleep when there is no data and be woken when data arrives"? That is exactly what I/O multiplexing does.

## I/O Multiplexing

The core idea of I/O multiplexing is simple: hand a batch of fds to the operating system, tell it "these are the events I care about on these fds (readable, writable, exceptional)", then go to sleep. When any fd sees an event you care about, the OS wakes you and says "these fds are ready". You handle them, hand the fds back, and sleep again. On and on.

This way one thread efficiently manages tens of thousands of connections. With no events it sleeps quietly, consuming no CPU. When events arrive it wakes and serves the ready connections.

### From select to poll to epoll

I/O multiplexing on Linux went through three generations; let us meet them in order: `select`, then `poll`, then `epoll`.

`select` is the oldest scheme (a POSIX standard, supported by every Unix). Its interface runs roughly like this: you pass three fd_sets (read, write, exceptional), each a bit array with one bit per fd. `select` watches at most 1024 fds (the `FD_SETSIZE` macro), and every call must copy the entire fd_set from user space to kernel space and back on return — a heavy copying cost once fds grow numerous. Worse, after returning you do not know which fds are ready; you must walk the whole fd_set to find out.

`poll` fixed some of `select`'s problems, replacing the bit array with an array of `pollfd` structs and dropping the 1024-fd cap. But let us see clearly: the core problem stands — every call still copies all fd information from user space to kernel space, and after returning you still walk every fd.

The real revolution is `epoll` (introduced in Linux 2.5.44). epoll splits "registering fds" from "waiting for events" into two steps: you register the fds you care about into the kernel with `epoll_ctl` (the kernel maintains an internal red-black tree; insert, remove, modify, and lookup are all O(log n)), then call `epoll_wait` repeatedly to wait for events. The kernel returns only the fds that are **actually ready** — no walking required. epoll far outperforms select/poll when fd counts are large but active fds are few — and, as you should remember, that is precisely the typical high-concurrency network-service scenario.

### epoll's Three Core APIs

epoll has exactly three system calls; let us take them one by one.

**`epoll_create1(flags)`** creates an epoll instance and returns an epoll fd. That fd is a "watcher" onto which you later register the socket fds to monitor. `flags` is usually `EPOLL_CLOEXEC` (close the epoll fd automatically on exec).

```cpp
#include <sys/epoll.h>

int epfd = epoll_create1(EPOLL_CLOEXEC);
if (epfd < 0) {
    perror("epoll_create1");
    return -1;
}
```

**`epoll_ctl(epfd, op, fd, &event)`** registers, modifies, or removes the watch on some fd. `op` is one of `EPOLL_CTL_ADD` (add), `EPOLL_CTL_MOD` (modify), `EPOLL_CTL_DEL` (remove). `event` is an `epoll_event` struct holding the event types you care about and a `data` field (you can stuff anything in there; epoll does not interpret it and hands it back untouched).

```cpp
struct epoll_event ev;
ev.events = EPOLLIN;        // 关心"可读"事件
ev.data.fd = socket_fd;     // 把 socket fd 存在 data 里

// 把 socket_fd 注册到 epoll 实例上
epoll_ctl(epfd, EPOLL_CTL_ADD, socket_fd, &ev);
```

**`epoll_wait(epfd, events, max_events, timeout)`** does the real work — it blocks waiting for events on the registered fds and returns how many are ready. `events` is an array you provide, which epoll fills with the ready events. `timeout` is the timeout in milliseconds; `-1` means wait indefinitely.

```cpp
struct epoll_event events[64];
int n = epoll_wait(epfd, events, 64, -1); // 阻塞等待
for (int i = 0; i < n; ++i) {
    int ready_fd = events[i].data.fd;
    // 处理 ready_fd 上的事件
}
```

That is epoll's entire API — three calls, concise and powerful; our toolkit is now fully acquainted.

### LT vs ET: Level-Triggered and Edge-Triggered

epoll has two trigger modes; know them separately: Level Triggered (LT, the default) and Edge Triggered (ET, requiring the `EPOLLET` flag).

The words "level" and "edge" come from electronics; let us carry them into the epoll context: level-triggered means "fires continuously as long as the level stays high", edge-triggered means "fires once, at the instant the level goes from low to high".

**LT mode**: as long as an fd has data to read (or room to write), `epoll_wait` keeps notifying you. Failing to drain the data is fine — the next `epoll_wait` will again say "this fd can still be read". LT mode is simpler and harder to get wrong.

**ET mode**: notifies you exactly once when the fd's state changes — say the moment the buffer goes from "empty" to "has data". If you do not drain everything (read until `EAGAIN`), the next `epoll_wait` will not notify you again until new data arrives. ET mode can cut the number of `epoll_wait` returns (handle everything in one pass) but is more complex to code and **requires non-blocking I/O**, otherwise you may block mid read-loop.

> **ET mode requires non-blocking I/O.** ET demands that you drain the data in one go (until `EAGAIN`); with a blocking socket, the final `read()` blocks when no data remains, and the whole event loop is stuck.

For most network applications LT mode is good enough and simpler to program. ET mode fits scenarios with extreme performance demands (Nginx, for one). Our examples below all use LT mode.

### Schemes on Other Platforms

A brief note on other operating systems' I/O multiplexing, if you work cross-platform. macOS and the BSD family use kqueue — similar in idea to epoll, slightly different in API; Nginx and Node.js on macOS sit on kqueue underneath. Windows has IOCP (I/O Completion Ports), which follows a "completion" model rather than a "readiness" model — you initiate an asynchronous operation and the OS notifies you when it completes, an essential difference from epoll's readiness-notification model. Linux 5.1+ introduced the next-generation asynchronous I/O scheme, io_uring, which uses shared-memory ring buffers to submit and complete I/O operations, avoiding traditional syscall overhead, outperforming epoll — but with higher API complexity, and still evolving fast.

About io_uring, let us point at its fundamental difference from epoll: epoll is the reactor pattern — it tells you "ready; go read and write yourself". io_uring leans closer to the proactor pattern — you submit read/write requests, the kernel completes them and notifies you "done" through the completion ring (though io_uring also supports a polling mode, not a perfect match for the classic proactor). io_uring's performance usually beats epoll under high concurrency because it cuts syscall count — you can pack several I/O operations into the ring buffer for submission, the kernel processes them in batch, then notifies you through the completion ring. But epoll's ecosystem is more mature with richer documentation, and most production environments still use it. We pick epoll as the teaching vehicle here precisely because its concepts are more intuitive and its API simpler.

## The Event Loop Pattern

Before the code, let us get clear on what pattern the "event loop" actually is.

An event loop's core structure is one infinite loop, with each iteration's work split into three parts: check the timers for any that have expired and need handling; then call `epoll_wait` (or another I/O multiplexing mechanism) to block waiting for ready fds; finally dispatch events for each ready fd, invoking the corresponding callback or resuming the corresponding coroutine. The pseudocode looks roughly like this:

```cpp
while (运行中) {
    处理到期的定时器();
    n = epoll_wait(..., timeout = 最近定时器的剩余时间);
    for (i = 0; i < n; ++i) {
        处理 events[i] 上的 I/O 事件;
    }
}
```

This is the core pattern behind Node.js, Nginx, Redis, Chrome, and libuv. Real implementations are of course far more complex (signals, inter-thread communication, graceful shutdown, and so on), but the skeleton you see is this loop.

## Bridging Coroutines and epoll

We now have coroutines (functions that can suspend and resume) and epoll (system calls that wait for I/O events efficiently); the question is how to wire them together.

The key insight was already laid out in Unit 7's awaiter discussion — in [How co_await Takes Part in Waiting](../../chapter07-coroutines/03-co-await-and-awaiters). It is this: **the awaiter's `await_suspend` is the bridge for scheduler integration**. Walk the whole flow: when a coroutine `co_await`s an I/O operation (say `async_read(socket, buffer)`), the awaiter's `await_suspend` is invoked; it stores the coroutine's `std::coroutine_handle` somewhere and registers the socket fd with epoll. Then `await_suspend` returns, the coroutine suspends, and control returns to the event loop. The loop calls `epoll_wait` and blocks; when data arrives on the socket, `epoll_wait` returns, the loop retrieves the coroutine handle from `epoll_event.data`, calls `handle.resume()` to resume execution — at which point `await_resume()` returns the read data and the coroutine continues from the `co_await` expression.

The key trick is storing the `coroutine_handle` inside `epoll_event.data`; let us see it land. `epoll_event.data` is a `union` that can hold a `void*` pointer or an `int` fd. A `coroutine_handle` converts safely to `void*` (via `handle.address()`) and converts back (via `std::coroutine_handle<>::from_address()`).

Now for the concrete implementation.

## A Minimal Event Loop Implementation

What we build is a minimal event loop that handles TCP accept and read. The whole implementation is about 200 lines, yet every core concept of coroutines + epoll lives inside.

### The Event Loop Skeleton

Let us erect a basic event-loop class wrapping epoll's creation, registration, waiting, and dispatch.

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

You will notice that all `add_reader` does is store the `coroutine_handle`'s address into `epoll_event.data.ptr`. This is the most central step of the whole design: it establishes a one-to-one mapping between epoll events and coroutines. When `epoll_wait` returns an event, we can recover the corresponding coroutine handle straight from `data.ptr` and `resume()` it.

### The Coroutine Task Type

Next we define a coroutine task type whose `promise_type` cooperates with the event loop.

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

### Asynchronous accept

When a client connection arrives, we must accept it. In the coroutine world, accept becomes a `co_await async_accept(listen_fd)` operation — no connection for now, the coroutine suspends, resuming when epoll reports the listen_fd readable.

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
        // 理想做法是试一次非阻塞 accept，看是否已经有等待的连接；
        // 这里简化处理，总是挂起
        return false;
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

Here is the elegance: in `await_suspend` we register the epoll event but do not yet call `accept` — no new connection exists yet. When epoll reports the listen_fd readable (meaning a new connection has arrived), the event loop resumes the coroutine, and only then does `await_resume` perform the real `accept`. Far clearer than traditional callback-style code.

### Asynchronous read

read follows almost the same pattern as accept; same prescription: register with epoll, and perform the real `read` once data has arrived.

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
        // 快速路径：上来就试一次非阻塞 read
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

You will notice that in `await_ready` we sneak in one non-blocking `read` before suspending. If the data is already there, we return directly, saving the epoll registration and the suspend/resume overhead. That is `await_ready`'s value as a "fast-path optimization" — in most cases, if you can determine up front that the operation has already completed, do it in `await_ready`.

### Assembling the Pieces

We now have a complete event loop, a coroutine task type, asynchronous accept, and asynchronous read; next, assemble them into a program that accepts TCP connections and reads data.

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

        // 注意：这里也应该用 async_write，但为了简洁暂时用同步 write
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

Rough edges remain (the handle_client coroutine's lifetime management, the missing async_write, and so on), but this is already a working coroutine-based TCP server. Let us review the whole flow: `main()` creates the listening socket, starts the accept-loop coroutine, and enters the event loop. When the accept-loop coroutine reaches `co_await async_accept(listen_fd)` with no new connection pending, it suspends and the listen_fd is registered with epoll. The event loop blocks on `epoll_wait`; when a client connects, epoll reports the listen_fd readable and the loop resumes the accept coroutine. Having obtained client_fd, the accept coroutine starts a handle_client coroutine for the connection, then returns to `co_await async_accept` to await the next one. When handle_client reaches `co_await async_read(client_fd, ...)`, the client_fd is registered with epoll and the coroutine suspends. Data arrives, epoll reports the client_fd readable, the loop resumes handle_client, which reads the data, echoes it, and returns to `co_await async_read` for the next batch. Throughout, a single thread manages every connection — quietly sleeping on `epoll_wait` when no I/O events exist, woken only when there is something to handle.

> **There is a lifetime-management problem in this code.** The `IoTask` object returned by `handle_client` is destroyed at the end of each loop iteration, but `IoTask`'s destructor does nothing — `coroutine_handle` is a non-owning handle, and destroying it does not destroy the coroutine frame. The frames therefore leak forever. Since `final_suspend` returns `suspend_always`, a finished coroutine's frame stays resident on the heap with nobody calling `handle.destroy()`. Production code needs a more complete task-management system tracking all live coroutines — say, storing every live handle in a container, calling `handle.destroy()` when a coroutine ends to free the frame and remove it. The next piece, [Coroutine Echo Server](07-coroutine-echo-server), deals with this.

### A Subtlety Inside the Event Loop

You may already have noticed a problem in the event loop above: after `epoll_wait` returns, we resume coroutines, but a coroutine inside `await_resume` may call `epoll_ctl` again to register new events. That means epoll's interest list may be modified while we are in the middle of resuming a coroutine — usually safe, because `epoll_ctl` changes take effect at the next `epoll_wait`. But if you modify the same fd's events inside the resume loop (registered `EPOLLIN`, then switched to `EPOLLOUT` after the coroutine resumed), ordering needs care.

Under LT mode this is usually no problem, because LT is level-triggered — as long as data remains unread, the next `epoll_wait` still notifies you. Under ET mode, however, modifying an fd's registration mid-event-processing can lose notifications.

### The Fast-Path Value of await_ready

Looking back at `AsyncReadAwaiter`, `await_ready` performs one non-blocking `read`. The design is not redundant: in many scenarios the data may already have arrived (the TCP receive buffer already holds some), and the whole ritual — suspend the coroutine, register with epoll, await notification, resume the coroutine — is unneeded; just read. This fast path matters greatly in high-performance settings, saving at least one system call (`epoll_ctl`) and two coroutine context switches.

## Cross-Platform Considerations

All our code above sits on Linux epoll. If you need cross-platform support, two strategies are common.

One is to abstract a unified `IoMultiplexer` interface with different implementations per platform — epoll on Linux, kqueue on macOS, IOCP on Windows — the approach taken by libuv (the library under Node.js) and Boost.Asio. You should also know the other road,

The other is to use a higher-level abstraction — Boost.Asio's `io_context`, for instance, which already wraps the platform differences for you. Asio has offered C++20 coroutine support such as `awaitable<T>`, `use_awaitable`, and `co_spawn()` since 1.13.0 (GCC 10's standard coroutine implementation supported from 1.17.0); you can write cross-platform asynchronous code combining `co_await` with Asio's asynchronous operations.

For learning purposes, epoll is enough for us to grasp I/O multiplexing's core concepts. Once the epoll + coroutine pattern is in hand, switching to kqueue or IOCP is mere API substitution.

## Where to Go from Here

In this piece we bridged coroutines and the operating system's I/O multiplexing. Starting from the problems of blocking I/O, we saw why non-blocking I/O plus polling fails, then followed I/O multiplexing's evolution from select and poll to epoll, with the focus on epoll's three APIs and the LT/ET trigger modes. Next we wired coroutines to epoll — by storing `coroutine_handle` in `epoll_event.data`, we built the complete chain from epoll event notification to resuming the right coroutine. Finally, with these components we assembled a minimal event loop that accepts TCP connections and reads data.

But this event loop is far from a complete server: it lacks graceful coroutine lifetime management, asynchronous write, error handling, timer support — and, most importantly, a complete echo server. Assembling all those puzzle pieces is exactly what the next piece, [Coroutine Echo Server](07-coroutine-echo-server), does, showing you what a coroutine-based network service that truly works looks like.

> Some of the sample code lives in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP) under `code/volumn_codes/vol5/ch06-async-io-coroutine/`; for the rest, the code blocks in this text are authoritative — take and compare as you like.

## Reference Resources

- [epoll(7) — Linux man page](https://man7.org/linux/man-pages/man7/epoll.7.html) — the official epoll documentation, including detailed explanation of LT/ET modes
- [The C10K problem — Dan Kegel](http://www.kegel.com/c10k.html) — the classic analysis of the I/O multiplexing problem, weighing the pros and cons of various I/O models
- [Blocking I/O, Nonblocking I/O, And Epoll — Eli Klitzke](https://eklitzke.org/blocking-io-nonblocking-io-and-epoll) — a complete walkthrough from blocking I/O to non-blocking I/O to epoll
- [Coroutines (C++20) — cppreference](https://en.cppreference.com/cpp/language/coroutines) — the language specification of C++20 coroutines
- [From epoll to io_uring's Multishot Receives](https://codemia.io/blog/path/From-epoll-to-iourings-Multishot-Receives--Why-2025-Is-the-Year-We-Finally-Kill-the-Event-Loop) — discusses the evolution from epoll to io_uring and the future of the event-loop model in 2025
- [io_uring vs epoll — kernel-internals.org](https://kernel-internals.org/io-uring/io-uring-vs-epoll/) — a feature comparison of epoll and io_uring
- [C++20 Coroutines: Sketching a Minimal Async Framework — Jeremy Ong](https://jeremyong.com/cpp/2021/01/04/cpp20-coroutines-a-minimal-async-framework/) — a hands-on reference for building a coroutine async framework from scratch
