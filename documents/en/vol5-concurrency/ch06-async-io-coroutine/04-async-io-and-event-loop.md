---
chapter: 6
cpp_standard:
- 20
description: Understand how I/O multiplexing (epoll/io_uring) works, build a coroutine-driven
  event loop, and complete the final mile of asynchronous I/O.
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
title: Asynchronous I/O and Event Loops
translation:
  source: documents/vol5-concurrency/ch06-async-io-coroutine/04-async-io-and-event-loop.md
  source_hash: bae752d9cfe97a1413c7289d01ffa48971de060a1f4bd6d11a7adc83155f6c26
  translated_at: '2026-06-16T04:06:21.944967+00:00'
  engine: anthropic
  token_count: 4819
---
# Asynchronous I/O and the Event Loop

Previously, we figured out the internal mechanisms of C++20 coroutines—the coroutine frame controls the lifecycle, the awaiter/awaitable controls suspension and resumption, and the scheduler manages execution timing via the coroutine handle. But to be honest, the scheduler we've written so far is just a "ready queue"—it doesn't know what it means to "wait for data to arrive," "wait for a network connection to be ready," or "wait for a timer to expire."

Coroutines themselves don't solve I/O problems—they are merely a control flow tool. What truly makes asynchronous I/O efficient is the I/O multiplexing mechanism provided by the operating system. The goal of this article is to connect coroutines with the OS's I/O multiplexing mechanism to build an event loop capable of handling real network I/O.

## Environment Setup

Starting from this article, we officially enter the domain of Linux-specific features. All code related to I/O multiplexing here relies on Linux's epoll API and cannot be compiled or run directly on Windows or macOS. Our test environment is Linux 2.6+ (epoll has been available since kernel 2.6; if you are interested in io_uring, you need 5.1+), using GCC 13+ or Clang 17+, with the compiler flag `-std=c++20 -Wall -Wextra -g`. Note that epoll is a Linux-specific API—macOS uses kqueue, and Windows uses IOCP. While the concepts are consistent, the APIs are completely different. We will briefly mention solutions for other platforms later.

## Blocking I/O vs. Non-blocking I/O

Before discussing I/O multiplexing, we need to clarify what "blocking" and "non-blocking" actually mean at the system call level.

In Unix/Linux, by default, all file descriptors (fds) are in blocking mode. When you call `recv` (or `read`) on a TCP socket, if the receive buffer is empty, the system call puts the current thread to **sleep** until data arrives (or the connection closes or an error occurs). This behavior is known as "blocking I/O."

Blocking I/O is fine for single-connection scenarios—you send a request, wait for a response, process it, and repeat. But when you need to handle thousands of connections simultaneously, problems arise: if no data arrives on one connection, the entire thread gets stuck, and all other connections are left waiting. Since one thread can only handle one blocking connection, handling 10,000 connections would require 10,000 threads—which is clearly unsustainable.

> The first time I wrote a high-concurrency network service, I fell into this trap—one thread per connection. As the number of connections grew, the overhead of thread switching exceeded the cost of actual work, with the CPU busy doing nothing but context switching.

The first step to a solution is setting the socket to non-blocking mode:

```cpp
// Set socket to non-blocking mode
int flags = fcntl(fd, F_GETFL, 0);
fcntl(fd, F_SETFL, flags | O_NONBLOCK);
```

In non-blocking mode, the behavior of `recv` changes completely: if the buffer is empty, `recv` doesn't sleep. Instead, it returns immediately with `-1` and sets `errno` to `EAGAIN` (or `EWOULDBLOCK`—on Linux, they are the same value). This tells you, "No data available right now, try again later."

This sounds good, but the question arises: what do you do after you get `EAGAIN`?

The most naive approach is polling—writing a tight loop that keeps calling `recv` until data arrives. But this causes the CPU to spin at 100%, doing nothing useful and simply wasting power. Polling is the worst of all solutions—it wastes CPU resources and doesn't guarantee timely response (data might arrive 0.1ms after the last `EAGAIN`, but your loop might not call `recv` again for several milliseconds due to scheduling).

Is there a way to "sleep when there is no data and be woken up when data arrives"? This is exactly what I/O multiplexing does.

