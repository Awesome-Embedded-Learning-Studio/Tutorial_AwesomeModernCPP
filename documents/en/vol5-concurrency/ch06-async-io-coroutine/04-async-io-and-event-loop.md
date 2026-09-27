---
chapter: 6
cpp_standard:
- 20
description: Understand how I/O multiplexing (epoll/io_uring) works, build a coroutine-driven
  event loop, and complete the final mile of asynchronous I/O
difficulty: advanced
order: 4
platform: host
prerequisites:
- promise_type and awaitable
- CPU Cache and OS Threads
reading_time_minutes: 25
related:
- 'Coroutine Echo Server in Practice'
tags:
- host
- cpp-modern
- advanced
- coroutine
- 异步编程
title: Asynchronous I/O and Event Loops
translation:
  source: documents/vol5-concurrency/ch06-async-io-coroutine/04-async-io-and-event-loop.md
  source_hash: bae752d9cfe97a1413c7289d01ffa48971de060a1f4bd6d11a7adc83155f6c26
  translated_at: '2026-09-26T09:01:25+00:00'
  engine: anthropic
  token_count: 8000
---
# Asynchronous I/O and Event Loops

In the previous article we worked out the internals of C++20 coroutines—`promise_type` controls the lifecycle, the awaiter/awaitable controls suspension and resumption, and the scheduler grabs the coroutine handle via `await_suspend` to manage when execution happens. But honestly, the scheduler we have written so far is just a "ready queue"—it has no idea what "waiting for data to arrive" means, no idea what "waiting for a network connection to become ready" means, and certainly no idea what "waiting for a timer to expire" means.

Coroutines by themselves do not solve the I/O problem—they are purely a control-flow tool. What actually makes asynchronous I/O efficient is the I/O multiplexing machinery provided by the operating system. The job of this article is to wire coroutines up to the OS's I/O multiplexing and build an event loop that can handle real network I/O.

## Environment Notes

From this article on, we formally enter Linux-specific territory. All the I/O multiplexing code in this article depends on Linux's epoll API and will not compile or run directly on Windows or macOS. Our test environment is Linux 2.6+ (epoll has been available since the 2.6 kernel; if you are interested in io_uring you need 5.1+), the compiler is GCC 13+ or Clang 17+, and the compiler flag is `-std=c++20`. One reminder worth stating: epoll is a Linux-specific API—the macOS counterpart is kqueue and the Windows one is IOCP. The ideas line up, but the APIs are completely different. We will briefly cover the other platforms' approaches later.

## Blocking I/O vs. Non-blocking I/O

Before talking about I/O multiplexing, we need to pin down, at the system-call level, what "blocking" and "non-blocking" actually mean.

On Unix/Linux, every file descriptor (fd) is in blocking mode by default. When you call `read()` on a TCP socket and the receive buffer is empty, `read()` puts the current thread **to sleep** until data arrives (or the connection closes, or an error occurs). This behavior is called "blocking I/O" (blocking I/O).

Blocking I/O is fine in single-connection scenarios—you send a request, wait for the response, process it, and repeat. But once you need to handle thousands of connections simultaneously, trouble arrives: if no data arrives on one connection, the whole thread is stuck, and every other connection queues up behind it. One thread can service only one blocking connection, so 10,000 connections would require 10,000 threads—which is clearly unsustainable.

> The first time I wrote a high-concurrency network service, I fell right into this trap—one thread per connection. As the connection count climbed, the overhead of thread switching exceeded the cost of the actual work; the CPU was busy doing nothing but context switching.

The first step toward a solution is putting the socket into non-blocking mode:

```cpp
#include <fcntl.h>
#include <unistd.h>

void set_nonblocking(int fd)
{
    int kFlags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, kFlags | O_NONBLOCK);
}
```

In non-blocking mode, `read()` behaves completely differently: if the buffer is empty, `read()` does not sleep—it returns `-1` immediately and sets `errno` to `EAGAIN` (or `EWOULDBLOCK`—on Linux they are the same value). That is the kernel telling you "nothing to read right now, try again later".

