---
chapter: 10
cpp_standard:
- 20
description: Combine components from all labs in Volume V to build a mini concurrent
  runtime, training system design, component composition, and observability.
difficulty: advanced
order: 7
prerequisites:
- 'Lab 0: Thread Lifecycle Lab'
- 'Lab 1: Bounded Queue, Concurrent Cache and Sync Primitives'
- 'Lab 2: Atomic Metrics and SPSC Ring Buffer'
- 'Lab 2.5: Concurrency Debugging Lab'
- 'Lab 3: Production-style Thread Pool'
- 'Lab 4: Coroutine Scheduler and Event Loop'
- 'Lab 5: Channel or Actor Runtime'
reading_time_minutes: 7
tags:
- host
- cpp-modern
- coroutine
- advanced
title: 'Capstone: Mini Concurrent Runtime'
translation:
  engine: anthropic
  source: documents/vol5-concurrency/exercises/06-capstone-mini-runtime.md
  source_hash: 9703a584a9a9805fad187494a8070d1d93eba952e9c671217c54d1fc84edf144
  token_count: 1677
  translated_at: '2026-06-14T00:20:34.530410+00:00'
---
# Capstone: Mini Concurrent Runtime

## Objectives

Volume 5 moves from "learning many concurrency tools" to "composing concurrent systems." This Capstone does not pursue production-grade completeness, but requires you to combine the finished components from the previous 7 Labs to build a runnable mini-system—a mini concurrent runtime or network service framework.

The focus is not on implementing new components from scratch, but on answering three engineering questions: How do components connect? How does the system stop? How are errors propagated and handled?

## Prerequisites

Complete all Labs 0–5. This Capstone directly reuses components from previous Labs.

## Environment Setup

Same as Lab 4 (C++20, Linux/WSL2 for epoll, Catch2 v3, TSan).

## Recommended Components

Below is a list of recommended components for the mini runtime. Each component comes from a previous Lab:

| Component | Source Lab | Responsibility |
|-----------|------------|----------------|
| `JoiningThread` | Lab 0 | Thread lifecycle management |
| `BoundedBlockingQueue` | Lab 1 | Task queue / channel bottom layer |
| `ConcurrentCache` | Lab 1 | Config cache / connection pool |
| `AtomicCounter` / `AtomicMaxTracker` | Lab 2 | Runtime metrics |
| `StopFlag` | Lab 2 | Graceful shutdown signal |
| `ThreadPool` | Lab 3 | CPU-bound task scheduling |
| `Scheduler` + `EventLoop` | Lab 4 | Coroutine scheduling + I/O event loop |
| `Channel` | Lab 5 | Inter-component communication / pipeline |

## Milestone 1: Architecture Design and Interface Definition

### Objectives

Draw a component diagram of the mini runtime and define the interaction interfaces between components. Do not write any implementation code—this milestone is purely about design.

### Why

The first step of system design is not writing code, but clarifying the relationships and responsibility boundaries between components. Specifically, the three questions: "Who creates whom?", "Who owns whom?", and "Who can shut down whom?". In concurrent systems, these issues are much more important than in single-threaded systems—an incorrect ownership relationship can lead to deadlocks, resource leaks, or crashes during shutdown.

### Implementation Guide

Use a paragraph of text or a diagram to describe your runtime's architecture. It is recommended to start with "the complete path of a request from entry to exit":

```cpp
客户端请求 → epoll accept → 协程 handle_connection
    → Channel 传递给 worker pipeline
    → ThreadPool 处理 CPU-bound 任务
    → 结果通过 future 返回
    → 协程 write response → 客户端
```

On this path, mark the responsibility and lifecycle relationships of each component. For example: `EventLoop` owns the epoll fd and the coroutine scheduler; `ThreadPool` owns worker threads and the task queue; `Channel` connects the coroutine layer and the thread pool layer.

You need to answer the following design questions:

1. Between `EventLoop` and `ThreadPool`, which is created first and shut down first?
2. Who is responsible for closing `Channel`—the producer or the consumer?
3. How are exceptions from one component propagated to others?