## I/O Multiplexing

The core idea of I/O multiplexing is simple: you hand a bunch of fds to the OS, telling it "I care about these events (readable, writable, exceptional) on these fds," and then you go to sleep. When an event you care about occurs on any of these fds, the OS wakes you up and says, "These fds are ready." You process them, hand the fds back, and go to sleep again. The cycle repeats.

This way, a single thread can efficiently manage thousands of connections—when there are no events, the thread sleeps quietly, consuming no CPU; when events arrive, the thread wakes up and handles the ready connections.

### From select to poll to epoll

I/O multiplexing on Linux has gone through three generations of evolution: `select` → `poll` → `epoll`.

`select` is the oldest solution (POSIX standard, supported on all Unix). Its interface works roughly like this: you pass it three `fd_set`s (read, write, exception), each being a bit array where every bit represents an fd. `select` can monitor at most 1024 fds (defined by `FD_SETSIZE`), and every call requires copying the entire `fd_set` from user space to kernel space and back again—when the number of fds is large, this copying overhead is significant. Worse, upon return, you don't know which fds are ready; you must iterate through the entire `fd_set` to check.

`poll` improved on some of `select`'s issues—it uses an array of `struct pollfd` instead of bit arrays, removing the 1024 fd limit. But the core problem remains: every call still copies all fd information from user space to kernel space, and you still have to iterate through all fds upon return.

The real revolution was `epoll` (introduced in Linux 2.5.44). epoll splits "registering fds" and "waiting for events" into two steps: you first use `epoll_ctl` to register the fds you care about into the kernel (internally, the kernel maintains a red-black tree, so insertions, deletions, and queries are O(log n)), and then you repeatedly call `epoll_wait` to wait for events. The kernel only returns fds that are **actually ready**, eliminating the need for iteration. In scenarios with a large number of fds but few active ones (typical for high-concurrency network servers), epoll vastly outperforms select/poll.

### The Three Core epoll APIs

There are only three system calls for epoll. Let's go through them one by one.

**`epoll_create1`** creates an epoll instance and returns an epoll fd. This fd acts as a "monitor"—you subsequently register the socket fds you want to monitor to this epoll fd. `epoll_create1` usually takes `EPOLL_CLOEXEC` (automatically closes the epoll fd on exec).

```cpp
int epfd = epoll_create1(EPOLL_CLOEXEC);
if (epfd == -1) {
    perror("epoll_create1");
    exit(1);
}
```

