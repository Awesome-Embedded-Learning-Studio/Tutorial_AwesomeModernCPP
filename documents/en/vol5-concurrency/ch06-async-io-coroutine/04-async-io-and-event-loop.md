---
title: Asynchronous I/O and Event Loops
description: Understand how I/O multiplexing (epoll/io_uring) works, build a coroutine-driven
  event loop, and bridge the final gap in asynchronous I/O.
chapter: 6
order: 4
tags:
- host
- cpp-modern
- advanced
- coroutine
- 异步编程
difficulty: advanced
platform: host
reading_time_minutes: 35
cpp_standard:
- 20
prerequisites:
- promise_type 与 awaitable
- CPU cache 与 OS 线程
related:
- 协程 Echo Server 实战
translation:
  source: documents/vol5-concurrency/ch06-async-io-coroutine/04-async-io-and-event-loop.md
  source_hash: ef24e2f38eeb3caece0731d0e89703097248a52b4c2cc64ad93c54ee1acc49b7
  translated_at: '2026-05-20T04:47:00.232107+00:00'
  engine: anthropic
  token_count: 4825
---
# Asynchronous I/O and the Event Loop

Previously, we figured out the internal mechanisms of C++20 coroutines — `promise_type` controls the lifetime, awaiter/awaitable controls suspension and resumption, and the scheduler uses `await_suspend` to obtain the coroutine handle to manage execution timing. But honestly, the scheduler we have written so far is just a "ready queue" — it does not know what it means to "wait for data to arrive," "wait for a network connection to become ready," or "wait for a timer to expire."

Coroutines themselves do not solve I/O problems — they are merely a control flow tool. What truly makes asynchronous I/O efficient is the I/O multiplexing mechanism provided by the operating system. What we need to do in this post is: connect coroutines with the operating system's I/O multiplexing mechanism to build an event loop capable of handling real network I/O.

## Environment Notes

Starting from this post, we officially enter Linux-specific territory. All code involving I/O multiplexing in this post relies on Linux's epoll API and cannot be directly compiled and run on Windows or macOS. Our test environment is Linux 2.6+ (epoll has been available since the 2.6 kernel; if you are interested in io_uring, you will need 5.1+), using GCC 13+ or Clang 17+ as the compiler, with the compiler flag `-std=c++20`. It is worth noting that epoll is a Linux-specific API — the equivalent on macOS is kqueue, and on Windows it is IOCP. The underlying concepts are the same, but the APIs are completely different. We will briefly mention solutions for other platforms later on.

## Blocking I/O vs. Non-blocking I/O

Before diving into I/O multiplexing, we need to clarify what "blocking" and "non-blocking" actually mean at the system call level.

In Unix/Linux, all file descriptors (fds) are in blocking mode by default. When you call `read()` on a TCP socket, if there is no data in the receive buffer, `read()` puts the current thread **to sleep** until data arrives (or the connection is closed, or an error occurs). This behavior is called "blocking I/O."

Blocking I/O is fine for single-connection scenarios — you send a request, wait for a response, process the response, and repeat. But when you need to handle thousands of connections simultaneously, problems arise: if no data arrives on one connection, the entire thread gets stuck, and all other connections are left waiting in line. Since one thread can only handle one blocking connection, handling 10,000 connections requires 10,000 threads — which is clearly unsustainable.

> The first time I wrote a highly concurrent network service, I fell right into this trap — one thread per connection. As the number of connections grew, the overhead of thread switching exceeded the overhead of actual work, and the CPU was entirely busy doing context switches.

The first step to a solution is to set the socket to non-blocking mode:

```cpp
#include <fcntl.h>
#include <unistd.h>

void set_nonblocking(int fd)
{
    int kFlags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, kFlags | O_NONBLOCK);
}
```

In non-blocking mode, the behavior of `read()` is completely different: if there is no data in the buffer, `read()` does not sleep. Instead, it returns immediately with `-1` and sets `errno` to `EAGAIN` (or `EWOULDBLOCK` — on Linux, they are the same value). This tells you "there is no data to read right now, try again later."

This sounds great, but the next question is: what do you do after getting `EAGAIN`?

The most naive approach is polling — writing a dead loop that constantly calls `read()` until data arrives. But this causes the CPU to spin at 100% idle, doing nothing useful and purely wasting power. Polling is the worst of all approaches — it wastes CPU resources and does not guarantee timely responses (data might arrive 0.1 milliseconds after `read()` returns `EAGAIN`, but your loop might not call `read()` again for several milliseconds due to scheduling issues).