### Verification

Discuss your design with a peer or AI to ensure no edge cases are missed. You don't need to write code, but you must be able to answer the three design questions above.

## Milestone 2: Component Assembly and Startup

### Objectives

Combine components from all Labs to implement the runtime's startup process. You don't need to handle network requests—just confirm that all components are initialized and running correctly.

### Why

The startup order of components is crucial. `ThreadPool` needs to be created before `Channel` (because worker threads need to fetch tasks from the channel), and `EventLoop` needs to be created before `ThreadPool` (because coroutine scheduling happens before I/O events). The goal of this milestone is to confirm that the startup order is correct and that there are no circular dependencies between components.

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

Pitfall Warning: The order of member declaration is the order of initialization, and destruction order is the reverse. Ensure `ThreadPool` is destroyed before `BoundedBlockingQueue` (because worker threads need to fetch data from the queue until the queue is closed), and `EventLoop` is destroyed before all channels.

### Verification

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
```cpp

## Milestone 3: Failure Path Testing

### Objectives

Test the runtime's behavior under various failure scenarios: tasks throwing exceptions, clients disconnecting, queues closing, and component exceptions.

### Why

The correctness of a concurrent system is not only reflected in the "happy path." A production-grade system must handle various failures gracefully—task execution failures should not crash the entire runtime, client disconnections should not leak resources, and component exceptions should be caught and reported rather than silently lost.

### Implementation Guide

Test the following scenarios:

1. **Task Exception**: Submit a task that throws an exception, confirm that `future::get()` can re-throw it, and that the runtime continues to run normally.
2. **Client Disconnect**: Simulate a client disconnecting during coroutine processing, confirm that the coroutine exits correctly without leaking resources.
3. **Queue Closure**: Close a middle channel while the pipeline is running, confirm that both upstream and downstream handle it correctly.
4. **Repeated Shutdown**: Call `stop()` multiple times to confirm idempotency.

### Verification

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

### Objectives

Add metrics collection (`AtomicCounter`, `AtomicMaxTracker`) to the runtime, implement at least one end-to-end benchmark, and verify correctness with TSan.

### Why

A concurrent system without observability is like a black box—you don't know what it is doing, how it performs, or if there are problems. The atomic metrics component from Lab 2 comes into play here: count completed tasks, current queue length, and maximum concurrent connections. These metrics don't need millisecond precision—their value lies in letting you see "the system is running" and "the system is degrading."

### Implementation Guide

Insert metrics collection points on the runtime's critical paths:

- When a task is submitted `active_tasks_.increment()`
- When a task is completed `active_tasks_.decrement()`
- When a new connection is established `max_connections_.update(current_connections)`
- Periodic sampling of queue length (optional)

Write an end-to-end benchmark: start the runtime, submit N tasks, wait for all futures to complete, and report total time and throughput. Follow Lab 2's benchmark methodology—warm up, take the median of multiple rounds, fix CPU affinity, report the test environment and boundaries, and don't just look at single runs or fluctuations within 5%.

Finally, run the full test suite with TSan to confirm there are no data races.

### Verification

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

## Checklist

- [ ] Components from all Labs 0–5 are correctly combined.
- [ ] Component creation and destruction order is correct (no circular dependencies, no dangling references).
- [ ] `stop()` is idempotent and does not deadlock or leak.
- [ ] There is a clear shutdown sequence: stop accepting new requests → drain queues → join all threads.
- [ ] Task exceptions do not cause the runtime to crash.
- [ ] Channel closure is correctly propagated to all stages of the pipeline.
- [ ] Metrics collection does not affect correctness (use `relaxed` atomic).
- [ ] At least one end-to-end benchmark exists, reporting throughput.
- [ ] The full test suite runs under TSan with no data race reports.
- [ ] Can answer: Where do we use locks, where do we use atomics, and where do we avoid shared state through message passing?
- [ ] Can explain what the benchmark results do not prove (e.g., "standalone tests do not represent performance in a network environment").
- [ ] Can describe which component you would prioritize improving if you had more time.
