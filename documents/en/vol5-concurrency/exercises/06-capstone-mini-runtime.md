---
title: 'Capstone: Mini Concurrent Runtime'
description: Combine components from all Labs in Volume 5 to build a mini concurrent
  runtime, practicing system design, component composition, and observability.
chapter: 10
order: 7
difficulty: advanced
tags:
- host
- cpp-modern
- coroutine
- advanced
cpp_standard:
- 20
reading_time_minutes: 9
prerequisites:
- 'Lab 0: Thread Lifecycle Lab'
- 'Lab 1: Bounded Queue, Concurrent Cache and Sync Primitives'
- 'Lab 2: Atomic Metrics and SPSC Ring Buffer'
- 'Lab 2.5: Concurrency Debugging Lab'
- 'Lab 3: Production-style Thread Pool'
- 'Lab 4: Coroutine Scheduler and Event Loop'
- 'Lab 5: Channel or Actor Runtime'
translation:
  source: documents/vol5-concurrency/exercises/06-capstone-mini-runtime.md
  source_hash: fcae6e1b85800920052211d645e7e4c7ca0b35c80c54a1d369cce3414dea1fcc
  translated_at: '2026-05-26T11:49:01.898502+00:00'
  engine: anthropic
  token_count: 1674
---
# Capstone: Mini Concurrent Runtime

## Goal

Volume 5 wraps up by shifting from "knowing many concurrency tools" to "being able to compose a concurrent system." This Capstone does not pursue production-grade completeness; instead, it requires you to combine the finished components from the previous seven Labs to build a runnable mini system—a mini concurrent runtime or network service framework.

The focus is not on implementing new components from scratch, but on answering three engineering questions: How do components connect? How does the system stop? How do errors propagate and get handled?

## Prerequisites

Complete all of Labs 0–5. This Capstone directly reuses components from previous Labs.

## Environment Setup

Same as Lab 4 (C++20, Linux/WSL2 for epoll, Catch2 v3, TSan).

## Recommended Components

Below is the recommended component list for the mini runtime. Each component comes from a previous Lab:

| Component | Source Lab | Responsibility |
|-----------|------------|----------------|
| `JoiningThread` | Lab 0 | Thread lifecycle management |
| `BoundedBlockingQueue` | Lab 1 | Task queue / channel underlying layer |
| `ConcurrentCache` | Lab 1 | Configuration cache / connection pool |
| `AtomicCounter` / `AtomicMaxTracker` | Lab 2 | Runtime metrics |
| `StopFlag` | Lab 2 | Graceful shutdown signal |
| `ThreadPool` | Lab 3 | CPU-bound task scheduling |
| `Scheduler` + `EventLoop` | Lab 4 | Coroutine scheduling + I/O event loop |
| `Channel` | Lab 5 | Inter-component communication / pipeline |

## Milestone 1: Architecture Design and Interface Definition

### Goal

Draw a component diagram of the mini runtime, and define the interaction interfaces between components. Do not write any implementation code—this milestone is purely about design.

### Why

The first step in system design is not writing code, but clarifying the relationships and responsibility boundaries between components. Specifically, three questions: "Who creates whom?", "Who owns whom?", and "Who can shut down whom?". In concurrent systems, these questions are far more important than in single-threaded systems—an incorrect ownership relationship can lead to dead lock, memory leak, or crashes during shutdown.

### Implementation Guide

Use a paragraph or a diagram to describe your runtime's architecture. We recommend starting from the "complete path of a request from entry to exit":

```cpp
客户端请求 → epoll accept → 协程 handle_connection
    → Channel 传递给 worker pipeline
    → ThreadPool 处理 CPU-bound 任务
    → 结果通过 future 返回
    → 协程 write response → 客户端
```

Along this path, annotate the responsibility and lifecycle relationship of each component. For example: `EventLoop` owns the epoll fd and the coroutine scheduler; `ThreadPool` owns the worker threads and the task queue; `Channel` connects the coroutine layer and the thread pool layer.

You need to answer the following design questions:

1. Between `EventLoop` and `ThreadPool`, which is created first, and which is shut down first?
2. Who is responsible for closing `Channel`—the producer or the consumer?
3. How does an exception in one component propagate to other components?

### Validation

Discuss your design with a peer or AI, and confirm there are no missing edge cases. You don't need to write code, but you must be able to answer the three design questions above.

## Milestone 2: Component Assembly and Startup

### Goal

Combine all Lab components together, and implement the runtime's startup flow. You don't need to handle network requests—just confirm that all components initialize and run correctly.

### Why

The startup order of components matters. `ThreadPool` must be created before `Channel` (because worker threads need to pull tasks from the channel), and `EventLoop` must be created before `ThreadPool` (because coroutine scheduling happens before I/O events). The goal of this milestone is to confirm the correct startup order and ensure there are no cyclic dependencies between components.

### Implementation Guide

Define a `MiniRuntime` class that creates and holds all components in the correct order:

```cpp
class MiniRuntime {
public:
    MiniRuntime()
        : metrics_()
        , task_queue_(256)
        , thread_pool_(4)
        , channels_()
        , event_loop_()
        , stop_flag_()
    {
        // 注册 metrics 回调
        // 启动 event loop 线程（如果需要独立线程）
    }

    void start();
    void stop();

private:
    AtomicCounter active_tasks_;
    AtomicMaxTracker max_connections_;
    StopFlag stop_flag_;
    ThreadPool thread_pool_;
    Channel<Request> request_channel_;
    EventLoop event_loop_;
};
```