**`epoll_ctl`** is used to register, modify, or delete monitoring for a specific fd. `op` can be `EPOLL_CTL_ADD` (add), `EPOLL_CTL_MOD` (modify), or `EPOLL_CTL_DEL` (delete). `event` is an `epoll_event` structure containing the event types you care about and a `data` field (you can stuff any data in here; epoll doesn't interpret it and returns it to you unchanged).

```cpp
struct epoll_event ev;
ev.events = EPOLLIN; // Wait for input
ev.data.fd = listen_fd; // Store the fd in user data
if (epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev) == -1) {
    perror("epoll_ctl");
    exit(1);
}
```

**`epoll_wait`** is the one that does the actual work—it blocks waiting for events to occur on registered fds and returns the number of ready fds. `events` is an array you provide, which epoll fills with ready events. `timeout` is the timeout in milliseconds; `-1` means wait indefinitely.

```cpp
#define MAX_EVENTS 64
struct epoll_event events[MAX_EVENTS];
int nfds = epoll_wait(epfd, events, MAX_EVENTS, -1);
if (nfds == -1) {
    perror("epoll_wait");
    exit(1);
}
```

That's the entire epoll API—three calls, simple yet powerful.

### LT vs ET: Level-Triggered vs. Edge-Triggered

epoll has two trigger modes: Level-Triggered (LT, the default) and Edge-Triggered (ET, requires setting the `EPOLLET` flag).

The terms "level" and "edge" come from electronics—level-triggered means "trigger continuously while the level is high," while edge-triggered means "trigger only at the instant the level changes from low to high." In the context of epoll:

**LT Mode**: As long as there is data to read (or write) on the fd, `epoll_wait` will repeatedly notify you. It doesn't matter if you haven't read all the data; the next `epoll_wait` will tell you "this fd is still readable." LT mode is simpler and less error-prone.

**ET Mode**: Notifies you only once when the state of the fd changes—for example, the moment the buffer goes from "empty" to "has data." If you don't read all the data (until you get `EAGAIN`), `epoll_wait` won't notify you again until new data arrives. ET mode can reduce the number of `epoll_wait` returns (process all data at once), but the coding is more complex, and **you must use non-blocking I/O**, otherwise you might get blocked while reading in a loop.

> ⚠️ **ET mode requires non-blocking I/O.** Because ET mode requires you to read all data in one go (until `EAGAIN`), if the socket is blocking, the last `recv` will block when there is no data, freezing the entire event loop.

For most network applications, LT mode is sufficient and simpler to program. ET mode is suitable for scenarios with extreme performance requirements (like Nginx). Our examples will use LT mode.

### Solutions on Other Platforms

Briefly mentioning I/O multiplexing solutions on other operating systems, in case you need to work in a cross-platform environment. macOS and BSD systems use kqueue; the concept is similar to epoll but the API is slightly different. Nginx and Node.js on macOS both use kqueue at the bottom. On Windows, there is IOCP (I/O Completion Ports), which uses a "completion" model rather than a "readiness" model—you initiate an asynchronous operation, and the OS notifies you when it's done. This is fundamentally different from epoll's "readiness notification" model. Linux 5.1+ introduced the next-generation asynchronous I/O solution, io_uring. It uses shared memory ring buffers to submit and complete I/O operations, avoiding traditional system call overhead. It offers better performance than epoll but with higher API complexity and is still evolving rapidly.

Regarding io_uring, it's worth noting the fundamental difference from epoll: epoll is a reactor pattern (telling you "it's ready, go read/write yourself"), while io_uring is closer to a proactor pattern (you submit read/write requests, the kernel does them and notifies you "it's done" via a completion ring—though io_uring also supports a polling mode, so it's not strictly equivalent to classic proactor). io_uring generally outperforms epoll in high-concurrency scenarios because it reduces system call counts—you can batch multiple I/O operations into the submission ring buffer, and the kernel processes them in bulk, notifying you via the completion ring when done. However, epoll has a more mature ecosystem and richer documentation, so most production environments still use it. We chose epoll as the teaching vehicle because its concepts are more intuitive and the API is simpler.

## Event Loop Pattern

Before looking at the code, let's clarify what the "Event Loop" pattern actually is.

The core structure of an event loop is an infinite loop where each iteration does three things: first, check timers to see if any have expired; second, call `epoll_wait` (or other I/O multiplexing mechanism) to block waiting for ready fds; and third, dispatch events for each ready fd—calling the corresponding callback function or resuming the corresponding coroutine. The pseudocode looks roughly like this:

```text
while (running) {
    // 1. Check timers (not implemented in this article)
    // check_expired_timers();

    // 2. Wait for I/O events
    int nfds = epoll_wait(epfd, events, MAX_EVENTS, timeout);

    // 3. Dispatch events
    for (int i = 0; i < nfds; ++i) {
        if (events[i].data.fd == listen_fd) {
            // Accept new connections
        } else {
            // Read data from existing connections
        }
    }
}
```

This is the core pattern behind Node.js, Nginx, Redis, Chrome, and libuv. Of course, actual implementations are much more complex (handling signals, inter-thread communication, graceful shutdown, etc.), but the skeleton is this loop.

## Connecting Coroutines + epoll

Now we have coroutines (functions that can suspend and resume) and epoll (a system call that efficiently waits for I/O events). The problem is how to connect them.

The key insight mentioned at the end of the last article is: **the awaiter's `await_suspend` is the bridge for scheduler integration**. The flow is like this—when a coroutine `co_await`s an I/O operation (like `async_read`), the awaiter's `await_suspend` is called. It stores the coroutine's handle somewhere and registers the socket fd with epoll. Then `await_suspend` returns, the coroutine suspends, and control returns to the event loop. The event loop calls `epoll_wait` to block waiting for I/O events. When data arrives on the socket, `epoll_wait` returns, and the event loop retrieves the coroutine handle from `epoll_event.data.ptr`, calls `resume` to resume the coroutine, `async_read` returns the read data, and the coroutine continues execution after the `co_await` expression.