Is there a way to "go to sleep when there is no data, and be woken up when data arrives"? This is exactly what I/O multiplexing does.

## I/O Multiplexing

The core idea of I/O multiplexing is very simple: you hand over a bunch of fds to the operating system, tell it "which events on these fds I care about (readable, writable, exceptional)," and then you go to sleep. When an event you care about occurs on any of those fds, the operating system wakes you up and tells you "these fds are ready." You process them, hand the fds back, and go back to sleep. And so the cycle repeats.

This way, a single thread can efficiently manage tens of thousands of connections — when there are no events, the thread sleeps quietly without consuming CPU; when events arrive, the thread is woken up to handle the ready connections.

### From select to poll to epoll

I/O multiplexing on Linux has gone through three generations of evolution: `select` → `poll` → `epoll`.

`select` is the earliest solution (a POSIX standard supported by all Unix systems). Its interface works roughly like this: you pass it three fd_sets (read, write, exception), where each fd_set is a bit array with each bit representing an fd. `select` can monitor at most 1024 fds (defined by the `FD_SETSIZE` macro), and every call requires copying the entire fd_set from user space to kernel space, and copying it back on return — when the number of fds is large, this copying overhead is massive. Even worse, after it returns, you do not know which fds are ready; you must traverse the entire fd_set to check.

`poll` improved on some of the issues with `select` — it uses an array of `pollfd` structures instead of a bit array, eliminating the 1024 fd limit. But the core problem remained: every call still requires copying all fd information from user space to kernel space, and you still have to traverse all fds on return.

The true revolution was `epoll` (introduced in Linux 2.5.44). epoll splits "registering fds" and "waiting for events" into two steps: you first use `epoll_ctl` to register the fds you care about into the kernel (the kernel internally maintains a red-black tree, so additions, deletions, modifications, and lookups are all O(log n)), and then you repeatedly call `epoll_wait` to wait for events. The kernel only returns the fds that are **actually ready**, requiring no traversal. In scenarios with a large number of fds but few active fds (which is the typical scenario for highly concurrent network services), epoll's performance far exceeds that of select/poll.

### The Three Core epoll APIs

epoll has exactly three system calls. Let us go through them one by one.

**`epoll_create1(flags)`** creates an epoll instance and returns an epoll fd. This fd acts as a "monitor" — you subsequently register the socket fds you want to monitor onto this epoll fd. `flags` is typically passed as `EPOLL_CLOEXEC` (which automatically closes the epoll fd on exec).

```cpp
#include <sys/epoll.h>

int epfd = epoll_create1(EPOLL_CLOEXEC);
if (epfd < 0) {
    perror("epoll_create1");
    return -1;
}
```

**`epoll_ctl(epfd, op, fd, &event)`** is used to register, modify, or remove monitoring for a specific fd. `op` can be `EPOLL_CTL_ADD` (add), `EPOLL_CTL_MOD` (modify), or `EPOLL_CTL_DEL` (delete). `event` is an `epoll_event` structure that contains the event types you care about and a `data` field (you can stuff any data into it; epoll does not interpret it and returns it to you exactly as is).

```cpp
struct epoll_event ev;
ev.events = EPOLLIN;        // 关心"可读"事件
ev.data.fd = socket_fd;     // 把 socket fd 存在 data 里

// 把 socket_fd 注册到 epoll 实例上
epoll_ctl(epfd, EPOLL_CTL_ADD, socket_fd, &ev);
```

**`epoll_wait(epfd, events, max_events, timeout)`** is the one that actually does the work — it blocks waiting for events to occur on the registered fds and returns the number of ready fds. `events` is an array you provide, which epoll fills with ready events. `timeout` is the timeout (in milliseconds), and `-1` means wait indefinitely.

```cpp
struct epoll_event events[64];
int n = epoll_wait(epfd, events, 64, -1); // 阻塞等待
for (int i = 0; i < n; ++i) {
    int ready_fd = events[i].data.fd;
    // 处理 ready_fd 上的事件
}
```

That is the entire epoll API — three calls, concise yet powerful.

### LT vs. ET: Level-Triggered and Edge-Triggered

epoll has two trigger modes: Level Triggered (LT, the default mode) and Edge Triggered (ET, which requires setting the `EPOLLET` flag).

The terms "level" and "edge" come from electronics — level-triggered means "continuously trigger as long as the level is high," while edge-triggered means "trigger only at the instant the level goes from low to high." In the context of epoll:

**LT mode**: As long as there is data to read (or write) on the fd, `epoll_wait` will repeatedly notify you. It does not matter if you have not finished reading the data; the next `epoll_wait` will still tell you "this fd is still readable." LT mode is relatively simple and less error-prone.

**ET mode**: Notifies you only once when the state of the fd changes — for example, at the exact moment the buffer goes from "empty" to "has data." If you do not read all the data (until you get `EAGAIN`), the next `epoll_wait` will not notify you again until new data arrives. ET mode can reduce the number of returns from `epoll_wait` (by processing all data at once), but the coding is more complex, and **you must use non-blocking I/O**, otherwise you might get stuck blocking during the read loop.

> ⚠️ **ET mode requires non-blocking I/O.** Because ET mode requires you to read all the data at once (until `EAGAIN`), if the socket is blocking, the final `read()` will block when there is no data, and the entire event loop will freeze.

For most network applications, LT mode is more than sufficient and easier to program. ET mode is suited for scenarios with extreme performance requirements (like Nginx). Our subsequent examples will all use LT mode.

### Solutions on Other Platforms

Let us briefly mention I/O multiplexing solutions on other operating systems, in case you need to work in a cross-platform environment. macOS and BSD systems use kqueue; the concept is similar to epoll but the API is slightly different. Nginx and Node.js on macOS both use kqueue under the hood. On Windows, there is IOCP (I/O Completion Ports), which adopts a "completion" model rather than a "readiness" model — you initiate an asynchronous operation, and the operating system notifies you when the operation is complete. This is fundamentally different from epoll's "readiness notification" model. Linux 5.1+ introduced the next-generation asynchronous I/O solution, io_uring, which uses shared memory ring buffers to submit and complete I/O operations, avoiding the overhead of traditional system calls. Its performance is better than epoll, but its API complexity is also higher, and it is still evolving rapidly.

Regarding io_uring, it is worth mentioning that the fundamental difference from epoll lies in this: epoll is a reactor pattern (telling you "it is ready, go read/write it yourself"), while io_uring is closer to a proactor pattern (you submit read/write requests, the kernel completes them and notifies you via the completion ring that "it is done" — though io_uring also supports a polling mode, so it is not entirely equivalent to the classic proactor). io_uring's performance in high-concurrency scenarios generally surpasses epoll because it reduces the number of system calls — you can batch multiple I/O operations into the submission ring buffer, the kernel processes them in bulk, and then notifies you via the completion ring when they are done. However, epoll has a more mature ecosystem and richer documentation, and most production environments still use it. We chose epoll as our teaching vehicle here precisely because its concepts are more intuitive and its API is simpler.

## The Event Loop Pattern

Before writing code, let us clarify what the "Event Loop" pattern actually is.

The core structure of an event loop is an infinite loop, where each iteration does three things: first, check timers to see if any have expired and need processing; then, call `epoll_wait` (or another I/O multiplexing mechanism) to block and wait for ready fds; and finally, dispatch events for each ready fd — calling the corresponding callback function or resuming the corresponding coroutine. The pseudocode looks roughly like this:

```cpp
while (运行中) {
    处理到期的定时器();
    n = epoll_wait(..., timeout = 最近定时器的剩余时间);
    for (i = 0; i < n; ++i) {
        处理 events[i] 上的 I/O 事件;
    }
}
```

This is the core pattern behind Node.js, Nginx, Redis, Chrome, and libuv. Of course, actual implementations are much more complex (needing to handle signals, inter-thread communication, graceful shutdown, etc.), but the skeleton is this loop.

## Connecting Coroutines and epoll

Now we have coroutines (functions that can suspend and resume) and epoll (a system call that can efficiently wait for I/O events). The question is how to connect them.

The key insight was already mentioned at the end of the previous post: **the awaiter's `await_suspend` is the bridge for scheduler integration**. The entire flow works like this — when a coroutine `co_await` an I/O operation (such as `async_read(socket, buffer)`), the awaiter's `await_suspend` is called. It stores the coroutine's `std::coroutine_handle` somewhere and simultaneously registers the socket fd with epoll. Then `await_suspend` returns, the coroutine suspends, and control returns to the event loop. The event loop calls `epoll_wait` to block and wait for I/O events. When data arrives on the socket, `epoll_wait` returns. The event loop retrieves the coroutine handle from `epoll_event.data` and calls `handle.resume()` to resume the coroutine. At this point, `await_resume()` returns the read data, and the coroutine continues execution from the `co_await` expression.