This sounds nice, but the next question is immediate: what do you do after you get `EAGAIN`?

The most naive answer is polling—a tight loop that keeps calling `read()` until data shows up. But that pins the CPU at 100% doing absolutely nothing useful, a pure waste of electricity. Polling is the worst of all approaches: it burns CPU resources and still cannot guarantee timely responses (the data might arrive 0.1 ms after your last `read()` returned `EAGAIN`, yet your loop might not call `read()` again for several more milliseconds because of scheduling).

Is there a way to "sleep while there is no data, and be woken when data arrives"? That is exactly what I/O multiplexing is for.

## I/O Multiplexing

The core idea of I/O multiplexing is simple: you hand a bunch of fds to the operating system, tell it "here are the events I care about on these fds (readable, writable, exceptional)", and then you go to sleep. When an event you care about happens on any of those fds, the OS wakes you up and tells you "these fds are ready". You handle them, hand the fds back, and go back to sleep. Round and round it goes.

This way, a single thread can efficiently manage tens of thousands of connections—with no events pending, the thread sleeps quietly and consumes no CPU; when events arrive, it wakes up and services the ready connections.

### From select to poll to epoll

I/O multiplexing on Linux went through three generations: `select` → `poll` → `epoll`.

`select` is the oldest scheme (a POSIX standard, supported by every Unix). Its interface looks roughly like this: you pass it three fd_sets (read, write, exceptional), each a bit array where every bit stands for one fd. `select` can watch at most 1024 fds (the `FD_SETSIZE` macro), and every call must copy the entire fd_set from user space to kernel space, and back again on return—when the fd count is large, that copying gets very expensive. Worse still, once it returns you have no idea which fds are ready; you must walk the entire fd_set to check.

`poll` fixed some of `select`'s problems—it replaces the bit array with an array of `pollfd` structures, removing the 1024-fd limit. But the core problem survives: every call still copies all the fd information from user space to kernel space, and you still have to scan every fd after the return.

The real revolution was `epoll` (introduced in Linux 2.5.44). epoll splits "registering fds" and "waiting for events" into two steps: first you register the fds you care about into the kernel with `epoll_ctl` (the kernel maintains an internal red-black tree, so add/remove/modify/lookup are all O(log n)), then you call `epoll_wait` repeatedly to wait for events. The kernel returns only the fds that are **actually ready**—no scanning required. When the fd count is large but only a few fds are active (precisely the typical shape of a high-concurrency network service), epoll far outperforms select/poll.

### The Three Core epoll APIs

epoll amounts to just three system calls—let's walk through them one by one.

**`epoll_create1(flags)`** creates an epoll instance and returns an epoll fd. That fd is the "watcher"—you later register the socket fds you want to watch onto this epoll fd. `flags` is usually `EPOLL_CLOEXEC` (close the epoll fd automatically on exec).

```cpp
#include <sys/epoll.h>

int epfd = epoll_create1(EPOLL_CLOEXEC);
if (epfd < 0) {
    perror("epoll_create1");
    return -1;
}
```

**`epoll_ctl(epfd, op, fd, &event)`** registers, modifies, or removes the watch on a given fd. `op` is one of `EPOLL_CTL_ADD` (add), `EPOLL_CTL_MOD` (modify), `EPOLL_CTL_DEL` (delete). `event` is an `epoll_event` structure holding the event types you care about plus a `data` field (you can stuff anything in there; epoll does not interpret it and hands it back untouched).

```cpp
struct epoll_event ev;
ev.events = EPOLLIN;        // We care about "readable" events
ev.data.fd = socket_fd;     // Store the socket fd in data

// Register socket_fd on the epoll instance
epoll_ctl(epfd, EPOLL_CTL_ADD, socket_fd, &ev);
```

**`epoll_wait(epfd, events, max_events, timeout)`** is where the real work happens—it blocks waiting for events on the registered fds and returns the number of ready fds. `events` is an array you provide; epoll fills in the ready events. `timeout` is the timeout in milliseconds, and `-1` means wait indefinitely.