The key trick is: **storing the coroutine handle in `epoll_event.data`**. `data` is a union, which can store a `void*` pointer or an `int` fd. `std::coroutine_handle<>` can be safely converted to `void*` (via `address()`), and converted back from `void*` (via `from_address()`).

Now let's look at the concrete code implementation.

## A Minimal Event Loop Implementation

We will implement a minimal event loop capable of handling TCP accept + read. The entire implementation is about 200 lines of code but covers all the core concepts of coroutines + epoll.

### Step 1: Event Loop Skeleton

First, let's build a basic event loop class that encapsulates epoll creation, registration, waiting, and dispatching.

```cpp
class EventLoop {
public:
    EventLoop() {
        epfd_ = epoll_create1(EPOLL_CLOEXEC);
        if (epfd_ == -1) {
            perror("epoll_create1");
            exit(1);
        }
    }

    ~EventLoop() {
        close(epfd_);
    }

    // Register a coroutine to wait for read events on fd
    void wait_for_read(int fd, std::coroutine_handle<> handle) {
        struct epoll_event ev;
        ev.events = EPOLLIN; // Level-triggered, wait for read
        ev.data.ptr = handle.address(); // Store coroutine handle pointer
        if (epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ev) == -1) {
            perror("epoll_ctl: wait_for_read");
            exit(1);
        }
    }

    void run() {
        const int MAX_EVENTS = 64;
        struct epoll_event events[MAX_EVENTS];

        while (true) {
            int nfds = epoll_wait(epfd_, events, MAX_EVENTS, -1);
            if (nfds == -1) {
                perror("epoll_wait");
                exit(1);
            }

            for (int i = 0; i < nfds; ++i) {
                auto handle = std::coroutine_handle<>::from_address(events[i].data.ptr);
                handle.resume(); // Resume the coroutine
            }
        }
    }

private:
    int epfd_;
};
```

You will notice that the `wait_for_read` method simply stores the address of the `coroutine_handle` in `ev.data.ptr`. This is the most critical step in the design—it establishes a one-to-one mapping between epoll events and coroutines. When `epoll_wait` returns an event, we can directly recover the corresponding coroutine handle from `data.ptr` and `resume` it.

### Step 2: Coroutine Task Type

Next, define a coroutine task type whose promise type works with our event loop.

```cpp
struct Task {
    struct promise_type {
        Task get_return_object() { return {}; }
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { std::terminate(); }
    };
};
```

### Step 3: Asynchronous Accept

When a client connection arrives, we need to accept it. In the coroutine world, accept becomes an asynchronous operation—if there is no connection yet, the coroutine suspends, and it resumes when epoll notifies that the listen_fd is readable.

```cpp
struct AsyncAccept {
    int listen_fd;
    int client_fd = -1;

    bool await_ready() { return false; } // Always suspend

    bool await_suspend(std::coroutine_handle<> handle) {
        // Register listen_fd with epoll
        struct epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.ptr = handle.address();
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev) == -1) {
            perror("epoll_ctl: async_accept");
            return false; // Don't suspend if registration fails
        }
        return true; // Suspend
    }

    int await_resume() { return client_fd; }
};
```

There is a subtlety here: in `await_suspend`, we registered the epoll event, but we haven't called `accept` yet—because there is no new connection at this point. When epoll notifies that listen_fd is readable (meaning a new connection has arrived), the event loop resumes the coroutine, and only then does `await_resume` execute the real `accept`. This is much clearer than traditional callback-style code.

### Step 4: Asynchronous Read

The pattern for read is almost identical to accept—register with epoll first, then execute the real `recv` after data arrives.

