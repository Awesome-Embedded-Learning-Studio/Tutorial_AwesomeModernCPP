---
chapter: 10
cpp_standard:
- 20
description: 'Build a minimal coroutine scheduler and master the complete C++20 coroutine
  chain from syntax to runtime: Task, Scheduler, timers, and an epoll event loop'
difficulty: advanced
order: 5
prerequisites:
- 'Volume 5 ch06: Asynchronous I/O and Coroutines'
- 'Lab 3: Production-style Thread Pool'
reading_time_minutes: 14
tags:
- host
- cpp-modern
- coroutine
- advanced
title: 'Lab 4: Coroutine Scheduler and Event Loop'
translation:
  source: documents/vol5-concurrency/exercises/04-coroutine-scheduler.md
  source_hash: f4489aa0b592d583c51505dd2e9fcb064d32365d9cbf12c1a3597cbbe042e374
  translated_at: '2026-09-26T09:36:02+00:00'
  engine: anthropic
  token_count: 3000
---
# Lab 4: Coroutine Scheduler and Event Loop

## Objectives

The thread pool of Lab 3 is "task-level" concurrency—each task is one complete function call that owns a thread from start to finish. In this lab we move to finer-grained concurrency: coroutines. A coroutine can suspend at some point mid-execution, hand execution back to the scheduler, and resume once its condition is met. That means one thread can take turns running several coroutines—no longer one task per thread, but one thread juggling multiple "half-finished" tasks.

We are going to build a minimal coroutine scheduler: first manual scheduling and `yield`, then timers, and finally epoll on Linux/WSL2, ending with a coroutine echo server. This lab is the advanced core project of Volume 5—it pushes C++20 coroutines from "syntax understanding" to "runtime understanding".

## Prerequisites

Before you start, make sure you have finished the following chapters:

- **ch06-01**: The Evolution of Asynchronous Programming — the motivation from callbacks to coroutines
- **ch06-02**: C++20 Coroutine Fundamentals — `co_await`, `co_return`, `promise_type`
- **ch06-03**: promise_type and awaitable — the complete mechanism of custom awaitables
- **ch06-04**: Asynchronous I/O and Event Loops — the epoll/kqueue event-driven model
- **ch06-05**: Coroutine Echo Server in Practice — a complete coroutine network application
- **Lab 3**: the shutdown-semantics design of the thread pool (the reference for this lab's shutdown design)

## Environment Setup

This lab requires C++20 and a Linux/WSL2 environment.

- **Compiler**: GCC 12+ or Clang 15+ (full coroutine support)
- **Platform**: Linux or WSL2 (needed for the epoll milestone)
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

## Final Interface

### `Task<T>` — the coroutine task wrapper (Milestone 1, move-only)

Defines `promise_type` internally, with the following callbacks to implement:

| promise_type method | Return type | Description | Milestone |
|-------------------|----------|------|-----------|
| get_return_object | `Task<T>` | Creates the Task object | MS1 |
| initial_suspend | `std::suspend_always` | Lazy mode: no automatic execution after creation | MS1 |
| final_suspend | `std::suspend_always` | Does not destroy the frame automatically at the end | MS1 |
| return_value | `void` | Stores the value from `co_return` | MS1 |
| unhandled_exception | `void` | Stores the exception (`std::exception_ptr`) | MS1 |

Member variables:

| Type | Member | Semantics |
|------|------|------|
| `coroutine_handle<promise_type>` | `handle_` | The coroutine handle |

Interface:

| Method | Signature | Description | Milestone |
|------|------|------|-----------|
| Constructor | `Task(handle_type)` | Takes a coroutine handle | MS1 |
| Destructor | `~Task()` | Destroys the coroutine frame | MS1 |
| get | `T get()` | Fetches the result or rethrows the exception | MS1 |

### `Scheduler` — the coroutine scheduler (Milestone 2)

Member variables:

| Type | Member | Semantics |
|------|------|------|
| `std::queue<coroutine_handle<>>` | `ready_queue_` | Queue of ready coroutines |

Interface:

| Method | Signature | Description | Milestone |
|------|------|------|-----------|
| schedule | `void schedule(coroutine_handle<>)` | Puts a coroutine into the ready queue | MS2 |
| yield | `auto yield()` | Returns an awaitable that suspends and re-queues | MS2 |
| run | `void run()` | Loops over ready coroutines until the queue is empty | MS2 |
| has_work | `bool has_work() const` | Whether any coroutines are pending | MS2 |

### `SleepAwaiter` — the awaitable for sleep_for (Milestone 3)

| Method | Signature | Description | Milestone |
|------|------|------|-----------|
| await_ready | `bool await_ready() noexcept` | Returns false (always suspends) | MS3 |
| await_suspend | `void await_suspend(coroutine_handle<>)` | Registers with the timer heap | MS3 |
| await_resume | `void await_resume() noexcept` | No-op on resume | MS3 |

### `EventLoop` — the epoll event loop (Milestone 4, Linux/WSL2)

Member variables:

| Type | Member | Semantics |
|------|------|------|
| `int` | `epoll_fd_` | File descriptor of the epoll instance |
| `bool` | `running_` | The running flag |

Interface:

| Method | Signature | Description | Milestone |
|------|------|------|-----------|
| read | `auto read(int fd, void* buf, size_t size)` | Registers a read event and returns an awaitable | MS4 |
| write | `auto write(int fd, const void* buf, size_t size)` | Registers a write event and returns an awaitable | MS4 |
| accept | `auto accept(int listen_fd)` | Registers an accept event and returns an awaitable | MS4 |
| run | `void run()` | The main loop of the event loop | MS4 |
| stop | `void stop()` | Stops the event loop | MS4 |

## Milestone 1: Task<void> and Basic Coroutines

### Objectives

Implement the `promise_type` of `Task<T>`, covering `initial_suspend`, `final_suspend`, `return_value`, and `unhandled_exception`. Start with the `Task<void>` specialization, then extend to `Task<T>`.

### Why

`Task` is the base currency of a coroutine scheduler—every coroutine function returns a `Task`, and the scheduler manages suspension and resumption through the `coroutine_handle` inside the `Task`. `promise_type` defines the behavior at each key point of the coroutine's lifecycle: what happens at creation (`initial_suspend`), what happens on return (`return_value`), what does *not* happen at the end (`final_suspend`), and what happens on an exception (`unhandled_exception`). Once these four callbacks make sense to you, you understand the runtime model of C++20 coroutines.

### Implementation Guide

The core responsibility of `promise_type` is to insert custom logic at each lifecycle point of the coroutine.

`initial_suspend` returns `std::suspend_always`—the coroutine suspends before the function body even starts, so it never runs on its own. This is the hallmark of a "lazy" task: after creation the coroutine does nothing until someone explicitly `resume`s it. The opposite is `std::suspend_never` (an "eager" task that starts executing right after creation). We pick lazy because the scheduler needs to control *when* execution begins.

`final_suspend` returns `std::suspend_always`—the coroutine suspends after reaching `co_return` instead of destroying its coroutine frame automatically. This prevents the frame from being destroyed before `get()` has read the result. The destructor of `Task` is responsible for destroying the frame.

`unhandled_exception` stores the exception (in a `std::exception_ptr`), and `get()` rethrows it.

Pitfall warning: if `final_suspend` returns `suspend_never`, the coroutine frame is destroyed automatically when the coroutine finishes. That looks convenient, but if the frame is destroyed before `get()`, accessing the members of `promise_type` is UB. Most educational implementations choose `suspend_always` plus `destroy()` in the destructor—one extra bit of manual management, but safer.

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
    // Task is lazy: it does not run automatically
    // We have to resume it manually
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
```

## Milestone 2: Scheduler and yield

### Objectives

Implement `Scheduler`: it maintains a ready queue and supports `schedule` (enqueue) and `yield` (suspend the current coroutine and put it back in the queue). `run()` loops taking coroutines out of the queue and resuming them, until the queue is empty.

### Why

With `Task`, we have a unit of execution that can suspend and resume. But without a scheduler, the order of execution is entirely manual—who `resume`s whom, and when. `Scheduler` automates this orchestration: every coroutine enters the ready queue, and the scheduler runs them in FIFO order. `yield` hands execution to the other coroutines—and that is the heart of "cooperative multitasking".

### Implementation Guide

The data structure of `Scheduler` is simple—a `std::queue<std::coroutine_handle<>>`. `schedule` puts a handle into the queue; `run` pops handles in a loop and `resume`s them.

`yield` is an awaitable whose `await_suspend` puts the current coroutine's handle back into the ready queue and returns `true` (meaning "suspend"). The scheduler then picks this coroutine up again on its next trip around the loop.

```cpp

auto yield() {
    struct YieldAwaiter {
        Scheduler& sched;

        bool await_ready() { return false; }
        // Always suspend

        void await_suspend(coroutine_handle<> handle) {
            sched.schedule(handle);
            // Put it back in the queue
        }

        void await_resume() {}
    };
    return YieldAwaiter{*this};
}

```

Pitfall warning: `run()` cannot be a simple `while (!queue.empty())`, because a coroutine may add new coroutines to the queue from inside `await_suspend`. You need `run()` to keep looping until the queue is empty and no coroutine is executing. One simple approach: `while (!queue_.empty()) { auto h = queue_.front(); queue_.pop(); h.resume(); }`.

### Verification

```cpp
Scheduler sched;

Task<void> ping(int id, int rounds)
{
    for (int i = 0; i < rounds; ++i) {
        // yield hands over execution
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

    // Verify interleaved execution
    REQUIRE(log.size() == 6);
    // The log should be interleaved: 1-0, 2-0, 1-1, 2-1, 1-2, 2-2
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

## Milestone 3: sleep_for and the timer heap

### Objectives

Implement a `sleep_for(duration)` awaitable. The scheduler maintains a timer heap (a min-heap) and moves coroutines back into the ready queue once their timers expire.

### Why

`yield` makes a coroutine give up execution immediately, but very often what we need is "give up and resume after a while"—polling intervals, timeout waits, animation frame-rate control. `sleep_for` is the most basic timed awaitable, and implementing it introduces the scheduler's first "non-immediate" event source: the coroutine does not go straight back to the ready queue, but first waits in the timer heap for a while.

### Implementation Guide

`SleepAwaiter`'s `await_suspend` does two things: compute the wake-up time point (`steady_clock::now() + duration`), and put the `(time_point, handle)` pair into the timer heap. `await_ready` returns false (always suspend).

The scheduler's `run()` loop now needs a change—each time it goes to fetch work, first check whether the smallest element of the timer heap has expired. If it has, pop it from the heap and put it into the ready queue. If it has not, and the ready queue is empty, `sleep` until the nearest timer expires.

Pseudocode:

```cpp
void run() {
    while (!ready_queue_.empty() || !timers_.empty()) {
        // 1. Handle expired timers
        while (!timers_.empty() &&
               timers_.top().deadline <= now()) {
            auto& t = timers_.top();
            ready_queue_.push(t.handle);
            timers_.pop();
        }

        // 2. Run the ready coroutines
        if (!ready_queue_.empty()) {
            auto h = ready_queue_.front();
            ready_queue_.pop();
            h.resume();
        }
        else if (!timers_.empty()) {
            // Sleep until the nearest timer expires
            sleep_until(timers_.top().deadline);
        }
    }
}
```

Pitfall warning: do not create a dedicated thread per `sleep_for` to keep time—that drags us right back to the "one task, one thread" model. The design goal of the timer heap is for all timers to share a single thread, using a min-heap to find the nearest expiry efficiently. Also, `std::priority_queue` is a max-heap by default; you need a custom comparator so the smallest element ends up on top.

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

    // Both tasks sleep 50ms each and run concurrently
    // Total elapsed should be close to 50ms, not 100ms
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
```

## Milestone 4: epoll Event Loop

### Objectives

Implement an epoll-based event loop on Linux/WSL2, supporting read/write/accept awaitables for non-blocking fds.

### Why

Timers let a coroutine resume at a specified time, but what true asynchronous programming waits for is "I/O event readiness"—a socket becomes readable, a socket becomes writable, a new connection arrives. epoll is Linux's efficient I/O multiplexing mechanism: it lets a single thread watch many fds for state changes at once, waking the waiting coroutine when an fd is ready. Integrate epoll into the scheduler, and we have a complete "coroutine + I/O" runtime.

### Implementation Guide

The core idea: every I/O awaitable registers `(fd, event type, handle)` with epoll in `await_suspend`; when epoll reports the fd ready, the corresponding handle goes back into the ready queue.

Pseudocode for the read awaitable:

```cpp

struct ReadAwaiter {
    int fd;
    void* buffer;
    size_t size;
    EventLoop& loop;

    bool await_ready() {
        // Try a non-blocking read
        // On EAGAIN -> return false: we must wait
    }

    void await_suspend(coroutine_handle<> handle) {
        // Register the fd with epoll, watching EPOLLIN
        // Store the handle so we can resume later
        epoll_event ev;
        ev.events = EPOLLIN | EPOLLET;  // Edge-triggered
        ev.data.ptr = handle.address();
        epoll_ctl(loop.epoll_fd_, EPOLL_CTL_ADD, fd, &ev);
    }

    size_t await_resume() {
        // Return the number of bytes actually read
        return bytes_read;
    }
};

```

The scheduler's `run()` loop needs to grow once more—while handling timers and the ready queue, it must also call `epoll_wait` to check for I/O events:

```cpp

void run() {
    while (running_) {
        // 1. Handle expired timers
        process_timers();

        // 2. Process the ready coroutines
        process_ready_queue();

        // 3. epoll_wait for I/O events
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

```

Pitfall warning: in edge-triggered (`EPOLLET`) mode, `epoll_wait` reports an fd only once, when its state changes. If you did not read all the data, the next `epoll_wait` will not report it again. So `await_resume` should read in a loop until `EAGAIN`. Also, `EINTR` (interrupted by a signal) is not an error—you should retry the `epoll_wait`.

### Verification

```cpp
TEST_CASE("Milestone 4: epoll echo server",
          "[lab4][milestone4]")
{
    // Start the echo server
    int listen_fd = create_listen_socket(8080);
    EventLoop loop;

    // One coroutine per connection
    auto handle_connection = [&](int fd) -> Task<void> {
        char buffer[1024];
        while (true) {
            auto n = co_await loop.read(fd, buffer, sizeof(buffer));
            if (n <= 0) break;  // Connection closed
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

    // Run the client test in another thread
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

### Objectives

Combine the components of Milestones 1–4 into a complete coroutine echo server. Support multiple concurrent connections, client-disconnect detection, and a graceful stop.

### Why

The echo server is the "Hello World" of network programming. Implemented with coroutines, the code looks almost the same as the synchronous version—a sequential read/write loop—yet underneath it is asynchronous and non-blocking, with one thread serving many connections. That is the power of coroutines: write synchronous-style code, get asynchronous performance.

### Implementation Guide

The complete logic of the echo server is already on display in the Milestone 4 tests. The focus of this milestone is adding error handling and a graceful stop:

- Handle `EAGAIN`, `EINTR`, connection close (read returns 0), and partial writes
- `stop()` closes the listen fd and waits for all established connections to finish
- An exception in one coroutine must not affect other connections—each connection's coroutine should have its own try-catch

### Verification

Verification for this milestone is an end-to-end test—start the server, connect several clients concurrently, send data, verify that the echoed data comes back correct, then stop gracefully.

## Checklist

- [ ] The four key `promise_type` callbacks of `Task<T>` are implemented correctly
- [ ] Multiple coroutines can execute alternately inside the `Scheduler`
- [ ] After `yield` hands over execution, the other coroutines keep running
- [ ] The timing accuracy of `sleep_for` stays within an acceptable range (±10ms)
- [ ] The timer heap handles coroutines with different expiry times correctly
- [ ] The epoll event loop handles read/write/accept correctly
- [ ] The echo server handles multiple concurrent connections
- [ ] The coroutine frame is destroyed when the coroutine finishes—no leaks
- [ ] The exception-handling policy is explicit; exceptions are never silently lost
- [ ] You can explain the design considerations behind `initial_suspend` returning `suspend_always`
- [ ] You can explain the difference between edge-triggered and level-triggered epoll, and what it means for the code
- [ ] You can show that the I/O awaiter handles `EAGAIN` and `EINTR` correctly
