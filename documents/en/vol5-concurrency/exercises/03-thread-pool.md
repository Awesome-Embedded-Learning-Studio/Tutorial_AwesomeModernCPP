---
title: 'Lab 3: Production-style Thread Pool'
description: Implement a fixed-size thread pool, mastering future, packaged_task,
  exception propagation, graceful shutdown, and backpressure strategies.
chapter: 10
order: 4
difficulty: advanced
tags:
- host
- cpp-modern
- advanced
cpp_standard:
- 17
- 20
reading_time_minutes: 16
prerequisites:
- '卷五 ch05: future、任务与线程池'
- 'Lab 0: Thread Lifecycle Lab'
- 'Lab 1: Bounded Queue, Concurrent Cache and Sync Primitives'
translation:
  source: documents/vol5-concurrency/exercises/03-thread-pool.md
  source_hash: 79a9c2a6b736d7e5080460a44e5e05fc556e161f20bad161dde1916d6fe9aff6
  translated_at: '2026-05-26T11:48:01.981549+00:00'
  engine: anthropic
  token_count: 3190
---
# Lab 3: Production-style Thread Pool

## Objectives

The thread pool is the project in Volume Five best suited for a CS144-style assignment. It ties together knowledge from all previous Labs—`JoiningThread` for managing thread lifecycles, `BoundedBlockingQueue` as the task queue, atomics for statistics, and shutdown semantics for graceful exit. But a thread pool is more than a simple assembly of these components—it introduces several new engineering challenges: type erasure for `std::future` and `packaged_task`, cross-thread exception propagation, move-only task support, and the drain strategy for the task queue during shutdown.

After completing this Lab, you should have a thread pool component with a clean interface, testability, proper shutdown, and exception propagation—ready to be used directly in the Capstone project.

## Prerequisites

Before starting, make sure you have read the following sections:

- **ch05-01**: std::async and future — `std::future`, `std::promise`, `std::async`
- **ch05-02**: promise and packaged_task — `std::packaged_task`, type erasure
- **ch05-03**: jthread and stop_token — C++20 cooperative cancellation
- **ch05-04**: Thread pool design — Basic architecture and design considerations for thread pools
- **Lab 0**: Implementation of `JoiningThread`
- **Lab 1**: Implementation of `BoundedBlockingQueue` (reused directly in this Lab)

## Environment Setup

Same as Lab 1 (C++20, Catch2 v3, TSan).

## Final Interface

### `ThreadPool` — Fixed-size thread pool (non-copyable, destructor triggers automatic shutdown)

Type alias: `using Task = std::function<void()>;` (type-erased task wrapper)

Member variables:

| Type | Member | Semantics |
|------|--------|-----------|
| `BoundedBlockingQueue<Task>` | `task_queue_` | Task queue (reused from Lab 1) |
| `std::vector<JoiningThread>` | `workers_` | Worker thread collection (reused from Lab 0) |
| `std::atomic<bool>` | `stopped_` | Shutdown flag |

Interface:

| Method | Signature | Description | Milestone |
|--------|-----------|-------------|-----------|
| Constructor | `ThreadPool(size_t thread_count)` | Creates the specified number of worker threads | MS1 |
| Destructor | `~ThreadPool() noexcept` | Calls shutdown(), waits for all tasks to complete | MS4 |
| submit | `auto submit(F&&, Args&&...) -> future<invoke_result_t<F, Args...>>` | Submits a task, returns a future; throws if already shut down | MS2 |
| shutdown | `void shutdown()` | Drains the queue, rejects new submissions, joins all workers | MS4 |
| pending_tasks | `size_t pending_tasks() const` | Current number of tasks in the queue | MS1 |

## Milestone 1: Basic Thread Pool

### Objective

Implement the most basic thread pool: a fixed number of workers, a shared task queue, and stop-and-join on destruction. `submit` accepts tasks of type `std::function<void()>` and does not return a future.

### Why

We first get the basic architecture of "multiple workers fetching tasks from a shared queue" working, without involving templates, futures, or exception propagation. Once this skeleton is in place, subsequent milestones simply layer functionality on top of it.

### Implementation Guide

The core structure is `BoundedBlockingQueue<Task>` + `std::vector<JoiningThread>`. The loop logic for each worker thread is simple: `pop` a task from the queue, execute it, and fetch the next one. When the queue is closed and empty, the worker exits the loop.

```cpp

void worker_loop() {
    while (auto task = task_queue_.pop()) {
        (*task)();  // 执行任务
    }
}

```