The key trick is: **storing the `coroutine_handle` in the `epoll_event.data`**. `epoll_event.data` is a `union` that can hold a `void*` pointer or an `int` fd. `coroutine_handle` can be safely converted to an `void*` (via `handle.address()`), and can also be converted back from an `void*` (via `std::coroutine_handle<>::from_address()`).

Now let us look at the specific code implementation.

## A Minimal Event Loop Implementation

We want to implement a minimal event loop capable of handling TCP accept and read. The entire implementation is about 200 lines of code, but it covers all the core concepts of coroutines + epoll.

### Step 1: The Event Loop Skeleton

Let us first set up a basic event loop class that encapsulates epoll's creation, registration, waiting, and dispatching.

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

You will notice that the `add_reader` method simply stores the address of `coroutine_handle` into `epoll_event.data.ptr`. This is the most crucial step in the entire design — it establishes a one-to-one mapping between epoll events and coroutines. When `epoll_wait` returns an event, we can directly recover the corresponding coroutine handle from `data.ptr` and then `resume()` it.

### Step 2: The Coroutine Task Type

Next, we define a coroutine task type whose `promise_type` works with our event loop.

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

### Step 3: Asynchronous accept

When a client connection arrives, we need to accept it. In the coroutine world, accept becomes an `co_await async_accept(listen_fd)` operation — if there is no connection yet, the coroutine suspends, and it resumes when epoll notifies that listen_fd is readable.

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

There is a subtle elegance here: in `await_suspend` we registered the epoll event, but we have not yet called `accept` — because there is no new connection yet. When epoll notifies that listen_fd is readable (meaning a new connection has arrived), the event loop resumes the coroutine, and only then does `await_resume` execute the actual `accept`. This is much clearer than traditional callback-based code.

### Step 4: Asynchronous read

The read pattern is almost identical to accept — first register with epoll, and only perform the actual `read` after data arrives.

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

You will notice that in `await_ready` we first attempt a non-blocking `read`. If the data has already arrived, we return immediately, saving the overhead of registering with epoll and suspending/resuming. This demonstrates the value of `await_ready` as a "fast path optimization" — in most cases, if you can determine in advance whether the operation is already complete, you should do so in `await_ready`.

### Step 5: Assembling the Pieces

Now we have a complete event loop, a coroutine task type, asynchronous accept, and asynchronous read. Next, we assemble them into a program that can accept TCP connections and read data.

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

Although this program still has several rough edges (such as the lifetime management of the handle_client coroutine, and the lack of an async_write implementation), it is already a working coroutine-based TCP server. Let us review the entire flow: `main()` creates the listening socket, starts the accept loop coroutine, and enters the event loop. When the accept loop coroutine reaches `co_await async_accept(listen_fd)`, there is no new connection yet, so the coroutine suspends and listen_fd is registered with epoll. The event loop blocks on `epoll_wait`. When a client connection arrives, epoll notifies that listen_fd is readable, and the event loop resumes the accept coroutine. After the accept coroutine obtains the client_fd, it starts the handle_client coroutine to handle this connection, then returns to `co_await async_accept` to continue waiting for the next connection. When the handle_client coroutine reaches `co_await async_read(client_fd, ...)`, client_fd is registered with epoll and the coroutine suspends. When data arrives, epoll notifies that client_fd is readable, the event loop resumes the handle_client coroutine, which reads the data, echoes it back, and then returns to `co_await async_read` to continue waiting for the next batch of data. Throughout this entire process, a single thread manages all connections — when there are no I/O events, the thread sleeps quietly on `epoll_wait`, and is only woken up to process events when they arrive.

> ⚠️ **There is a lifetime management pitfall in this code.** The `IoTask` object returned by `handle_client` is destroyed at the end of each loop iteration, but `IoTask`'s destructor does nothing — `coroutine_handle` is a non-owning handle, and its destruction does not destroy the coroutine frame. This means the coroutine frame is never freed (memory leak). Since `final_suspend` returns `suspend_always`, the frame remains on the heap after the coroutine completes, and nobody calls `handle.destroy()`. In production code, you need a more robust task management system to track all active coroutines — for example, storing all active coroutine handles in a container and calling `handle.destroy()` to free the frame and remove it from the container when the coroutine finishes. We will address this issue in the Echo Server in the next post.

### A Subtle Issue in the Event Loop

