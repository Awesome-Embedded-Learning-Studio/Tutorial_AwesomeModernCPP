---
chapter: 10
cpp_standard:
- 17
- 20
description: 'Implement a fixed-size thread pool and master future, packaged_task, exception propagation, graceful shutdown, and backpressure policies'
difficulty: advanced
order: 4
prerequisites:
- Futures, Tasks, and Thread Pools
- 'Lab 0: Thread Lifecycle'
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
  translated_at: '2026-09-26T09:33:05+00:00'
  engine: anthropic
  token_count: 4100
---
# Lab 3: Production-style Thread Pool

## Goal

The thread pool is the project in Volume 5 that best deserves the CS144-style big-assignment treatment. It strings together knowledge from all the previous labs — `JoiningThread` for thread lifecycle management, `BoundedBlockingQueue` as the task queue, atomics for statistics, close semantics for graceful exit. But a thread pool is not just a simple assembly of those components — it introduces several new engineering challenges: type erasure with `std::future` and `packaged_task`, propagating exceptions across threads, move-only task support, and a drain strategy for the task queue at shutdown.

After finishing this lab, you should have a thread pool component with a clean interface that is testable, shutdownable, and able to propagate exceptions — ready to be used directly in the Capstone project.

## Prerequisites

Before you start, make sure you have read the following chapters:

- **ch05-01**: std::async and future — `std::future`, `std::promise`, `std::async`
- **ch05-02**: promise and packaged_task — `std::packaged_task`, type erasure
- **ch05-03**: jthread and stop_token — C++20 cooperative cancellation
- **ch05-04**: Thread Pool Design — basic thread pool architecture and design considerations
- **Lab 0** — the `JoiningThread` implementation
- **Lab 1** — the `BoundedBlockingQueue` implementation (reused directly in this lab)

## Environment Setup

Same as Lab 1 (C++20, Catch2 v3, TSan).

## Final Interface

### `ThreadPool` — Fixed-Size Thread Pool (Non-Copyable, Destructor Triggers Shutdown)

Type alias: `using Task = std::function<void()>;` (a type-erased task wrapper)

Member variables:

| Type | Member | Semantics |
|------|------|------|
| `BoundedBlockingQueue<Task>` | `task_queue_` | Task queue (reused from Lab 1) |
| `std::vector<JoiningThread>` | `workers_` | The set of worker threads (reused from Lab 0) |
| `std::atomic<bool>` | `stopped_` | Shutdown flag |

Interface:

| Method | Signature | Description | Milestone |
|------|------|------|-----------|
| Constructor | `ThreadPool(size_t thread_count)` | Creates the given number of worker threads | MS1 |
| Destructor | `~ThreadPool() noexcept` | Calls shutdown(), waits for all tasks to complete | MS4 |
| submit | `auto submit(F&&, Args&&...) -> future<invoke_result_t<F, Args...>>` | Submits a task and returns a future; throws if already shut down | MS2 |
| shutdown | `void shutdown()` | Drains the queue, rejects new submissions, joins all workers | MS4 |
| pending_tasks | `size_t pending_tasks() const` | Number of tasks currently in the queue | MS1 |

## Milestone 1: Basic Thread Pool

### Goal

Implement the most basic thread pool: a fixed number of workers, one shared task queue, stop and join on destruction. `submit` accepts a task of type `std::function<void()>` and returns no future.

### Why

First get the basic architecture working — multiple workers pulling tasks from a shared queue — without involving templates, futures, or exception propagation. Once this skeleton stands, every later milestone just layers features on top of it.

### Implementation Guide

The core structure is `BoundedBlockingQueue<Task>` + `std::vector<JoiningThread>`. Each worker thread's loop logic is simple: `pop` a task from the queue, execute it, grab the next one. When the queue is closed and empty, the worker exits the loop.

```cpp

void worker_loop() {
    while (auto task = task_queue_.pop()) {
        (*task)();  // execute the task
    }
}

```

The constructor creates N workers:

```cpp

ThreadPool(size_t count)
    : task_queue_(256)  // queue capacity
{
    for (size_t i = 0; i < count; ++i) {
        workers_.emplace_back(&ThreadPool::worker_loop, this);
    }
}

```

Pitfall warning: when `worker_loop` is passed to `JoiningThread` as a member function, the first argument is the `this` pointer. Make sure the pool object outlives all workers — the destructor must close the queue first and wait for every worker to exit. Also, how large should the `BoundedBlockingQueue` capacity be? 256 is a decent default — too large wastes memory, too small easily ends up blocking submitting threads. If you don't want a cap at all, you can use a very large value or implement an unbounded queue yourself, but this lab recommends a bounded queue.