```cpp
struct epoll_event events[64];
int n = epoll_wait(epfd, events, 64, -1); // Block and wait
for (int i = 0; i < n; ++i) {
    int ready_fd = events[i].data.fd;
    // Handle the event on ready_fd
}
```

That is the entire epoll API—three calls, compact and powerful.

### LT vs ET: Level-Triggered and Edge-Triggered

epoll has two triggering modes: Level Triggered (LT, level-triggered, the default) and Edge Triggered (ET, edge-triggered, which requires setting the `EPOLLET` flag).

"Level" and "edge" are borrowed from electronics—level-triggered means "keep firing as long as the level stays high", while edge-triggered means "fire exactly once, at the instant the level goes from low to high". In epoll's context:

**LT mode**: as long as the fd has data to read (or room to write), `epoll_wait` keeps notifying you. It does not matter if you did not drain the data; the next `epoll_wait` will still tell you "this fd can still be read". LT mode is simpler and harder to get wrong.

**ET mode**: you are notified exactly once, when the fd's state changes—for instance, the moment the buffer goes from "empty" to "has data". If you did not read all the data out (until `EAGAIN`), the next `epoll_wait` will not notify you again, until new data arrives. ET mode can reduce the number of `epoll_wait` returns (process all the data in one sweep), but the code is more involved, and it **must use non-blocking I/O**—otherwise the read loop can block.

> ⚠️ **ET mode must use non-blocking I/O.** Because ET mode requires you to read all the data in one pass (until `EAGAIN`), and if the socket is blocking, the final `read()` will block once there is no data left, seizing up the entire event loop.

For most network applications, LT mode is plenty good and simpler to program. ET mode suits scenarios with extreme performance demands (Nginx, for example). All our examples below use LT mode.

### Approaches on Other Platforms

A quick note on the other operating systems' I/O multiplexing schemes, in case you need to work cross-platform. macOS and the BSD family use kqueue—the idea is similar to epoll though the API differs somewhat; Nginx and Node.js on macOS both sit on kqueue underneath. Windows has IOCP (I/O Completion Ports), which follows a "completion" model rather than a "readiness" model—you launch an asynchronous operation, and the operating system notifies you when the operation completes; this is a fundamental difference from epoll's "readiness notification" model. Linux 5.1+ introduced io_uring, the next-generation asynchronous I/O scheme: it submits and completes I/O operations through shared-memory ring buffers, avoiding the overhead of traditional system calls and outperforming epoll, at the price of a more complex API that is still evolving rapidly.

Regarding io_uring, the fundamental distinction from epoll is worth spelling out: epoll is the reactor pattern (it tells you "you are ready, go read and write yourself"), while io_uring is closer to the proactor pattern (you submit read/write requests, the kernel completes them for you and notifies you "done" through the completion ring—though io_uring also supports a polling mode, so it is not exactly the classic proactor). Under high concurrency, io_uring usually outperforms epoll because it reduces the number of system calls—you can bundle several I/O operations, submit them to the ring buffer, have the kernel process them in a batch, and get notified through the completion ring when they finish. But epoll has the more mature ecosystem and richer documentation, and most production environments still use it. We choose epoll as our teaching vehicle here precisely because its concepts are more intuitive and its API simpler.

## The Event Loop Pattern

Before touching the code, let's pin down what exactly the "event loop" is as a pattern.

The core structure of an event loop is one infinite loop, and each iteration does three things: first check the timers for any expired ones that need handling; then call `epoll_wait` (or another I/O multiplexing mechanism) to block waiting for ready fds; finally dispatch an event for every ready fd—invoke the corresponding callback or resume the corresponding coroutine. The pseudocode looks roughly like this:

```cpp
while (running) {
    process_expired_timers();
    n = epoll_wait(..., timeout = time remaining until the nearest timer);
    for (i = 0; i < n; ++i) {
        handle the I/O event on events[i];
    }
}
```