```cpp
struct AsyncRead {
    int fd;
    char* buffer;
    size_t len;
    ssize_t nread = 0;

    bool await_ready() {
        // Fast path: try a non-blocking read first
        ssize_t n = recv(fd, buffer, len, MSG_DONTWAIT);
        if (n > 0) {
            nread = n;
            return true; // Data already available, don't suspend
        }
        return false; // Need to wait
    }

    bool await_suspend(std::coroutine_handle<> handle) {
        // Register fd with epoll
        struct epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.ptr = handle.address();
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
            perror("epoll_ctl: async_read");
            return false;
        }
        return true;
    }

    ssize_t await_resume() {
        if (nread > 0) return nread; // From fast path
        // Slow path: read now (data is ready)
        return recv(fd, buffer, len, 0);
    }
};
```

You will notice that in `await_ready`, we attempt a non-blocking `recv` first. If data has already arrived, we return immediately, skipping the overhead of registering with epoll and suspending/resuming. This demonstrates the value of `await_ready` as a "fast path optimization"—in most cases, if you can determine in advance whether an operation is complete, you should do so in `await_ready`.

### Step 5: Assemble Them Together

Now we have a complete event loop, a coroutine task type, async accept, and async read. Let's assemble them into a program that can accept TCP connections and read data.

```cpp
EventLoop loop;

Task handle_client(int client_fd) {
    char buffer[1024];
    while (true) {
        ssize_t n = co_await AsyncRead{client_fd, buffer, sizeof(buffer)};
        if (n <= 0) break; // Connection closed or error
        // Echo back (omitted for brevity, use async_write)
    }
    close(client_fd);
}

Task accept_loop(int listen_fd) {
    while (true) {
        int client_fd = co_await AsyncAccept{listen_fd};
        if (client_fd >= 0) {
            handle_client(client_fd); // Start client handler coroutine
        }
    }
}

int main() {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    // ... set listen_fd to non-blocking, bind, listen ...

    // Start accept loop coroutine
    accept_loop(listen_fd);

    // Start event loop
    loop.run();
}
```

Although this program is still rough (e.g., lifecycle management of the `handle_client` coroutine, lack of `async_write`, etc.), it is a working coroutine-based TCP server. Let's review the entire flow: `main` creates the listening socket, starts the accept loop coroutine, and enters the event loop. When the accept loop coroutine executes `co_await AsyncAccept`, there is no new connection yet, so the coroutine suspends and listen_fd is registered with epoll. The event loop blocks on `epoll_wait`. When a client connection arrives, epoll notifies that listen_fd is readable, and the event loop resumes the accept coroutine. The accept coroutine gets the client_fd, starts the `handle_client` coroutine to handle the connection, and goes back to `co_await AsyncAccept` to wait for the next connection. The `handle_client` coroutine executes `co_await AsyncRead`, client_fd is registered with epoll, and the coroutine suspends. When data arrives, epoll notifies that client_fd is readable, the event loop resumes the `handle_client` coroutine, which reads the data, echoes it back, and returns to `co_await AsyncRead` to wait for the next batch of data. Throughout this process, a single thread manages all connections—when there are no I/O events, the thread sleeps quietly on `epoll_wait`, and is woken up only to handle events.

> ⚠️ **There is a lifecycle management pitfall in this code.** The `Task` object returned by `handle_client` is destroyed at the end of each loop iteration, but `Task`'s destructor does nothing—`std::coroutine_handle<>` is a non-owning handle, and its destruction doesn't destroy the coroutine frame. This means the coroutine frame is never freed (memory leak). Since `Task::promise_type` returns `std::suspend_never` in `final_suspend`, the frame remains on the heap after the coroutine completes, and no one calls `destroy`. In production code, you need a more robust task management system to track all active coroutines—for example, storing all active coroutine handles in a container and calling `destroy` on them when the coroutine ends to release the frame and remove it from the container. We will address this issue in the Echo Server in the next article.

### A Subtle Issue in the Event Loop

You may have noticed a problem with the event loop above: after `epoll_wait` returns, we resume the coroutine, but the coroutine might call `epoll_ctl` again inside `await_suspend` to register new events. This means the interest list of epoll might be modified while resuming a coroutine—this is usually safe because modifications via `epoll_ctl` take effect at the next `epoll_wait`. However, if you modify the event for the same fd while resuming coroutines in the loop (e.g., first registering `EPOLLIN`, then changing to `EPOLLOUT` after the coroutine resumes), you need to be careful about ordering.

