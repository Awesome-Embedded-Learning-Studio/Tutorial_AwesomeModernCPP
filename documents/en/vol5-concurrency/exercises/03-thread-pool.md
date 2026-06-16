---
chapter: 10
cpp_standard:
- 17
- 20
description: Implement a fixed-size thread pool, and master futures, packaged tasks,
  exception propagation, graceful shutdown, and backpressure strategies.
difficulty: advanced
order: 4
prerequisites:
- '卷五 ch05: future、任务与线程池'
- 'Lab 0: Thread Lifecycle Lab'
- 'Lab 1: Bounded Queue, Concurrent Cache and Sync Primitives'
reading_time_minutes: 12
tags:
- host
- cpp-modern
- advanced
title: 'Lab 3: Production-style Thread Pool'
translation:
  source: documents/vol5-concurrency/exercises/03-thread-pool.md
  source_hash: b2bc89e7250b9cb9405ecab7e6441b09bd7f525903e752bcde6903494130e39e
  translated_at: '2026-06-16T04:07:25.417012+00:00'
  engine: anthropic
  token_count: 3187
---
# Lab 3: Production-style Thread Pool

## Objectives

The thread pool is the project in Volume 5 best suited for a CS144-style assignment. It integrates knowledge from all previous Labs—`std::jthread` for managing thread lifecycles, `BlockingQueue` as the task queue, atomics for statistics, and shutdown semantics for graceful exit. However, a thread pool is not just a simple assembly of these components—it introduces several new engineering challenges: type erasure for `std::function` and `packaged_task`, cross-thread exception propagation, support for move-only tasks, and the drain strategy for the task queue upon shutdown.

After completing this Lab, you should have a thread pool component with a clear interface, testability, shutdown capability, and exception propagation—ready for direct use in the Capstone project.

## Prerequisites

Before starting, ensure you have read the following chapters:

- **ch05-01**: std::async and future — `std::future`, `std::promise`, `std::shared_future`
- **ch05-02**: promise and packaged_task — `packaged_task`, type erasure
- **ch05-03**: jthread and stop_token — C++20 cooperative cancellation
- **ch05-04**: Thread Pool Design — Basic architecture and design considerations for thread pools
- **Lab 0**: Implementation of `std::jthread`
- **Lab 1**: Implementation of `BlockingQueue` (reused directly in this Lab)

## Environment Setup

Same as Lab 1 (C++20, Catch2 v3, TSan).

## Final Interface

### `ThreadPool` — Fixed-size thread pool (non-copyable, automatic shutdown on destruction)

Type alias: `Task` (type-erased task wrapper)

Member variables:

| Type | Member | Semantics |
|------|--------|-----------|
| `BlockingQueue<Task>` | `queue_` | Task queue (reuse Lab 1) |
| `std::vector<std::jthread>` | workers_ | Worker thread collection (reuse Lab 0) |
| `std::atomic<bool>` | shutdown_flag_ | Shutdown flag |

Interface:

| Method | Signature | Description | Milestone |
|--------|-----------|-------------|-----------|
| Constructor | `ThreadPool(size_t n)` | Creates specified number of worker threads | MS1 |
| Destructor | `~ThreadPool()` | Calls `shutdown()`, waits for all tasks to complete | MS4 |
| submit | `template <typename F, typename... Args> auto submit(F&& f, Args&&... args) -> std::future<return_type>` | Submits task, returns future; throws exception if already shut down | MS2 |
| shutdown | `void shutdown()` | Drains queue, rejects new submissions, joins all workers | MS4 |
| pending_tasks | `size_t pending_tasks()` | Number of tasks currently in the queue | MS1 |

## Milestone 1: Basic Thread Pool

### Objective

Implement the most basic thread pool: a fixed number of workers, a shared task queue, and stop/join on destruction. The `submit` method accepts a `Task` type and does not return a future.

### Why

First, get the basic architecture of "multiple workers fetching tasks from a shared queue" working without involving templates, futures, or exception propagation. Once this skeleton is in place, subsequent milestones simply add functionality on top.

### Implementation Guide

The core structure is `BlockingQueue` + `std::jthread`. The loop logic for each worker thread is simple: `pop` a task from the queue, execute it, and continue to the next. When the queue is closed and empty, the worker exits the loop.

```cpp
void worker_thread() {
    while (true) {
        auto task = queue_.pop(); // Blocks until task available or closed
        if (!task) {
            break; // Exit if queue is closed and empty
        }
        (*task)(); // Execute the task
    }
}
```

Create N workers in the constructor:

```cpp
ThreadPool(size_t n) : queue_(256) { // Set capacity to 256
    for (size_t i = 0; i < n; ++i) {
        workers_.emplace_back([this] { this->worker_thread(); });
    }
}
```

