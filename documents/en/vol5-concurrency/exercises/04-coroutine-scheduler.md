---
chapter: 10
cpp_standard:
- 20
description: 'Implement a minimalist coroutine scheduler, and master the complete
  C++20 coroutine chain from syntax to runtime: Task, Scheduler, timer, and epoll
  event loop.'
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
  source: documents/vol5-concurrency/exercises/04-coroutine-scheduler.md
  source_hash: f4489aa0b592d583c51505dd2e9fcb064d32365d9cbf12c1a3597cbbe042e374
  translated_at: '2026-06-16T04:42:23.389173+00:00'
  engine: anthropic
  token_count: 3624
---
# Lab 4: Coroutine Scheduler and Event Loop

## Objectives

The thread pool in Lab 3 represents "task-level" concurrency—where each task is a complete function call that exclusively occupies a thread from start to finish. In this lab, we dive into finer-grained concurrency: coroutines. A coroutine can suspend at a specific point, yielding execution back to the scheduler, and resume once conditions are met. This means a single thread can multiplex multiple coroutines—shifting from the "one task per thread" model to "one thread running multiple half-finished tasks."

We will implement a minimalist coroutine scheduler: starting with manual scheduling and `yield`, adding timers, and finally integrating `epoll` on Linux/WSL2 to build a coroutine echo server. This lab is a core advanced project in Volume 5—it moves C++20 coroutines from "syntax understanding" to "runtime understanding."

## Prerequisites

Before starting, ensure you have read the following chapters:

- **ch06-01**: Evolution of Asynchronous Programming — Motivation from callbacks to coroutines
- **ch06-02**: C++20 Coroutine Basics — `co_await`, `co_yield`, `co_return`
- **ch06-03**: `promise_type` and Awaitable — The complete mechanism for custom awaitables
- **ch06-04**: Async I/O and Event Loops — epoll/kqueue event-driven models
- **ch06-05**: Coroutine in Action: Echo Server — Complete coroutine networking applications
- **Lab 3**: Thread pool shutdown semantics design ideas (referenced for this lab's shutdown design)

## Environment Setup

This lab requires C++20 and a Linux/WSL2 environment.

- **Compiler**: GCC 12+ or Clang 15+ (full coroutine support)
- **Platform**: Linux or WSL2 (required for the epoll milestone)
- **CMake**: 3.14+

```bash
sudo apt install cmake g++ python3 # Basic tools
```

## Final Interface

### `Task<T>` — Coroutine Task Wrapper (Milestone 1, move-only)

Internally defines `promise_type`, requiring the implementation of the following callbacks:

| promise_type Method | Return Type | Description | Milestone |
|---------------------|-------------|-------------|-----------|
| get_return_object | `Task` | Create Task object | MS1 |
| initial_suspend | `std::suspend_always` | Lazy mode, do not auto-execute after creation | MS1 |
| final_suspend | `std::suspend_always` | Do not auto-destroy frame after completion | MS1 |
| return_value | `void` | Store the return value of `co_return` | MS1 |
| unhandled_exception | `void` | Store exception (using `std::exception_ptr`) | MS1 |

Member variables:

| Type | Member | Semantics |
|------|--------|-----------|
| `std::coroutine_handle<>` | `handle_` | Coroutine handle |

Interface:

| Method | Signature | Description | Milestone |
|------|------|------|-----------|
| Constructor | `Task(std::coroutine_handle<> h)` | Accept coroutine handle | MS1 |
| Destructor | `~Task()` | Destroy coroutine frame | MS1 |
| get | `T get()` | Get result or rethrow exception | MS1 |

### `Scheduler` — Coroutine Scheduler (Milestone 2)

Member variables:

| Type | Member | Semantics |
|------|--------|-----------|
| `std::queue<std::coroutine_handle<>>` | `ready_queue_` | Ready coroutine queue |

Interface:

| Method | Signature | Description | Milestone |
|------|------|------|-----------|
| schedule | `void schedule(std::coroutine_handle<> h)` | Add coroutine to ready queue | MS2 |
| yield | `auto yield()` | Return awaitable, suspend and re-queue | MS2 |
| run | `void run()` | Loop executing ready coroutines until queue is empty | MS2 |
| has_work | `bool has_work()` | Check if there are pending coroutines | MS2 |

### `SleepFor` — `sleep_for` awaitable (Milestone 3)

| Method | Signature | Description | Milestone |
|------|------|------|-----------|
| await_ready | `bool await_ready()` | Returns false (always suspend) | MS3 |
| await_suspend | `void await_suspend(std::coroutine_handle<> h)` | Register to timer heap | MS3 |
| await_resume | `void await_resume()` | No-op on resume | MS3 |

### `EventLoop` — `epoll` Event Loop (Milestone 4, Linux/WSL2)

Member variables:

| Type | Member | Semantics |
|------|--------|-----------|
| `int` | `epoll_fd_` | epoll instance file descriptor |
| `std::atomic<bool>` | `running_` | Running flag |

Interface:

| Method | Signature | Description | Milestone |
|------|------|------|-----------|
| read | `auto read(int fd)` | Register read event, return awaitable | MS4 |
| write | `auto write(int fd)` | Register write event, return awaitable | MS4 |
| accept | `auto accept(int fd)` | Register accept event, return awaitable | MS4 |
| run | `void run()` | Event loop main loop | MS4 |
| stop | `void stop()` | Stop event loop | MS4 |

## Milestone 1: Task<void> and Basic Coroutines

### Objectives

Implement `Task<void>` and its `promise_type`, including `get_return_object`, `initial_suspend`, `final_suspend`, and `unhandled_exception`. First implement the specialization for `Task<void>`, then extend to `Task<T>`.

### Why

`Task` is the base currency of the coroutine scheduler—all coroutine functions return `Task`, and the scheduler manages suspension and resumption via the `std::coroutine_handle<>` inside `Task`. `promise_type` defines behavior at key lifecycle points: what to do on creation (`get_return_object`), what to do on return (`return_value`), what *not* to do on finish (`final_suspend`), and what to do on exception (`unhandled_exception`). Understanding these four callbacks means you understand the C++20 coroutine runtime model.

### Implementation Guide

The core responsibility of `promise_type` is to inject custom logic at various lifecycle nodes of the coroutine.

`initial_suspend` returns `std::suspend_always`—meaning the coroutine suspends before the function body executes, preventing it from running automatically. This marks a "lazy" task—the coroutine does nothing after creation until explicitly `resume`d. The opposite is `std::suspend_never` ("eager" task, executes immediately). We choose lazy because the scheduler needs to control *when* execution starts.

`final_suspend` returns `std::suspend_always`—the coroutine suspends after reaching `co_return`, without automatically destroying the coroutine frame. This prevents the frame from being destroyed before `get` reads the result. `Task`'s destructor is responsible for destroying the frame.

`unhandled_exception` stores the exception (using `std::current_exception`), which is rethrown in `get`.

**Warning**: If `final_suspend` returns `std::suspend_never`, the coroutine frame is destroyed automatically upon completion. While convenient, if the frame is destroyed before `get` is called, accessing members of `promise_type` is UB. Most educational implementations choose `std::suspend_always` + manual destroy in the destructor; while it requires manual management, it is safer.

### Verification

```cpp
// tests/test_milestone_1.cpp
```

## Milestone 2: Scheduler and yield

### Objectives

Implement `Scheduler`, maintaining a ready queue that supports `schedule` (enqueue) and `yield` (suspend current coroutine and re-queue). `run` loops to take coroutines from the queue and resume them until the queue is empty.

### Why

With `Task`, we have executable units that can suspend and resume. Without a scheduler, the execution order is entirely manual—who `resume`s whom, and when. `Scheduler` automates this orchestration: all coroutines enter the ready queue, and the scheduler executes them in FIFO order. `yield` gives up execution to other coroutines—this is the core of "cooperative multitasking."

### Implementation Guide

The data structure for `Scheduler` is simple—a `std::queue<std::coroutine_handle<>>`. `schedule` puts the handle into the queue. `run` loops to take handles and `resume` them.

`yield` is an awaitable whose `await_suspend` puts the current coroutine's handle back into the ready queue and returns `true` (indicating suspension). This ensures the scheduler picks up this coroutine again in the next loop cycle.

```cpp
// src/scheduler.cpp
```

**Warning**: The `run` loop cannot be a simple `while (!queue.empty())`, because coroutines might add new coroutines to the queue during execution. You need to ensure `run` loops until the queue is empty *and* no coroutines are executing. A simple approach is: `while (has_work())`.

### Verification

```text
# Milestone 2 Verification
```

## Milestone 3: sleep_for and timer heap

### Objectives

Implement a `SleepFor` awaitable. The scheduler maintains a timer heap (min-heap) and pushes the coroutine back to the ready queue when it expires.

### Why

`yield` makes a coroutine give up execution immediately, but often we need to "yield and resume after a duration"—for polling intervals, timeout waits, or animation frame rate control. `sleep_for` is the most basic timed awaitable; its implementation introduces the scheduler's first "non-immediate" event source—the coroutine doesn't return to the ready queue immediately but waits in the timer heap for a while.

### Implementation Guide

`SleepFor`'s `await_suspend` does two things: calculate the wake-up time (`std::chrono::steady_clock::now() + duration`) and put the `std::coroutine_handle<>` into the timer heap. `await_ready` returns false (always suspend).

The scheduler's `run` loop needs modification—before taking a task, check if the timer heap's minimum element has expired. If expired, pop it from the heap and push it to the ready queue. If not expired and the ready queue is empty, `sleep` until the nearest timer expires.

Pseudo-code:

```text
# Milestone 3 Pseudo-code
```

**Warning**: Do not create a separate thread for every `sleep_for` to time execution—that reverts to the "one task per thread" model. The design goal of the timer heap is to share one thread for all timers, using a min-heap to efficiently find the nearest expiration time. Also, `std::priority_queue` is a max-heap by default; you need a custom comparator to keep the smallest element at the top.

### Verification

```cpp
// tests/test_milestone_3.cpp
```

## Milestone 4: epoll event loop

### Objectives

Implement an epoll-based event loop on Linux/WSL2, supporting read/write/accept awaitables for non-blocking file descriptors.

### Why

Timers allow coroutines to resume after a specified time, but true asynchronous programming requires waiting for "I/O events ready"—socket readable, socket writable, or new connections arriving. `epoll` is Linux's efficient I/O multiplexing mechanism; it allows a single thread to monitor multiple file descriptors and wakes up waiting coroutines when they are ready. Integrating `epoll` into the scheduler gives us a complete "coroutine + I/O" runtime.

### Implementation Guide

Core idea: Each I/O awaitable registers its `std::coroutine_handle<>` to `epoll` in `await_suspend`. When `epoll` reports the fd is ready, it puts the corresponding handle back into the ready queue.

Pseudo-code for read awaitable:

```cpp
// Milestone 4 Read Awaitable Pseudo-code
```

The scheduler's `run` loop needs extension again—while processing timers and the ready queue, it must also call `epoll_wait` to check for I/O events:

```cpp
// Milestone 4 Event Loop Pseudo-code
```

**Warning**: In Edge-Triggered mode (`EPOLLET`), `epoll_wait` reports an event only once when the fd state changes. If you don't read all data, the next `epoll_wait` won't report it again. Therefore, you should loop reading until `EAGAIN` in the awaitable. Also, `EINTR` (interrupted by signal) is not an error; `epoll_wait` should be retried.

### Verification

```text
# Milestone 4 Verification
```

## Milestone 5: coroutine echo server

### Objectives

Combine components from Milestones 1–4 to implement a complete coroutine echo server. Support multiple concurrent connections, client disconnect detection, and graceful shutdown.

### Why

The echo server is the "Hello World" of network programming. Implementing it with coroutines looks almost identical to the synchronous version—sequential read/write loops—but the underlying implementation is asynchronous and non-blocking, handling multiple connections with a single thread. This demonstrates the power of coroutines: writing synchronous-style code while achieving asynchronous performance.

### Implementation Guide

The complete logic for the echo server is reflected in the Milestone 4 tests. The focus of this milestone is adding error handling and graceful shutdown:

- Handle `EINTR`, `EAGAIN`, connection close (read returns 0), and partial writes
- `stop()` closes the listen fd and waits for all established connections to finish
- Coroutine exceptions should not affect other connections—each connection's coroutine should have its own `try-catch`

### Verification

Verification for this milestone is end-to-end testing—start the server, connect with multiple clients concurrently, send data, verify the echoed data is correct, and then shut down gracefully.

## Checklist

- [ ] `Task`'s `promise_type` four key callbacks implemented correctly
- [ ] Multiple coroutines can execute alternately in `Scheduler`
- [ ] Other coroutines continue running after `yield` gives up execution
- [ ] `sleep_for` timing accuracy is within acceptable range (±10ms)
- [ ] Timer heap correctly handles coroutines with different expiration times
- [ ] `epoll` event loop correctly handles read/write/accept
- [ ] Echo server handles multiple concurrent connections
- [ ] Coroutine frame is destroyed after coroutine finishes, no leaks
- [ ] Exception handling strategy is clear, no silent loss of exceptions
- [ ] Can explain the design considerations of `final_suspend` returning `std::suspend_always`
- [ ] Can explain the difference between edge-triggered and level-triggered and its impact on code
- [ ] Can explain how the I/O awaiter correctly handles `EINTR` and `EAGAIN`