Create N workers in the constructor:

```cpp

ThreadPool(size_t count)
    : task_queue_(256)  // 队列容量
{
    for (size_t i = 0; i < count; ++i) {
        workers_.emplace_back(&ThreadPool::worker_loop, this);
    }
}

```

Pitfall warning: When `worker_loop` is passed as a member function to `JoiningThread`, the first argument is a `this` pointer. Ensure the thread pool object's lifetime exceeds all workers—the destructor must close the queue and wait for all workers to exit first. Also, how large should the capacity of `BoundedBlockingQueue` be? 256 is a good default—too large wastes memory, too small easily blocks the submitting thread. If you don't want an upper limit, you can use a very large value or implement an unbounded queue yourself, but this Lab recommends using a bounded queue.

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

    // 等待所有任务完成
    // 注意：基础版本的 submit 不返回 future
    // 需要通过其他方式等待——这里用一个简单的 sleep
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
    }  // pool 析构 → shutdown → join

    REQUIRE(counter.load() == 50);
}
```

## Milestone 2: submit Returns a Future

### Objective

Implement a template version of `submit` that accepts any callable object and arguments, and returns a `std::future<R>`. The caller uses `future::get()` to retrieve the task's return value.

### Why

The basic version of `submit` only accepts `std::function<void()>`, so the caller cannot get the task's return value. In real-world engineering, thread pool callers almost always need to know the task's result—whether it's successfully returned data or a thrown exception. `std::future` + `std::packaged_task` is the "cross-thread result passing" mechanism provided by the C++ standard.

### Implementation Guide

The core idea is to wrap the user-submitted callable into a `std::packaged_task<R()>`, then return the `future` of the `packaged_task` to the caller, and push the `packaged_task` itself (wrapped as a `std::function<void()>`) into the task queue.

Pseudocode:

```cpp
template <class F, class... Args>
auto submit(F&& f, Args&&... args)
    -> future<invoke_result_t<F, Args...>>
{
    using R = invoke_result_t<F, Args...>;

    // 把 f(args...) 绑定成一个无参可调用对象
    auto task = make_shared<packaged_task<R()>>(
        bind(forward<F>(f), forward<Args>(args)...)
    );

    future<R> result = task->get_future();

    // 包装成 function<void()> 放进队列
    task_queue_.push([task]() { (*task)(); });

    return result;
}
```

We use `std::shared_ptr<packaged_task>` here because `packaged_task` is move-only (not copyable), while `std::function` requires a copy-constructible type. By placing the `packaged_task` inside a `shared_ptr` and having the lambda capture the `shared_ptr` (which is copyable), we solve this problem.

Pitfall warning: `std::bind` has pitfalls when handling reference parameters. If your callable accepts reference parameters, `bind` might decay the reference semantics. A safer approach is to use a lambda for binding:

```cpp
auto wrapper = [f = forward<F>(f),
                ... args = forward<Args>(args)]() mutable {
    return f(args...);
};
```

C++20 lambda init-captures support parameter pack expansion (`... args = forward<Args>(args)`). If your compiler does not support this, you can use `std::tuple` to store the arguments.

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

    f.get();  // 不应抛异常
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

### Objective

Ensure `future::get()` can rethrow exceptions from tasks. Support move-only type arguments (such as `std::unique_ptr`).

### Why

Exception propagation is the most easily overlooked part of thread pool design. If a task throws an exception and `future::get()` does not rethrow it, the exception is silently swallowed—the caller has no idea the task failed. The good news is that `std::packaged_task` already handles exception propagation—when a task throws, `packaged_task` catches it and stores it in the `future`, and it is rethrown upon `get()`. So the main work for this milestone is not "implementing" exception propagation, but "verifying" that it works correctly and ensuring your `submit` implementation doesn't accidentally swallow exceptions.

Support for move-only arguments is more straightforward—`std::packaged_task` itself is move-only, and lambdas can also capture move-only types. You need to ensure that nowhere along the entire delivery chain, from `submit` to worker execution, is a copy forced.

### Implementation Guide

If your Milestone 2 implementation used `shared_ptr<packaged_task>`, exception propagation already works automatically. You just need to verify it.

For move-only arguments, use lambda init-captures to pass them:

```cpp

auto ptr = make_unique<Data>(42);
auto f = pool.submit(`[p = move(ptr)]()` {
    return p->compute();
});