Pitfall alert: When passing a member function to `std::jthread`, the first argument is the `this` pointer. Ensure the thread pool object's lifetime exceeds all workers—the destructor must close the queue and wait for all workers to exit. Also, what capacity is appropriate for `BlockingQueue`? 256 is a good default—too large wastes memory, too small easily blocks the submitting thread. If you don't want a limit, you can use a very large value or implement an unbounded queue yourself, but this Lab recommends using a bounded queue.

### Verification

```cpp
TEST_CASE("ThreadPool basic execution", "[pool]") {
    ThreadPool pool(4);
    std::atomic<int> counter{0};
    for (int i = 0; i < 100; ++i) {
        pool.submit([&] { counter.fetch_add(1); });
    }
    // Destructor waits for all tasks
    REQUIRE(counter == 100);
}
```

## Milestone 2: submit Returns future

### Objective

Implement a template version of `submit` that accepts any callable object and arguments, returning a `std::future<R>`. The caller retrieves the task's return value via the future.

### Why

The basic version of `submit` only accepts `std::function`, so the caller cannot retrieve the task's return value. In actual engineering, thread pool callers almost always need to know the task's result—whether it's successfully returned data or a thrown exception. `std::promise` + `std::future` is the mechanism provided by the C++ standard for "passing results across threads".

### Implementation Guide

The core idea is to wrap the user-submitted callable into a `std::packaged_task`, return the `std::future` associated with the `packaged_task` to the caller, and push the `packaged_task` itself (wrapped as a `std::function`) into the task queue.

Pseudo-code:

```cpp
template <typename F, typename... Args>
auto submit(F&& f, Args&&... args) -> std::future<decltype(f(args...))> {
    using R = decltype(f(args...));

    // 1. Create packaged_task
    auto task = std::make_shared<std::packaged_task<R()>>(
        [f = std::move(f), args...]() mutable -> R {
            return f(args...); // Capture arguments by value
        }
    );

    // 2. Get future
    auto result = task->get_future();

    // 3. Wrap in std::function and push
    queue_.push([task]() { (*task)(); });

    return result;
}
```

Here we use `std::shared_ptr` because `std::packaged_task` is move-only (not copyable), while `std::function` requires a copyable constructor. Placing `packaged_task` in a `shared_ptr`, and having the lambda capture the `shared_ptr` (which is copyable), solves this problem.

Pitfall alert: `std::forward` has pitfalls when handling reference parameters. If your callable accepts reference parameters, `std::forward` might decay the reference semantics. A safer approach is to use a lambda to bind:

```cpp
[f = std::forward<F>(f), args...]() mutable -> R {
    return f(args...);
}
```

C++20 lambda init-capture supports parameter pack expansion (`args...`). If your compiler doesn't support it, you can use `std::tuple` to store arguments.

### Verification

```cpp
TEST_CASE("ThreadPool returns future", "[pool]") {
    ThreadPool pool(2);
    auto f1 = pool.submit([] { return 42; });
    auto f2 = pool.submit([] { return std::string("hello"); });

    REQUIRE(f1.get() == 42);
    REQUIRE(f2.get() == "hello");
}
```

## Milestone 3: Exception Propagation and move-only Parameters

### Objective

Ensure that `future.get()` can re-throw exceptions from tasks. Support move-only type parameters (like `std::unique_ptr`).

### Why

Exception propagation is the most easily overlooked part of thread pool design. If a task throws an exception and `future.get()` doesn't re-throw it, the exception is silently swallowed—the caller has no idea the task failed. The good news is that `std::promise` already handles exception propagation—when a task throws, `packaged_task` captures it and stores it in the shared state, re-throwing it when `future.get()` is called. So the main work of this milestone isn't "implementing" exception propagation, but "verifying" it works correctly and ensuring your `submit` implementation doesn't accidentally swallow exceptions.

Support for move-only parameters is more direct—`std::packaged_task` is itself move-only, and lambdas can capture move-only types. You need to ensure that nowhere in the entire chain from `submit` to worker execution forces a copy.

### Implementation Guide

If your Milestone 2 implementation used `std::packaged_task`, exception propagation works automatically. You just need to verify it.

For move-only parameters, use lambda init-capture to pass them:

```cpp
auto ptr = std::make_unique<int>(123);
pool.submit([p = std::move(ptr)] { /* use p */ });
```

Pitfall alert: Do not use `std::ref` in `submit` parameters to pass move-only types—`std::ref` doesn't transfer ownership, it just creates a reference wrapper, and the referenced object might be destroyed by the time the worker executes.

### Verification