### Verification

```cpp
TEST_CASE("Milestone 1: basic thread pool executes tasks",
          "[lab3][milestone1]")
{
    ThreadPool pool(4);
    std::atomic<int> counter{0};

    for (int i = 0; i < 100; ++i) {
        pool.submit([&counter]() {
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }

    // Wait for all tasks to complete
    // Note: the basic submit doesn't return a future
    // so we need another way to wait — a simple sleep here
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    REQUIRE(counter.load() == 100);
}

TEST_CASE("Milestone 1: destructor joins all workers",
          "[lab3][milestone1]")
{
    std::atomic<int> counter{0};
    {
        ThreadPool pool(4);
        for (int i = 0; i < 50; ++i) {
            pool.submit([&counter]() {
                counter.fetch_add(1);
            });
        }
    }  // pool destructor → shutdown → join

    REQUIRE(counter.load() == 50);
}
```

## Milestone 2: submit Returns a Future

### Goal

Implement a templated version of `submit` that accepts any callable object and arguments and returns `std::future<R>`. The caller obtains the task's return value through `future::get()`.

### Why

The basic `submit` only accepts `std::function<void()>`, so the caller has no way to obtain a task's return value. In real-world engineering, callers of a thread pool almost always need to know the task's outcome — whether that is data returned on success or a thrown exception. `std::future` + `std::packaged_task` is the standard library's mechanism for passing results across threads.

### Implementation Guide

The core idea is to wrap the user-submitted callable in a `std::packaged_task<R()>`, return the `packaged_task`'s `future` to the caller, and stuff the `packaged_task` itself (wrapped as a `std::function<void()>`) into the task queue.

Pseudocode:

```cpp
template <class F, class... Args>
auto submit(F&& f, Args&&... args)
    -> future<invoke_result_t<F, Args...>>
{
    using R = invoke_result_t<F, Args...>;

    // Bind f(args...) into a nullary callable object
    auto task = make_shared<packaged_task<R()>>(
        bind(forward<F>(f), forward<Args>(args)...)
    );

    future<R> result = task->get_future();

    // Wrap as function<void()> and put it into the queue
    task_queue_.push([task]() { (*task)(); });

    return result;
}
```

The reason for `std::shared_ptr<packaged_task>` is that `packaged_task` is move-only (not copyable), while `std::function` requires copy-constructibility. Put the `packaged_task` inside a `shared_ptr`, have the lambda capture the `shared_ptr` (which is copyable), and the problem is solved.

Pitfall warning: `std::bind` has traps when dealing with reference parameters. If your callable takes arguments by reference, `bind` may decay away the reference semantics. Binding with a lambda is safer:

```cpp
auto wrapper = [f = forward<F>(f),
                ... args = forward<Args>(args)]() mutable {
    return f(args...);
};
```

C++20's lambda init-capture supports parameter pack expansion (`... args = forward<Args>(args)`); if your compiler doesn't support it, you can store the arguments in a `std::tuple`.

### Verification

```cpp
TEST_CASE("Milestone 2: submit returns future with value",
          "[lab3][milestone2]")
{
    ThreadPool pool(4);

    auto f1 = pool.submit([]() { return 42; });
    auto f2 = pool.submit([](int a, int b) { return a + b; },
                          10, 20);

    REQUIRE(f1.get() == 42);
    REQUIRE(f2.get() == 30);
}

TEST_CASE("Milestone 2: submit handles void return",
          "[lab3][milestone2]")
{
    ThreadPool pool(4);
    std::atomic<bool> done{false};

    auto f = pool.submit([&done]() {
        done.store(true);
    });

    f.get();  // must not throw
    REQUIRE(done.load());
}

TEST_CASE("Milestone 2: multiple futures collected",
          "[lab3][milestone2]")
{
    ThreadPool pool(4);
    std::vector<std::future<int>> futures;

    for (int i = 0; i < 20; ++i) {
        futures.push_back(
            pool.submit([i]() { return i * i; }));
    }

    int sum = 0;
    for (auto& f : futures) {
        sum += f.get();
    }

    // sum = 0^2 + 1^2 + ... + 19^2 = 2470 - 19 = 2275? No.
    // 0+1+4+9+...+361 = 2470
    REQUIRE(sum == 2470);
}
```

## Milestone 3: Exception Propagation and Move-Only Arguments

### Goal

Make sure `future::get()` can rethrow exceptions thrown inside a task. Support move-only argument types (such as `std::unique_ptr`).

