---
chapter: 10
cpp_standard:
- 20
description: 'Implement a minimal coroutine scheduler, and master the complete C++20
  coroutine chain from syntax to runtime: Task, Scheduler, timer, and epoll event
  loop.'
difficulty: advanced
order: 5
prerequisites:
- '卷五 ch06: 异步 I/O 与协程'
- 'Lab 3: Production-style Thread Pool'
reading_time_minutes: 14
tags:
- host
- cpp-modern
- coroutine
- advanced
title: 'Lab 4: Coroutine Scheduler and Event Loop'
translation:
  engine: anthropic
  source: documents/vol5-concurrency/exercises/04-coroutine-scheduler.md
  source_hash: e841fcf7c238a8184911e3e4eb7a06be95bb6f6320d0a31034190388aa665b33
  token_count: 3627
  translated_at: '2026-05-26T11:49:18.998481+00:00'
---
# Lab 4: Coroutine Scheduler and Event Loop

## Objectives

The thread pool in Lab 3 represents "task-level" concurrency—each task is a complete function call that exclusively occupies a thread from start to finish. In this lab, we dive into finer-grained concurrency: coroutines. A coroutine can suspend at a certain point, yielding execution back to the scheduler, and resume when conditions are met. This means a single thread can take turns executing multiple coroutines—instead of one task per thread, we have one thread running multiple "half-finished" tasks.

We will build a minimal coroutine scheduler: starting with manual scheduling and `yield`, then adding timers, and finally integrating epoll on Linux/WSL2 to implement a coroutine echo server. This lab is a core advanced project in Volume Five—it pushes your understanding of C++20 coroutines from "syntax" to "runtime."

## Prerequisites

Before starting, make sure you have read the following chapters:

- **ch06-01**: Async programming evolution — the motivation from callbacks to coroutines
- **ch06-02**: C++20 coroutine basics — `co_await`, `co_return`, `promise_type`
- **ch06-03**: promise_type and awaitable — the complete mechanism for custom awaitables
- **ch06-04**: Async I/O and event loops — the epoll/kqueue event-driven model
- **ch06-05**: Coroutines in action: echo server — a complete coroutine networking application
- **Lab 3**: Shutdown semantics design for thread pools (reference for this lab's shutdown design)

## Environment Setup

This lab requires C++20 and a Linux/WSL2 environment.

- **Compiler**: GCC 12+ or Clang 15+ (for full coroutine support)
- **Platform**: Linux or WSL2 (required for the epoll milestone)
- **CMake**: 3.14+

```cmake
cmake_minimum_required(VERSION 3.14)
project(lab4_coroutine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
)
FetchContent_MakeAvailable(Catch2)

add_executable(lab4_tests tests/main.cpp)
target_link_libraries(lab4_tests PRIVATE Catch2::Catch2WithMain)
```

## Final Interfaces

### `Task<T>` — Coroutine task wrapper (Milestone 1, move-only)

Internally defines `promise_type`, which must implement the following callbacks:

| promise_type Method | Return Type | Description | Milestone |
|---------------------|-------------|-------------|-----------|
| get_return_object | `Task<T>` | Creates the Task object | MS1 |
| initial_suspend | `std::suspend_always` | Lazy mode; does not auto-execute on creation | MS1 |
| final_suspend | `std::suspend_always` | Does not auto-destroy the frame on completion | MS1 |
| return_value | `void` | Stores the `co_return` value | MS1 |
| unhandled_exception | `void` | Stores the exception (`std::exception_ptr`) | MS1 |

Member variables:

| Type | Member | Semantics |
|------|--------|-----------|
| `coroutine_handle<promise_type>` | `handle_` | Coroutine handle |

Interface:

| Method | Signature | Description | Milestone |
|--------|-----------|-------------|-----------|
| Constructor | `Task(handle_type)` | Accepts a coroutine handle | MS1 |
| Destructor | `~Task()` | Destroys the coroutine frame | MS1 |
| get | `T get()` | Retrieves the result or rethrows the exception | MS1 |

### `Scheduler` — Coroutine scheduler (Milestone 2)

Member variables:

| Type | Member | Semantics |
|------|--------|-----------|
| `std::queue<coroutine_handle<>>` | `ready_queue_` | Ready coroutine queue |

Interface:

| Method | Signature | Description | Milestone |
|--------|-----------|-------------|-----------|
| schedule | `void schedule(coroutine_handle<>)` | Adds a coroutine to the ready queue | MS2 |
| yield | `auto yield()` | Returns an awaitable, suspends and puts back in the queue | MS2 |
| run | `void run()` | Loops executing ready coroutines until the queue is empty | MS2 |
| has_work | `bool has_work() const` | Checks if there are pending coroutines | MS2 |

### `SleepAwaiter` — sleep_for awaitable (Milestone 3)

| Method | Signature | Description | Milestone |
|--------|-----------|-------------|-----------|
| await_ready | `bool await_ready() noexcept` | Returns false (always suspends) | MS3 |
| await_suspend | `void await_suspend(coroutine_handle<>)` | Registers with the timer heap | MS3 |
| await_resume | `void await_resume() noexcept` | No-op on resume | MS3 |

### `EventLoop` — epoll event loop (Milestone 4, Linux/WSL2)

Member variables:

| Type | Member | Semantics |
|------|--------|-----------|
| `int` | `epoll_fd_` | epoll instance file descriptor |
| `bool` | `running_` | Running flag |

Interface:

| Method | Signature | Description | Milestone |
|--------|-----------|-------------|-----------|
| read | `auto read(int fd, void* buf, size_t size)` | Registers a read event, returns an awaitable | MS4 |
| write | `auto write(int fd, const void* buf, size_t size)` | Registers a write event, returns an awaitable | MS4 |
| accept | `auto accept(int listen_fd)` | Registers an accept event, returns an awaitable | MS4 |
| run | `void run()` | Event loop main loop | MS4 |
| stop | `void stop()` | Stops the event loop | MS4 |

## Milestone 1: Task<void> and Basic Coroutines

### Objective

Implement the `promise_type` of `Task<T>`, including `initial_suspend`, `final_suspend`, `return_value`, and `unhandled_exception`. We first implement the specialization for `Task<void>`, then extend it to `Task<T>`.

### Why

`Task` is the base currency of a coroutine scheduler—all coroutine functions return `Task`, and the scheduler manages coroutine suspension and resumption through the `coroutine_handle` inside `Task`. `promise_type` defines the behavior at key points in the coroutine lifecycle: what to do on creation (`initial_suspend`), what to do on return (`return_value`), what *not* to do on completion (`final_suspend`), and what to do on exception (`unhandled_exception`). Once you understand these four callbacks, you understand the C++20 coroutine runtime model.

### Implementation Guide

The core responsibility of `promise_type` is to inject custom logic at various lifecycle nodes of the coroutine.

`initial_suspend` returns `std::suspend_always`—meaning the coroutine suspends before the function body begins executing, so it does not run automatically. This is the hallmark of a "lazy" task—the coroutine does nothing after creation until someone explicitly `resume`s it. The opposite is `std::suspend_never` (an "eager" task that executes immediately on creation). We choose lazy because the scheduler needs control over "when to start executing."

`final_suspend` returns `std::suspend_always`—the coroutine suspends after reaching `co_return` and does not auto-destroy the coroutine frame. This prevents the frame from being destroyed before `get()` reads the result. The destructor of `Task` is responsible for destroying the frame.

`unhandled_exception` stores the exception (using `std::exception_ptr`), and rethrows it when `get()` is called.

Pitfall warning: If `final_suspend` returns `suspend_never`, the coroutine frame is automatically destroyed when the coroutine finishes. This seems convenient, but if the frame is destroyed before you call `get()`, accessing members of `promise_type` is undefined behavior (UB). Most educational implementations choose `suspend_always` plus `destroy()` in the destructor. Although this requires extra manual management, it is safer.

### Verification

```cpp
Task<int> simple_task()
{
    co_return 42;
}

Task<void> void_task()
{
    co_return;
}

TEST_CASE("Milestone 1: Task returns value",
          "[lab4][milestone1]")
{
    auto task = simple_task();
    // Task 是 lazy 的，不会自动执行
    // 需要手动 resume
    task.handle_.resume();
    REQUIRE(task.get() == 42);
}

TEST_CASE("Milestone 1: Task<void> compiles",
          "[lab4][milestone1]")
{
    auto task = void_task();
    task.handle_.resume();
    REQUIRE_NOTHROW(task.get());
}

Task<int> throwing_task()
{
    throw std::runtime_error("coroutine error");
    co_return 0;
}

TEST_CASE("Milestone 1: exception propagates through get",
          "[lab4][milestone1]")
{
    auto task = throwing_task();
    task.handle_.resume();
    REQUIRE_THROWS_AS(task.get(), std::runtime_error);
}
```cpp

## Milestone 2: Scheduler and yield

### Objective

Implement `Scheduler`, maintaining a ready queue that supports `schedule` (enqueue) and `yield` (suspend the current coroutine and put it back in the queue). `run()` loops to dequeue coroutines and resume them until the queue is empty.

### Why

With `Task`, we have executable units that can suspend and resume. But without a scheduler, the execution order of coroutines is entirely manual—who `resume`s whom, and when to `resume`. `Scheduler` automates this orchestration: all coroutines enter the ready queue, and the scheduler executes them in FIFO order. `yield` yields execution to other coroutines—this is the core of "cooperative multitasking."

### Implementation Guide

The data structure for `Scheduler` is very simple—a `std::queue<std::coroutine_handle<>>`. `schedule` puts the handle into the queue. `run` loops to dequeue handles and `resume` them.

`yield` is an awaitable whose `await_suspend` puts the current coroutine's handle back into the ready queue and returns `true` (indicating suspension). This way, the scheduler will pick up this coroutine and resume it in the next loop iteration.

```

auto yield() {
    struct YieldAwaiter {
        Scheduler& sched;

        bool await_ready() { return false; }
        // 总是挂起

        void await_suspend(coroutine_handle<> handle) {
            sched.schedule(handle);
            // 放回队列
        }

        void await_resume() {}
    };
    return YieldAwaiter{*this};
}

```cpp

Pitfall warning: `run()` cannot be a simple `while (!queue.empty())`, because a coroutine might add new coroutines to the queue during `await_suspend`. You need to ensure `run()` keeps looping until the queue is empty and no coroutines are currently executing. A simple approach is: `while (!queue_.empty()) { auto h = queue_.front(); queue_.pop(); h.resume(); }`.

### Verification

```cpp
Scheduler sched;

Task<void> ping(int id, int rounds)
{
    for (int i = 0; i < rounds; ++i) {
        // yield 让出执行权
        co_await sched.yield();
    }
    co_return;
}

TEST_CASE("Milestone 2: scheduler runs multiple coroutines",
          "[lab4][milestone2]")
{
    Scheduler sched;
    std::vector<std::string> log;

    auto make_task = [&](int id) -> Task<void> {
        for (int i = 0; i < 3; ++i) {
            log.push_back(
                std::to_string(id) + "-" + std::to_string(i));
            co_await sched.yield();
        }
    };

    sched.schedule(make_task(1));
    sched.schedule(make_task(2));
    sched.run();

    // 验证交替执行
    REQUIRE(log.size() == 6);
    // 日志应该是交错的: 1-0, 2-0, 1-1, 2-1, 1-2, 2-2
}

TEST_CASE("Milestone 2: scheduler drains all work",
          "[lab4][milestone2]")
{
    Scheduler sched;
    std::atomic<int> counter{0};

    auto make_task = [&]() -> Task<void> {
        counter.fetch_add(1);
        co_await sched.yield();
        counter.fetch_add(1);
    };

    sched.schedule(make_task());
    sched.schedule(make_task());
    sched.run();

    REQUIRE(counter.load() == 4);
    REQUIRE_FALSE(sched.has_work());
}
```

## Milestone 3: sleep_for and Timer Heap

### Objective

Implement the `sleep_for(duration)` awaitable. The scheduler maintains a timer heap (min-heap) and moves coroutines back to the ready queue when they expire.

### Why

`yield` makes a coroutine immediately yield execution, but often we need to "yield and resume after a period of time"—such as polling intervals, timeout waits, or animation frame rate control. `sleep_for` is the most basic timed awaitable. Its implementation introduces the scheduler's first "non-immediate" event source—instead of returning to the ready queue right away, the coroutine waits in the timer heap for a while.

### Implementation Guide

`await_suspend` of `SleepAwaiter` does two things: calculates the wake-up time (`steady_clock::now() + duration`), and puts the `(时间点, handle)` into the timer heap. `await_ready` returns false (always suspends).

The scheduler's `run()` loop needs to be modified—each time a task is fetched, it first checks whether the smallest element in the timer heap has expired. If it has, it removes it from the heap and puts it into the ready queue. If it has not expired and the ready queue is empty, `sleep` until the nearest timer expires.

Pseudocode:

```cpp
void run() {
    while (!ready_queue_.empty() || !timers_.empty()) {
        // 1. 处理到期的 timer
        while (!timers_.empty() &&
               timers_.top().deadline <= now()) {
            auto& t = timers_.top();
            ready_queue_.push(t.handle);
            timers_.pop();
        }

        // 2. 执行就绪协程
        if (!ready_queue_.empty()) {
            auto h = ready_queue_.front();
            ready_queue_.pop();
            h.resume();
        }
        else if (!timers_.empty()) {
            // 等到最近一个 timer 到期
            sleep_until(timers_.top().deadline);
        }
    }
}
```

Pitfall warning: Do not create a separate thread for each `sleep_for` to handle timing—that regresses to the "one task per thread" model. The design goal of the timer heap is for all timers to share a single thread, using a min-heap to efficiently find the nearest expiration time. Additionally, `std::priority_queue` is a max-heap by default, so you need a custom comparator to keep the smallest element at the top.

### Verification

```cpp
TEST_CASE("Milestone 3: sleep_for delays execution",
          "[lab4][milestone3]")
{
    Scheduler sched;
    std::vector<std::string> log;

    auto timed_task = [&](int id) -> Task<void> {
        log.push_back(std::to_string(id) + "-start");
        co_await sleep_for(std::chrono::milliseconds(50));
        log.push_back(std::to_string(id) + "-end");
    };

    auto start = std::chrono::steady_clock::now();
    sched.schedule(timed_task(1));
    sched.schedule(timed_task(2));
    sched.run();
    auto elapsed = std::chrono::steady_clock::now() - start;

    // 两个 task 各 sleep 50ms，并行执行
    // 总耗时应该接近 50ms 而不是 100ms
    REQUIRE(elapsed < std::chrono::milliseconds(100));

    REQUIRE(log.size() == 4);
}

TEST_CASE("Milestone 3: timer respects order",
          "[lab4][milestone3]")
{
    Scheduler sched;
    std::vector<int> order;

    auto timed = [&](int id, int ms) -> Task<void> {
        co_await sleep_for(std::chrono::milliseconds(ms));
        order.push_back(id);
    };

    sched.schedule(timed(1, 50));
    sched.schedule(timed(2, 20));
    sched.schedule(timed(3, 30));
    sched.run();

    REQUIRE(order == std::vector<int>{2, 3, 1});
}
```cpp

## Milestone 4: epoll Event Loop

### Objective

Implement an epoll-based event loop on Linux/WSL2, supporting read/write/accept awaitables for non-blocking fds.

### Why

Timers allow coroutines to resume after a specified time, but true async programming requires waiting for "I/O events to be ready"—a socket becoming readable, a socket becoming writable, or a new connection arriving. epoll is Linux's efficient I/O multiplexing mechanism; it allows a single thread to monitor state changes on multiple fds simultaneously, waking up waiting coroutines when an fd is ready. By integrating epoll into the scheduler, we get a complete "coroutine + I/O" runtime.

### Implementation Guide

Core idea: each I/O awaitable registers the `(fd, 事件类型, handle)` with epoll in `await_suspend`. When epoll reports that the fd is ready, it puts the corresponding handle back into the ready queue.

Pseudocode for the read awaitable:

```

struct ReadAwaiter {
    int fd;
    void* buffer;
    size_t size;
    EventLoop& loop;

    bool await_ready() {
        // 尝试非阻塞读取
        // 如果 EAGAIN → 返回 false，需要等待
    }

    void await_suspend(coroutine_handle<> handle) {
        // 注册 fd 到 epoll，关注 EPOLLIN
        // 存储 handle 以便后续恢复
        epoll_event ev;
        ev.events = EPOLLIN | EPOLLET;  // 边缘触发
        ev.data.ptr = handle.address();
        epoll_ctl(loop.epoll_fd_, EPOLL_CTL_ADD, fd, &ev);
    }

    size_t await_resume() {
        // 返回实际读取的字节数
        return bytes_read;
    }
};

```cpp

The scheduler's `run()` loop needs to be extended again—while handling timers and the ready queue, it must also call `epoll_wait` to check for I/O events:

```

void run() {
    while (running_) {
        // 1. 处理到期的 timer
        process_timers();

        // 2. 处理就绪协程
        process_ready_queue();

        // 3. epoll_wait 等待 I/O 事件
        int timeout = calculate_next_timeout();
        int n = epoll_wait(epoll_fd_, events, kMaxEvents,
                           timeout);
        for (int i = 0; i < n; ++i) {
            auto handle = coroutine_handle<>::from_address(
                events[i].data.ptr);
            ready_queue_.push(handle);
        }
    }
}

```cpp

Pitfall warning: In edge-triggered (EPOLLET) mode, `epoll_wait` reports an fd's state change only once. If you do not read all the data, the next `epoll_wait` will not report it again. Therefore, `await_resume` should loop reading until `EAGAIN`. Additionally, `EINTR` (interrupted by a signal) is not an error and should trigger a retry of `epoll_wait`.

### Verification

```cpp
TEST_CASE("Milestone 4: epoll echo server",
          "[lab4][milestone4]")
{
    // 启动 echo server
    int listen_fd = create_listen_socket(8080);
    EventLoop loop;

    // 每个连接一个协程
    auto handle_connection = [&](int fd) -> Task<void> {
        char buffer[1024];
        while (true) {
            auto n = co_await loop.read(fd, buffer, sizeof(buffer));
            if (n <= 0) break;  // 连接关闭
            co_await loop.write(fd, buffer, n);
        }
        close(fd);
    };

    auto accept_loop = [&]() -> Task<void> {
        while (true) {
            int client_fd = co_await loop.accept(listen_fd);
            if (client_fd < 0) break;
            loop.schedule(handle_connection(client_fd));
        }
    };

    loop.schedule(accept_loop());

    // 在另一个线程中运行客户端测试
    JoiningThread client([&]() {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(100));
        int sock = connect_to("127.0.0.1", 8080);
        send(sock, "hello", 5, 0);
        char buf[16];
        recv(sock, buf, 5, 0);
        buf[5] = '\0';
        REQUIRE(std::string(buf) == "hello");
        close(sock);
        loop.stop();
    });

    loop.run();
    close(listen_fd);
}
```

## Milestone 5: Coroutine Echo Server

### Objective

Combine the components from Milestones 1–4 to implement a complete coroutine echo server. It should support multiple concurrent connections, client disconnect detection, and graceful shutdown.

### Why

The echo server is the "Hello World" of network programming. Implementing it with coroutines looks almost identical to the synchronous version—a sequential read/write loop—but under the hood, it is asynchronous and non-blocking, with a single thread handling multiple connections. This is the power of coroutines: writing synchronous-style code while achieving asynchronous performance.

### Implementation Guide

The complete logic of the echo server is already demonstrated in the Milestone 4 tests. The focus of this milestone is adding error handling and graceful shutdown:

- Handle `EAGAIN`, `EINTR`, connection closures (read returning 0), and partial writes
- `stop()` closes the listen fd and waits for all established connections to finish processing
- Coroutine exceptions should not affect other connections—each connection's coroutine should have its own try-catch block

### Verification

The verification for this milestone is end-to-end testing—start the server, connect with multiple clients concurrently, send data, verify that the echoed data is correct, and then shut down gracefully.

## Self-Check List

- [ ] The four key callbacks of `promise_type` in `Task<T>` are implemented correctly
- [ ] Multiple coroutines can execute alternately in `Scheduler`
- [ ] After `yield` yields execution, other coroutines can continue running
- [ ] The timing accuracy of `sleep_for` is within an acceptable range (±10ms)
- [ ] The timer heap correctly handles coroutines with different expiration times
- [ ] The epoll event loop correctly handles read/write/accept
- [ ] The echo server can handle multiple concurrent connections
- [ ] Coroutine frames are destroyed after coroutines finish, with no memory leaks
- [ ] The exception handling strategy is clear, and exceptions are not silently lost
- [ ] You can explain the design considerations of `initial_suspend` returning `suspend_always`
- [ ] You can explain the difference between edge-triggered and level-triggered modes, and their impact on the code
- [ ] You can explain how the I/O awaiter correctly handles `EAGAIN` and `EINTR`