Pitfall warning: The declaration order of members is the initialization order, and the destruction order is the reverse. Ensure that `ThreadPool` is destroyed before `BoundedBlockingQueue` (because worker threads need to pull data from the queue until the queue closes), and that `EventLoop` is destroyed before all channels.

### Validation

```cpp
TEST_CASE("Milestone 2: runtime starts and stops cleanly",
          "[capstone][milestone2]")
{
    MiniRuntime runtime;
    runtime.start();

    // 提交一些测试任务
    auto f1 = runtime.thread_pool().submit([]() {
        return 42;
    });
    REQUIRE(f1.get() == 42);

    runtime.stop();

    // stop 后不应该崩溃
    // 所有 worker 线程应该已经退出
}
```text

## Milestone 3: Failure Path Testing

### Goal

Test the runtime's behavior under various failure scenarios: tasks throwing exceptions, client disconnections, queue closures, and component exceptions.

### Why

The correctness of a concurrent system is not only reflected in the "happy path." A production-grade system must gracefully handle various failures—a task execution failure should not crash the entire runtime, a client disconnection should not cause a memory leak, and a component exception should be caught and reported rather than silently lost.

### Implementation Guide

Test the following scenarios:

1. **Task exception**: Submit a task that throws an exception, confirm that `future::get()` can rethrow it, and that the runtime continues running normally
2. **Client disconnection**: Simulate a client disconnecting during coroutine processing, confirm that the coroutine exits correctly without leaking resources
3. **Queue closure**: Close a middle channel while the pipeline is running, confirm that both upstream and downstream handle it correctly
4. **Repeated shutdown**: Call `stop()` multiple times, confirm idempotency

### Validation

```cpp
TEST_CASE("Milestone 3: task exception doesn't crash runtime",
          "[capstone][milestone3]")
{
    MiniRuntime runtime;
    runtime.start();

    auto f1 = runtime.thread_pool().submit([]() {
        throw std::runtime_error("boom");
    });
    auto f2 = runtime.thread_pool().submit([]() {
        return 42;
    });

    REQUIRE_THROWS_AS(f1.get(), std::runtime_error);
    REQUIRE(f2.get() == 42);  // 其他任务不受影响

    runtime.stop();
}

TEST_CASE("Milestone 3: double stop is safe",
          "[capstone][milestone3]")
{
    MiniRuntime runtime;
    runtime.start();
    runtime.stop();
    REQUIRE_NOTHROW(runtime.stop());  // 幂等
}

TEST_CASE("Milestone 3: channel close propagates through pipeline",
          "[capstone][milestone3]")
{
    Channel<int> input(8);
    Channel<int> output(8);

    JoiningThread stage([&]() {
        while (auto val = input.receive()) {
            output.send(*val * 2);
        }
        output.close();
    });

    input.send(1);
    input.send(2);
    input.close();  // 关闭触发 pipeline 关闭

    REQUIRE(output.receive() == 2);
    REQUIRE(output.receive() == 4);
    REQUIRE(output.receive() == std::nullopt);
}
```

## Milestone 4: Observability and Performance Validation

### Goal

Add metrics collection to the runtime (`AtomicCounter`, `AtomicMaxTracker`), implement at least one end-to-end benchmark, and validate correctness with TSan.

### Why

A concurrent system without observability is like a black box—you don't know what it's doing, how it performs, or whether it has problems. The atomic metrics component from Lab 2 comes into play here: count completed tasks, current queue length, and maximum concurrent connections. These metrics don't need to be precise to the millisecond—their value lies in letting you see "the system is running" and "the system is degrading."

### Implementation Guide

Insert metrics collection points at key paths in the runtime:

- When a task is submitted: `active_tasks_.increment()`
- When a task completes: `active_tasks_.decrement()`
- When a new connection is established: `max_connections_.update(current_connections)`
- Periodic sampling of queue length (optional)

Write an end-to-end benchmark: start the runtime, submit N tasks, wait for all futures to complete, and report the total time and throughput. Refer to the benchmark methodology in `.claude/chapter-projects-outline.md`.

Finally, run the complete test suite with TSan to confirm there are no data races.

### Validation

```cpp
TEST_CASE("Milestone 4: metrics track runtime behavior",
          "[capstone][milestone4]")
{
    MiniRuntime runtime;
    runtime.start();

    std::vector<std::future<int>> futures;
    for (int i = 0; i < 100; ++i) {
        futures.push_back(
            runtime.thread_pool().submit([i]() {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(1));
                return i;
            }));
    }

    for (auto& f : futures) f.get();

    REQUIRE(runtime.total_tasks_completed() == 100);

    runtime.stop();
}
```

## Self-Check List

- [ ] All components from Labs 0–5 are correctly combined
- [ ] Component creation order and destruction order are correct (no cyclic dependencies, no dangling references)
- [ ] `stop()` is idempotent, and does not dead lock or leak
- [ ] There is a clear shutdown sequence: stop accepting new requests → drain the queue → join all threads
- [ ] Task exceptions do not crash the runtime
- [ ] Channel closure correctly propagates to all stages of the pipeline
- [ ] Metrics collection does not affect correctness (using `relaxed` atomic)
- [ ] There is at least one end-to-end benchmark that reports throughput
- [ ] The complete test suite has no data race reports under TSan
- [ ] You can answer: where do you use locks, where do you use atomics, and where do you avoid shared state through message passing
- [ ] You can explain what the benchmark results do not prove (e.g., "local testing does not represent behavior in a network environment")
- [ ] You can describe which component you would prioritize improving if you had more time