You may have already noticed that the event loop above has an issue: after `epoll_wait` returns, we resume the coroutine, but the coroutine might call `epoll_ctl` again inside `await_resume` to register new events. This means the epoll interest list could be modified while resuming a coroutine — this is usually safe because modifications from `epoll_ctl` only take effect on the next `epoll_wait` call. But if you modify the events for the same fd while resuming coroutines in the loop (for example, first registering `EPOLLIN`, and then changing it to `EPOLLOUT` after the coroutine resumes), you need to be careful about ordering issues.

In LT mode, this is usually not a problem because LT mode is "level-triggered" — as long as you still have unread data, the next `epoll_wait` will notify you again. But in ET mode, if you modify an fd's registration while processing an event, you might lose the event notification.

### The Value of the Fast Path in await_ready

Looking back at our `AsyncReadAwaiter`, we perform a non-blocking `read` first in `await_ready`. This design is not redundant — in many scenarios, the data may have already arrived (the TCP receive buffer already contains data). In such cases, there is no need for the entire process of suspending the coroutine, registering with epoll, waiting for a notification, and resuming the coroutine; you can just read directly. This fast path is extremely important in high-performance scenarios because it saves at least one system call (`epoll_ctl`) and two coroutine context switches.

## Cross-Platform Considerations

All the code above is based on Linux epoll. If you need cross-platform support, there are two common strategies:

The first is to abstract a unified `IoMultiplexer` interface with different implementations on different platforms — epoll on Linux, kqueue on macOS, and IOCP on Windows. This is the approach adopted by libuv (Node.js's underlying library) and Boost.Asio.

The second is to use a higher-level abstraction — such as Boost.Asio's `io_context`, which already encapsulates the platform differences for you. Since version 1.13.0, Asio has provided C++20 coroutine support including `awaitable<T>`, `use_awaitable`, and `co_spawn()` (support for GCC 10's standard coroutine implementation arrived in 1.17.0). You can use `co_await` with Asio's asynchronous operations to write cross-platform asynchronous code.

For learning purposes, epoll is sufficient for us to understand the core concepts of I/O multiplexing. Once you have mastered the epoll + coroutine pattern, switching to kqueue or IOCP is merely a matter of API replacement.

## Where We Are

In this post, we bridged the gap between coroutines and the operating system's I/O multiplexing. Starting from the problems with blocking I/O, we saw why non-blocking I/O + polling is not feasible, and then introduced I/O multiplexing (the evolution from select to poll to epoll), focusing on epoll's three APIs and the two trigger modes, LT and ET. Next, we connected coroutines with epoll — by storing the `coroutine_handle` in the `epoll_event.data`, we achieved the closed loop of "epoll event notification → resume the corresponding coroutine." Finally, we used these components to build a minimal event loop capable of accepting TCP connections and reading data.

But this event loop is still far from a complete server — it lacks graceful coroutine lifetime management, asynchronous write, error handling, timer support, and most importantly: a complete Echo Server. What we will do in the next post is put all these puzzle pieces together to implement a fully functional coroutine-based Echo Server, so you can see what a "truly usable" coroutine network service looks like.

> 💡 The complete example code is available in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `code/volumn_codes/vol5/ch06-async-io-coroutine/`.

## References

- [epoll(7) — Linux man page](https://man7.org/linux/man-pages/man7/epoll.7.html) — The official documentation for epoll, including detailed explanations of LT/ET modes
- [The C10K problem — Dan Kegel](http://www.kegel.com/c10k.html) — A classic article analyzing the "I/O multiplexing" problem, discussing the pros and cons of various I/O models
- [Blocking I/O, Nonblocking I/O, And Epoll — Eli Klitzke](https://eklitzke.org/blocking-io-nonblocking-io-and-epoll) — A complete walkthrough from blocking I/O to non-blocking I/O to epoll
- [Coroutines (C++20) — cppreference](https://en.cppreference.com/cpp/language/coroutines) — The language specification for C++20 coroutines
- [From epoll to io_uring's Multishot Receives](https://codemia.io/blog/path/From-epoll-to-iourings-Multishot-Receives--Why-2025-Is-the-Year-We-Finally-Kill-the-Event-Loop) — Discusses the evolution from epoll to io_uring, and the future of the event loop model in 2025
- [io_uring vs epoll — kernel-internals.org](https://kernel-internals.org/io-uring/io-uring-vs-epoll/) — A feature comparison between epoll and io_uring
- [C++20 Coroutines: Sketching a Minimal Async Framework — Jeremy Ong](https://jeremyong.com/cpp/2021/01/04/cpp20-coroutines-a-minimal-async-framework/) — A practical reference for building a coroutine-based async framework from scratch