```

Pitfall warning: Do not use `std::ref` in `submit`'s parameters to pass move-only types—`std::ref` does not transfer ownership; it merely creates a reference wrapper, and the referenced object might have already been destroyed by the time the worker executes.

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

### Objective

Implement the `shutdown()` method: drain existing tasks in the queue, but reject new submissions. The destructor calls `shutdown()` and waits for all workers to exit.

### Why

Shutdown is the part of thread pool design that truly tests your architecture. A production-grade thread pool shutdown must simultaneously satisfy three conditions: existing tasks are fully executed (no data loss), new submissions are rejected (with a clear error signal), and all worker threads are joined (no leaks). Failing to meet any one of these conditions is an engineering defect—losing tasks leads to incomplete data, not rejecting new submissions leads to infinite waits, and not joining leads to a `std::terminate()`.

### Implementation Guide

The implementation idea for `shutdown()` is: set the `stopped_` flag to true, then `close()` the task queue. The worker loop remains unchanged—it exits when `pop` returns `nullopt`. `submit` throws an exception (or returns a future with a broken promise) when `stopped_` is true.

```cpp
void shutdown() {
    bool expected = false;
    if (!stopped_.compare_exchange_strong(expected, true)) {
        return;  // 已经关闭了
    }
    task_queue_.close();
    // workers_ 的析构会自动 join
}
```

The destructor calls `shutdown()`:

```cpp
~ThreadPool() noexcept {
    shutdown();
    // workers_ 的 JoiningThread 析构时自动 join
}
```

Pitfall warning: `shutdown()` must be idempotent—calling it multiple times should not cause issues. Use `compare_exchange_strong` to guarantee that only one thread executes the shutdown logic. Additionally, if there are backlogged tasks in the queue, workers will still execute them after `close()` (because `BoundedBlockingQueue::close` allows draining remaining data). If you want "immediate stop" behavior (discarding unexecuted tasks), you need to modify the shutdown logic.

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

    // 所有 future 应该都能 get（任务都被执行了）
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
    }  // 析构 → shutdown → drain → join

    REQUIRE(counter.load() == 20);
}
```

## Milestone 5: Optional Capacity and Backpressure Strategy

### Objective

Add a capacity limit to the thread pool's task queue, implementing three backpressure strategies: block (wait for space), reject (reject immediately), and caller-runs (execute on the caller's thread).

### Why

Unbounded queues are dangerous in production environments—if consumers can't keep up with producers, the queue will grow indefinitely and eventually exhaust memory. A bounded queue combined with a backpressure strategy is the standard design for production-grade thread pools. Each of the three strategies has its own applicable scenarios: block suits scenarios where task loss is unacceptable, reject suits high-throughput scenarios that can tolerate task loss, and caller-runs suits scenarios where automatic throttling is desired.

### Implementation Guide

Add capacity check logic in `submit`. `BoundedBlockingQueue` already has a capacity limit and `try_push_for`, so the implementation is relatively straightforward.

- **block**: Directly use `push()` (block waiting for space)
- **reject**: Use `try_push_for(timeout=0)`, throwing an exception on failure
- **caller-runs**: Execute the task directly on the current thread when `try_push_for` fails

The backpressure strategy can be passed in as a constructor parameter, or implemented via a template strategy parameter. For simplicity, this Lab recommends using an enum:

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
                    4);  // 队列容量 4
    std::atomic<int> counter{0};

    // 提交大量任务，应该都能成功（会阻塞等待）
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

    // 填满队列
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
            // 队列满了，预期会被拒绝一部分
        }
    }

    for (auto& f : futures) f.get();
    REQUIRE(counter.load() <= 10);
}
```

## Checklist

- [ ] The basic thread pool can execute tasks concurrently without loss
- [ ] The `future` returned by `submit` can retrieve the correct return value
- [ ] When a task throws an exception, `future::get()` can rethrow it
- [ ] Move-only arguments (`unique_ptr`) can be passed correctly
- [ ] `shutdown()` drains the queue and rejects new submissions
- [ ] The destructor calls `shutdown()` and joins all workers
- [ ] `shutdown()` is idempotent, calling it multiple times causes no issues
- [ ] The backpressure strategy behaves as expected
- [ ] All tests pass under TSan with no data race reports
- [ ] Can explain what problem `shared_ptr<packaged_task>` solves (why we can't just use `packaged_task` directly)
- [ ] Can explain the trade-off between "draining the queue" and "discarding tasks" during shutdown
- [ ] Can verbally describe how this thread pool will be used directly in the Capstone project