This is the core pattern behind Node.js, Nginx, Redis, Chrome, and libuv. Real implementations are of course far more complicated (handling signals, inter-thread communication, graceful shutdown, and so on), but the skeleton is this loop.

## Bridging Coroutines and epoll

So now we have coroutines (functions that can suspend and resume) and epoll (a system call that can wait for I/O events efficiently). The question is how to connect them.

The key insight appeared at the end of the previous article: **the awaiter's `await_suspend` is the bridge for scheduler integration**. The whole flow goes like this—when a coroutine `co_await`s an I/O operation (say `async_read(socket, buffer)`), the awaiter's `await_suspend` is invoked; it stores the coroutine's `std::coroutine_handle` somewhere and registers the socket fd with epoll. Then `await_suspend` returns, the coroutine suspends, and control lands back in the event loop. The event loop calls `epoll_wait` and blocks waiting for I/O events; when data arrives on the socket, `epoll_wait` returns, the event loop pulls the coroutine handle out of `epoll_event.data`, calls `handle.resume()` to resume the coroutine, at which point `await_resume()` returns the data that was read, and the coroutine carries on from the `co_await` expression.

The key trick is: **store the `coroutine_handle` inside `epoll_event.data`**. `epoll_event.data` is a `union` that can hold a `void*` pointer or an `int` fd. A `coroutine_handle` converts safely to `void*` (via `handle.address()`) and back again (via `std::coroutine_handle<>::from_address()`).

Now let's look at the concrete implementation.

## A Minimal Event Loop Implementation

We are going to build a minimal event loop that can handle TCP accept + read. The whole implementation is about 200 lines of code, yet it covers every core concept of coroutines + epoll.

### Step 1: The Event Loop Skeleton

First, sketch the most basic event loop class, wrapping epoll's creation, registration, waiting, and dispatch.

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