```cpp
TEST_CASE("ThreadPool exception propagation", "[pool]") {
    ThreadPool pool(1);
    auto f = pool.submit([] { throw std::runtime_error("error"); });
    REQUIRE_THROWS_AS(f.get(), std::runtime_error);
}

TEST_CASE("ThreadPool move-only params", "[pool]") {
    ThreadPool pool(1);
    auto ptr = std::make_unique<int>(42);
    auto f = pool.submit([p = std::move(ptr)] { return *p; });
    REQUIRE(f.get() == 42);
}
```

## Milestone 4: Shutdown Semantics

### Objective

Implement the `shutdown` method: drain existing tasks in the queue, but reject new submissions. The destructor calls `shutdown` and waits for all workers to exit.

### Why

Shutdown is the part of thread pool design that tests the design the most. A production-grade thread pool shutdown must satisfy three conditions simultaneously: existing tasks are executed (no loss), new submissions are rejected (clear error signal), and all worker threads are joined (no leaks). If any condition isn't met, it's an engineering defect—losing tasks leads to data incompleteness, not rejecting new submissions leads to infinite waits, and not joining leads to `std::terminate`.

### Implementation Guide

The implementation idea for `shutdown` is: set the `shutdown_flag_` to true, then `close` the task queue. The worker loop invariant remains—exit when `pop` returns `std::nullopt`. `submit` throws an exception (or returns a broken promise future) when `shutdown_flag_` is true.

```cpp
void shutdown() {
    shutdown_flag_.store(true);
    queue_.close(); // Unblock all workers
}

void worker_thread() {
    while (true) {
        auto task = queue_.pop(); // Returns nullopt if closed & empty
        if (!task) {
            break;
        }
        (*task)();
    }
}
```

The destructor calls `shutdown`:

```cpp
~ThreadPool() {
    shutdown();
    // jthread destructor automatically joins
}
```

Pitfall alert: `shutdown` must be idempotent—calling it multiple times shouldn't cause issues. Use `std::call_once` to ensure only one thread executes the shutdown logic. Also, if there are backlogged tasks in the queue, workers will still execute them after `shutdown` (because `close` allows draining remaining data). If you want "immediate stop" behavior (discarding unexecuted tasks), you need to modify the shutdown logic.

### Verification

```cpp
TEST_CASE("ThreadPool shutdown", "[pool]") {
    ThreadPool pool(2);
    pool.submit([] { std::this_thread::sleep_for(100ms); });
    pool.shutdown();
    REQUIRE_THROWS(pool.submit([] {})); // Should reject
}
```

## Milestone 5: Optional Capacity and Backpressure Strategy

### Objective

Add capacity limits to the thread pool's task queue and implement three backpressure strategies: block (wait for space), reject (immediate rejection), and caller-runs (execute in caller's thread).

### Why

Unbounded queues are dangerous in production environments—if consumers can't keep up with producers, the queue will grow indefinitely, eventually exhausting memory. Bounded queues with backpressure strategies are standard design for production-grade thread pools. Each strategy has its use cases: block fits scenarios where task loss is unacceptable, reject fits high-throughput scenarios where task loss is tolerable, and caller-runs fits scenarios where automatic slowdown is desired.

### Implementation Guide

Add capacity check logic to `submit`. `BlockingQueue` already has capacity limits and `try_push`, so implementation is relatively straightforward.

- **block**: Use `push` directly (blocks waiting for space)
- **reject**: Use `try_push`, throw exception on failure
- **caller-runs**: Execute task directly in current thread if `try_push` fails

Backpressure strategy can be passed in as a constructor parameter or implemented via template strategy parameters. For simplicity, this Lab suggests using an enum:

```cpp
enum class Backpressure { Block, Reject, CallerRuns };
```

### Verification

```cpp
TEST_CASE("ThreadPool backpressure", "[pool]") {
    ThreadPool pool(1, /* capacity */ 1, Backpressure::Reject);
    // Fill queue
    pool.submit([] { std::this_thread::sleep_for(1s); });
    // Should reject
    REQUIRE_THROWS(pool.submit([] {}));
}
```

## Checklist

- [ ] Basic thread pool executes tasks concurrently without loss
- [ ] `submit` returns `future` that gets correct return values
- [ ] When task throws exception, `future.get()` re-throws it
- [ ] move-only parameters (`std::unique_ptr`) pass correctly
- [ ] `shutdown` drains queue and rejects new submissions
- [ ] Destructor calls `shutdown` and joins all workers
- [ ] `shutdown` is idempotent, multiple calls cause no issues
- [ ] Backpressure strategy behaves as expected
- [ ] All tests pass under TSan with no data race reports
- [ ] Can explain what problem `std::shared_ptr` solves (why not use `std::packaged_task` directly)
- [ ] Can explain the tradeoff between "drain queue" vs "discard tasks" on shutdown
- [ ] Can verbally explain how this thread pool will be used directly in the Capstone project