### Why

Exception propagation is the most easily overlooked part of thread pool design. If a task throws and `future::get()` doesn't rethrow it, the exception is silently swallowed — the caller never learns that the task failed. The good news is that `std::packaged_task` already handles exception propagation — when the task throws, `packaged_task` catches the exception and stores it in the `future`, and `get()` rethrows it. So the main work in this milestone is not *implementing* exception propagation but *verifying* that it works correctly, and making sure your `submit` implementation doesn't accidentally swallow exceptions.

Move-only argument support is more straightforward — `std::packaged_task` itself is move-only, and lambdas can capture move-only types too. What you must ensure is that nowhere along the delivery chain from `submit` to worker execution is a copy forced.

### Implementation Guide

If your Milestone 2 implementation used `shared_ptr<packaged_task>`, exception propagation already works automatically. You only need to verify it.

For move-only arguments, pass them with a lambda init-capture:

```cpp

auto ptr = make_unique<Data>(42);
auto f = pool.submit(`[p = move(ptr)]()` {
    return p->compute();
});

```

Pitfall warning: do not use `std::ref` in `submit`'s arguments to pass move-only types — `std::ref` doesn't transfer ownership; it merely creates a reference wrapper, and the object it refers to may already have been destroyed by the time a worker executes the task.

### Verification

```cpp
TEST_CASE("Milestone 3: exception propagates through future",
          "[lab3][milestone3]")
{
    ThreadPool pool(4);

    auto f = pool.submit([]() {
        throw std::runtime_error("task failed");
        return 42;
    });

    REQUIRE_THROWS_AS(f.get(), std::runtime_error);
}

TEST_CASE("Milestone 3: move-only parameter support",
          "[lab3][milestone3]")
{
    ThreadPool pool(4);

    auto ptr = std::make_unique<int>(42);
    auto f = pool.submit([p = std::move(ptr)]() {
        return *p;
    });

    REQUIRE(f.get() == 42);
}

TEST_CASE("Milestone 3: exception in one task doesn't affect others",
          "[lab3][milestone3]")
{
    ThreadPool pool(4);
    std::vector<std::future<int>> futures;

    futures.push_back(pool.submit([]() { return 1; }));
    futures.push_back(pool.submit([]() {
        throw std::runtime_error("fail");
    }));
    futures.push_back(pool.submit([]() { return 3; }));

    REQUIRE(futures[0].get() == 1);
    REQUIRE_THROWS_AS(futures[1].get(), std::runtime_error);
    REQUIRE(futures[2].get() == 3);
}
```

## Milestone 4: Shutdown Semantics

### Goal

Implement the `shutdown()` method: drain the tasks already in the queue, but reject new submissions. The destructor calls `shutdown()` and waits for all workers to exit.

### Why

Shutdown is the part of a thread pool that most tests the design. Shutting down a production-grade thread pool must satisfy three conditions at once: existing tasks are executed to completion (nothing lost), new submissions are rejected (with a clear error signal), and every worker thread is joined (no leaks). Failing any one of these conditions is an engineering defect — lost tasks lead to incomplete data, failing to reject new submissions leads to infinite waiting, and failing to join leads to `std::terminate()`.

### Implementation Guide

The implementation idea for `shutdown()` is: set the `stopped_` flag to true, then `close()` the task queue. The worker loop stays unchanged — it exits when `pop` returns `nullopt`. `submit` throws when `stopped_` is true (or returns a future with a broken promise).

```cpp
void shutdown() {
    bool expected = false;
    if (!stopped_.compare_exchange_strong(expected, true)) {
        return;  // already shut down
    }
    task_queue_.close();
    // destroying workers_ joins them automatically
}
```

The destructor calls `shutdown()`:

```cpp
~ThreadPool() noexcept {
    shutdown();
    // JoiningThread destructors inside workers_ join automatically
}
```

Pitfall warning: `shutdown()` must be idempotent — calling it multiple times must not cause trouble. Use `compare_exchange_strong` to guarantee that exactly one thread executes the shutdown logic. Also, if the queue has a backlog of tasks, workers will still execute them after `close()` (because `BoundedBlockingQueue::close` allows draining the remaining data). If you want "stop immediately" behavior (discarding unexecuted tasks), you need to modify the shutdown logic.

### Verification