/// The event loop—wraps epoll operations
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

    /// Register an fd with epoll, associating a coroutine handle
    void add_reader(int fd, uint32_t events,
                    std::coroutine_handle<> handle)
    {
        struct epoll_event ev;
        ev.events = events;
        // Key step: store the coroutine_handle inside epoll_event.data
        ev.data.ptr = handle.address();
        if (epoll_ctl(kEpollFd, EPOLL_CTL_ADD, fd, &ev) < 0) {
            // The fd may already be registered (e.g. the accept loop reuses
            // the same listen_fd); fall back to MOD to update the handle and events
            epoll_ctl(kEpollFd, EPOLL_CTL_MOD, fd, &ev);
        }
    }

    /// Remove an fd from epoll
    void remove(int fd)
    {
        epoll_ctl(kEpollFd, EPOLL_CTL_DEL, fd, nullptr);
    }

    /// Run the event loop
    void run()
    {
        struct epoll_event events[64];
        std::puts("=== 事件循环启动 ===");

        while (kRunning) {
            // Wait for I/O events with a 1-second timeout
            int n = epoll_wait(kEpollFd, events, 64, 1000);
            if (n < 0) {
                if (errno == EINTR) {
                    continue; // Interrupted by a signal, retry
                }
                perror("epoll_wait");
                break;
            }

            for (int i = 0; i < n; ++i) {
                // Restore the coroutine_handle from epoll_event.data
                auto handle = std::coroutine_handle<>::from_address(
                    events[i].data.ptr
                );
                if (handle && !handle.done()) {
                    handle.resume(); // Resume the coroutine
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

Notice what the `add_reader` method does: it stores the address of the `coroutine_handle` into `epoll_event.data.ptr`. This is the single most important step in the whole design—it establishes a one-to-one mapping between epoll events and coroutines. When `epoll_wait` returns an event, we can reconstruct the corresponding coroutine handle directly from `data.ptr` and then `resume()` it.

### Step 2: A Coroutine Task Type

Next, define a coroutine task type whose `promise_type` cooperates with our event loop.

```cpp
/// Task type for asynchronous I/O
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

When a client connection arrives, we need to accept it. In the coroutine world, accept becomes `co_await async_accept(listen_fd)`—if no connection is pending, the coroutine suspends, and resumes once epoll reports the listen_fd readable.

```cpp
/// Global event loop instance
EventLoop g_event_loop;

/// Set a socket to non-blocking
void set_nonblocking(int fd)
{
    int kFlags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, kFlags | O_NONBLOCK);
}

/// Awaiter for asynchronous accept
struct AsyncAcceptAwaiter {
    int kListenFd;

    explicit AsyncAcceptAwaiter(int listen_fd)
        : kListenFd(listen_fd) {}

    bool await_ready() noexcept
    {
        // Try a non-blocking accept first to see if a connection is already waiting
        return false; // Simplification: always suspend
    }

    void await_suspend(std::coroutine_handle<> handle)
    {
        // Register listen_fd with epoll, watching for readability (a new
        // connection arriving), and store the coroutine handle in epoll_event.data
        g_event_loop.add_reader(
            kListenFd,
            EPOLLIN,
            handle
        );
    }

    int await_resume()
    {
        // The coroutine resumes: do the accept and take the new connection
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

/// The coroutine-style accept function
AsyncAcceptAwaiter async_accept(int listen_fd)
{
    return AsyncAcceptAwaiter(listen_fd);
}
```

Here is the elegant part: in `await_suspend` we registered the epoll event, but we have not called `accept` yet—because no new connection exists. Only when epoll reports the listen_fd readable (meaning a new connection has arrived) does the event loop resume the coroutine, and `await_resume` performs the real `accept`. This is far clearer than traditional callback-style code.

### Step 4: Asynchronous read

The pattern for read is almost identical to accept—register with epoll first, and do the real `read` once the data has arrived.

```cpp
/// Awaiter for asynchronous read
struct AsyncReadAwaiter {
    int kFd;
    void* kBuffer;
    std::size_t kSize;
    ssize_t kResult; // Result of the read
    bool kSuspended; // Whether suspension happened

    AsyncReadAwaiter(int fd, void* buffer, std::size_t size)
        : kFd(fd), kBuffer(buffer), kSize(size), kResult(0),
          kSuspended(false) {}

    bool await_ready() noexcept
    {
        // Try a non-blocking read first
        kResult = ::read(kFd, kBuffer, kSize);
        if (kResult >= 0) {
            return true; // Got data, no need to suspend
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return false; // No data yet, suspend and wait for epoll's notification
        }
        return true; // Error: don't suspend, let await_resume handle it
    }

    void await_suspend(std::coroutine_handle<> handle)
    {
        kSuspended = true;
        // Register with epoll, waiting for the fd to become readable
        g_event_loop.add_reader(kFd, EPOLLIN, handle);
    }

    ssize_t await_resume()
    {
        if (kSuspended) {
            // Resumed after suspension: epoll reports the fd readable,
            // do the real read
            kResult = ::read(kFd, kBuffer, kSize);
        }
        return kResult;
    }
};

/// The coroutine-style read function
AsyncReadAwaiter async_read(int fd, void* buffer, std::size_t size)
{
    return AsyncReadAwaiter(fd, buffer, size);
}
```

Notice that in `await_ready` we first attempted a non-blocking `read`. If the data is already there, we return directly, saving the cost of epoll registration and suspend/resume. This is `await_ready`'s value as a "fast-path optimization"—in most cases, if you can determine up front that an operation has already completed, that check belongs in `await_ready`.

### Step 5: Putting It All Together

We now have a complete event loop, a coroutine task type, asynchronous accept, and asynchronous read. Next, assemble them into a program that can accept TCP connections and read data.

```cpp
/// Create the listening socket
int create_listen_socket(uint16_t port)
{
    int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return -1;
    }

    // Set SO_REUSEADDR so the port can be reused
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

/// Coroutine that handles a single client connection
IoTask handle_client(int client_fd)
{
    char buffer[1024];
    std::printf("[协程] 新连接 fd=%d\n", client_fd);

    while (true) {
        // Read data asynchronously (reserve 1 byte for the '\0' terminator)
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

        // Simple echo: print the received data
        buffer[n] = '\0';
        std::printf("[协程] 收到数据 fd=%d: %s", client_fd, buffer);

        // NOTE: this should really use async_write; synchronous write keeps
        // things simple. Under LT mode, synchronous write is usually fine
        // for small amounts of data
        ::write(client_fd, buffer, n);
    }
}

/// Coroutine that accepts new connections
IoTask accept_loop(int listen_fd)
{
    std::printf("[协程] 开始监听，等待连接...\n");

    while (true) {
        // Asynchronous accept—the coroutine suspends while no new
        // connection arrives
        int client_fd = co_await async_accept(listen_fd);

        if (client_fd < 0) {
            std::printf("[协程] accept 失败\n");
            continue;
        }

        std::printf("[协程] 接受新连接 fd=%d\n", client_fd);

        // Start a new coroutine to handle this connection.
        // NOTE: coroutines created here need manual lifetime management
        auto task = handle_client(client_fd);
        // Start the handle_client coroutine immediately
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

    // Create the accept-loop coroutine
    auto acceptor = accept_loop(listen_fd);
    // Start it manually (because initial_suspend returns suspend_always)
    acceptor.handle.resume();

    // Run the event loop
    g_event_loop.run();

    // Clean up
    close(listen_fd);
    return 0;
}
```

This program still has plenty of rough edges (the lifetime management of the handle_client coroutines, the missing async_write, and so on), but it is already a working coroutine-based TCP server. Let's review the whole flow: `main()` creates the listening socket, starts the accept-loop coroutine, and enters the event loop. The accept-loop coroutine runs until `co_await async_accept(listen_fd)`; with no new connection pending, the coroutine suspends and the listen_fd is registered with epoll. The event loop blocks in `epoll_wait`; when a client connects, epoll reports the listen_fd readable and the event loop resumes the accept coroutine. Having obtained client_fd, the accept coroutine starts a handle_client coroutine to handle that connection, then returns to `co_await async_accept` to wait for the next one. The handle_client coroutine runs until `co_await async_read(client_fd, ...)`; the client_fd is registered with epoll and the coroutine suspends. Once data arrives, epoll reports the client_fd readable, the event loop resumes the handle_client coroutine, it reads the data, echoes it, and returns to `co_await async_read` to wait for the next batch. Throughout all of this, a single thread manages every connection—with no I/O events, the thread sleeps quietly in `epoll_wait`, waking only when events arrive to handle them.

> ⚠️ **There is a lifetime-management trap in this code.** The `IoTask` object returned by `handle_client` is destroyed at the end of each loop iteration, but `IoTask`'s destructor does nothing—a `coroutine_handle` is a non-owning handle, and destroying it does not destroy the coroutine frame. That means the coroutine frame is never freed (a memory leak). Because `final_suspend` returns `suspend_always`, the frame stays resident on the heap after the coroutine finishes, and nobody calls `handle.destroy()`. In production code, you need a more complete task-management system to track all live coroutines—for example, store every live coroutine handle in a container, and when a coroutine ends, call `handle.destroy()` to free the frame and remove it from the container. We will deal with this in the next article's Echo Server.

### A Subtle Issue in the Event Loop

You may already have noticed a problem with the event loop above: after `epoll_wait` returns, we resume coroutines, but a coroutine may call `epoll_ctl` again inside `await_resume` to register new events. That means epoll's interest list can be modified while we are in the middle of resuming a coroutine—this is usually safe, because `epoll_ctl`'s modifications only take effect at the next `epoll_wait`. But if you modify the same fd's events inside the loop that resumes coroutines (say you first registered `EPOLLIN`, and after the coroutine resumes you switch it to `EPOLLOUT`), you need to be careful about ordering.

Under LT mode this is usually not a problem, because LT is level-triggered—as long as you still have data left unread, the next `epoll_wait` will notify you again. Under ET mode, however, if you modify an fd's registration while processing events, you may lose event notifications.

### The Fast-Path Value of await_ready

Looking back at our `AsyncReadAwaiter`, `await_ready` performs a non-blocking `read` up front. This design is not redundant—in many scenarios the data may already have arrived (there is already data in the TCP receive buffer), in which case there is no need for the whole routine of suspending the coroutine, registering with epoll, waiting for notification, and resuming the coroutine—just read it directly. This fast path matters a great deal in high-performance scenarios, because it saves at least one system call (`epoll_ctl`) and two coroutine context switches.

## Cross-Platform Considerations

All our code above is based on Linux epoll. If you need cross-platform support, there are two common strategies:

The first is to abstract a unified `IoMultiplexer` interface with different implementations per platform—epoll on Linux, kqueue on macOS, IOCP on Windows. This is the approach taken by libuv (the library underneath Node.js) and Boost.Asio.

The second is to use a higher-level abstraction—such as Boost.Asio's `io_context`, which has already encapsulated the platform differences for you. Since 1.13.0, Asio has provided C++20 coroutine support such as `awaitable<T>`, `use_awaitable`, and `co_spawn()` (and since 1.17.0, GCC 10's standard coroutine implementation is supported), so you can write cross-platform asynchronous code by combining `co_await` with Asio's asynchronous operations.

For learning purposes, epoll is more than enough for understanding the core concepts of I/O multiplexing. Once you have mastered the epoll + coroutines pattern, switching to kqueue or IOCP is merely a matter of API substitution.

## Where We Are

In this article we built the bridge between coroutines and the operating system's I/O multiplexing. Starting from the problems of blocking I/O, we saw why non-blocking I/O + polling does not work, then introduced I/O multiplexing (the select → poll → epoll evolution), with a close look at epoll's three APIs and the LT/ET triggering modes. Next, we connected coroutines and epoll—by storing the `coroutine_handle` inside `epoll_event.data`, we closed the loop of "epoll event notification → resume the corresponding coroutine". Finally, we used these components to build a minimal event loop that can accept TCP connections and read data.

But this event loop is still far from a complete server—it lacks graceful coroutine lifetime management, asynchronous write, error handling, timer support, and most importantly: a complete Echo Server. What the next article does is put all these puzzle pieces together and implement a fully functional coroutine-based Echo Server, so you can see what a coroutine network service that is "actually usable" looks like.

> 💡 The complete example code is in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP); browse to `code/volumn_codes/vol5/ch06-async-io-coroutine/`.

## References

- [epoll(7) — Linux man page](https://man7.org/linux/man-pages/man7/epoll.7.html) — the official epoll documentation, including a detailed explanation of LT/ET modes
- [The C10K problem — Dan Kegel](http://www.kegel.com/c10k.html) — the classic analysis of the "I/O multiplexing" problem, discussing the pros and cons of the various I/O models
- [Blocking I/O, Nonblocking I/O, And Epoll — Eli Klitzke](https://eklitzke.org/blocking-io-nonblocking-io-and-epoll) — a complete walkthrough from blocking I/O to non-blocking I/O to epoll
- [Coroutines (C++20) — cppreference](https://en.cppreference.com/cpp/language/coroutines) — the language specification for C++20 coroutines
- [From epoll to io_uring's Multishot Receives](https://codemia.io/blog/path/From-epoll-to-iourings-Multishot-Receives--Why-2025-Is-the-Year-We-Finally-Kill-the-Event-Loop) — discusses the evolution from epoll to io_uring and the future of the event loop model in 2025
- [io_uring vs epoll — kernel-internals.org](https://kernel-internals.org/io-uring/io-uring-vs-epoll/) — a feature comparison of epoll and io_uring
- [C++20 Coroutines: Sketching a Minimal Async Framework — Jeremy Ong](https://jeremyong.com/cpp/2021/01/04/cpp20-coroutines-a-minimal-async-framework/) — a hands-on reference for building a coroutine async framework from the ground up