In LT mode, this is usually not a problem because LT is "level-triggered"—as long as you have unread data, the next `epoll_wait` will notify you again. However, in ET mode, if you modify the fd's registration while processing events, you might lose event notifications.

### The Value of await_ready's Fast Path

Looking back at our `AsyncRead`, `await_ready` attempts a non-blocking `recv` first. This design isn't redundant—in many scenarios, data may have already arrived (data is already in the TCP receive buffer), so there's no need for the full workflow of suspending the coroutine, registering with epoll, waiting for notification, and resuming the coroutine; just read directly. This fast path is crucial in high-performance scenarios because it saves at least one system call (`epoll_wait`) and two coroutine context switches.

## Cross-Platform Considerations

All the code above is based on Linux epoll. If you need cross-platform support, there are two common strategies:

The first is to abstract a unified `Reactor` interface, implemented differently on each platform—epoll on Linux, kqueue on macOS, and IOCP on Windows. This is the approach used by libuv (the underlying library of Node.js) and Boost.Asio.

The second is to use a higher-level abstraction—like Boost.Asio's `asio::awaitable`, which already encapsulates platform differences for you. Asio has provided C++20 coroutine support like `asio::awaitable`, `co_spawn`, and `this_coro::executor` since version 1.13.0 (supporting GCC 10's standard coroutine implementation since 1.17.0). You can use `co_await` with Asio's asynchronous operations to write cross-platform asynchronous code.

For learning purposes, epoll is sufficient to understand the core concepts of I/O multiplexing. Once you master the epoll + coroutine pattern, switching to kqueue or IOCP is just a matter of API replacement.

## Where We Are

In this article, we bridged the gap between coroutines and the OS's I/O multiplexing. Starting from the problem of blocking I/O, we saw why non-blocking I/O + polling isn't feasible, then introduced I/O multiplexing (the evolution from select to poll to epoll), focusing on epoll's three APIs and the two trigger modes, LT and ET. We then connected coroutines with epoll—by storing the coroutine handle in `epoll_event.data`, we achieved the closed loop of "epoll event notification → resume corresponding coroutine." Finally, we used these components to build a minimal event loop capable of accepting TCP connections and reading data.

But this event loop is far from a complete server—it lacks graceful coroutine lifecycle management, async write, error handling, timer support, and most importantly: a complete Echo Server. In the next article, we will put all these pieces together to implement a fully functional coroutine-based Echo Server, showing you what a "truly usable" coroutine network service looks like.

> 💡 Complete example code is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `src/coroutines/event_loop`.

## References

- [epoll(7) — Linux man page](https://man7.org/linux/man-pages/man7/epoll.7.html) — Official documentation for epoll, including detailed explanations of LT/ET modes
- [The C10K problem — Dan Kegel](http://www.kegel.com/c10k.html) — Classic article analyzing the "I/O multiplexing" problem, discussing pros and cons of various I/O models
- [Blocking I/O, Nonblocking I/O, And Epoll — Eli Klitzke](https://eklitzke.org/blocking-io-nonblocking-io-and-epoll) — Complete walkthrough from blocking I/O to non-blocking I/O to epoll
- [Coroutines (C++20) — cppreference](https://en.cppreference.com/cpp/language/coroutines) — Language specification for C++20 coroutines
- [From epoll to io_uring's Multishot Receives](https://codemia.io/blog/path/From-epoll-to-iourings-Multishot-Receives--Why-2025-Is-the-Year-We-Finally-Kill-the-Event-Loop) — Discussing the evolution from epoll to io_uring and the future of the event loop model in 2025
- [io_uring vs epoll — kernel-internals.org](https://kernel-internals.org/io-uring/io-uring-vs-epoll/) — Feature comparison between epoll and io_uring
- [C++20 Coroutines: Sketching a Minimal Async Framework — Jeremy Ong](https://jeremyong.com/cpp/2021/01/04/cpp20-coroutines-a-minimal-async-framework/) — Practical reference for building a coroutine async framework from scratch