```cpp
TEST_CASE("Milestone 4: shutdown drains pending tasks",
          "[lab3][milestone4]")
{
    auto pool = std::make_unique<ThreadPool>(2);
    std::atomic<int> counter{0};

    std::vector<std::future<void>> futures;
    for (int i = 0; i < 50; ++i) {
        futures.push_back(
            pool->submit([&counter]() {
                counter.fetch_add(1);
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(10));
            }));
    }

    pool->shutdown();

    // Every future should be gettable (all tasks were executed)
    for (auto& f : futures) {
        REQUIRE_NOTHROW(f.get());
    }
    REQUIRE(counter.load() == 50);
}

TEST_CASE("Milestone 4: submit after shutdown throws",
          "[lab3][milestone4]")
{
    ThreadPool pool(2);
    pool.shutdown();

    REQUIRE_THROWS_AS(
        pool.submit([]() { return 42; }),
        std::runtime_error);
}

TEST_CASE("Milestone 4: destructor calls shutdown",
          "[lab3][milestone4]")
{
    std::atomic<int> counter{0};
    {
        ThreadPool pool(4);
        for (int i = 0; i < 20; ++i) {
            pool.submit([&counter]() {
                counter.fetch_add(1);
            });
        }
    }  // destructor → shutdown → drain → join

    REQUIRE(counter.load() == 20);
}
```

## Milestone 5: Optional Capacity and Backpressure Policies

### Goal

Add a capacity limit to the pool's task queue and implement three backpressure policies: block (wait for space), reject (refuse immediately), and caller-runs (the calling thread executes the task).

### Why

Unbounded queues are dangerous in production — if consumers can't keep up with producers, the queue grows without bound and eventually exhausts memory. A bounded queue plus a backpressure policy is the standard design for production-grade thread pools. Each of the three policies has its niche: block suits scenarios where losing tasks is unacceptable, reject suits high-throughput scenarios that can tolerate lost tasks, and caller-runs suits scenarios that want automatic slowdown.

### Implementation Guide

Add capacity-check logic to `submit`. `BoundedBlockingQueue` already has a capacity limit and `try_push_for`, so the implementation is relatively direct.

- **block**: just use `push()` (blocks waiting for space)
- **reject**: use `try_push_for(timeout=0)` and throw on failure
- **caller-runs**: when `try_push_for` fails, execute the task directly on the current thread

The backpressure policy can be passed in as a constructor parameter, or implemented via a template policy parameter. To keep things simple, this lab recommends an enum:

```cpp

enum class BackpressurePolicy {
    kBlock,
    kReject,
    kCallerRuns
};

```

### Verification

```cpp
TEST_CASE("Milestone 5: block policy waits for space",
          "[lab3][milestone5]")
{
    ThreadPool pool(2, BackpressurePolicy::kBlock,
                    4);  // queue capacity 4
    std::atomic<int> counter{0};

    // Submit a large batch of tasks; all should succeed (they'll block waiting)
    std::vector<std::future<void>> futures;
    for (int i = 0; i < 20; ++i) {
        futures.push_back(pool.submit([&counter]() {
            counter.fetch_add(1);
            std::this_thread::sleep_for(
                std::chrono::milliseconds(50));
        }));
    }

    for (auto& f : futures) f.get();
    REQUIRE(counter.load() == 20);
}

TEST_CASE("Milestone 5: reject policy throws on full queue",
          "[lab3][milestone5]")
{
    ThreadPool pool(2, BackpressurePolicy::kReject, 2);
    std::atomic<int> counter{0};

    // Fill up the queue
    std::vector<std::future<void>> futures;
    for (int i = 0; i < 10; ++i) {
        try {
            futures.push_back(pool.submit([&counter]() {
                counter.fetch_add(1);
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(100));
            }));
        }
        catch (const std::runtime_error&) {
            // queue full — some rejections are expected
        }
    }

    for (auto& f : futures) f.get();
    REQUIRE(counter.load() <= 10);
}
```

## Self-Check Checklist

- [ ] The basic thread pool executes tasks concurrently with none lost
- [ ] The `future` returned by `submit` yields the correct return value
- [ ] When a task throws, `future::get()` rethrows it
- [ ] Move-only arguments (`unique_ptr`) pass through correctly
- [ ] `shutdown()` drains the queue and rejects new submissions
- [ ] The destructor calls `shutdown()` and joins all workers
- [ ] `shutdown()` is idempotent — repeated calls cause no trouble
- [ ] The backpressure policies behave as expected
- [ ] All tests run under TSan with no data race reports
- [ ] You can explain what problem `shared_ptr<packaged_task>` solves (why a bare `packaged_task` won't do)
- [ ] You can explain the shutdown trade-off between "drain the queue" and "drop the tasks"
- [ ] You can state out loud that this thread pool will be used directly in the Capstone project
