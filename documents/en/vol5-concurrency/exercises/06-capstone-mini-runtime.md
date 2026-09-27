---
chapter: 10
cpp_standard:
- 20
description: Combine components from every Volume 5 lab into a mini concurrent runtime, practicing system design, component composition, and observability
difficulty: advanced
order: 7
prerequisites:
- 'Lab 0: Thread Lifecycle'
- 'Lab 1: Bounded Queue, Concurrent Cache and Sync Primitives'
- 'Lab 2: Atomic Metrics and SPSC Ring Buffer'
- 'Lab 2.5: Concurrency Debugging'
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
  source: documents/vol5-concurrency/exercises/06-capstone-mini-runtime.md
  source_hash: 25bfcfb9e71e32a2c7e54c2fd0a87a4a22b56f4aaef109cc19e7e450af1025ec
  translated_at: '2026-09-26T09:30:49+00:00'
  engine: anthropic
  token_count: 1400
  notes: '原文一处明显笔误按正确形式译出：第 153 行 Milestone 2 验证代码块的闭合围栏误作 ```cpp，原文疑为 ```（CommonMark 闭合围栏不得带 info string，否则该代码块会一直吞到 Milestone 3 之后的裸 ```，把「## Milestone 3: 失败路径测试」标题一并吞入代码块）。译文以规范 ``` 闭合，en 侧渲染为 5 个独立代码块、结构恢复本意；zh 源第 153 行仍待修复。'
---
# Capstone: Mini Concurrent Runtime

## Objectives

Volume 5 has taken us from "we've learned a lot of concurrency tools" to the point where it all converges into "we can compose concurrent systems". This Capstone doesn't chase production-grade completeness — it asks you to combine the finished components from the previous 7 labs into a small system that actually runs: a mini concurrent runtime or a network service framework.

The point is not to implement new components from scratch, but to answer three engineering questions: how do the components connect? How does the system stop? When something goes wrong, how do errors propagate and get handled?

## Prerequisites

Complete all of Labs 0–5 first. This Capstone reuses those components directly.

## Environment Setup

Same as Lab 4 (C++20, Linux/WSL2 for epoll, Catch2 v3, TSan).

## Recommended Components

Here is the recommended component list for the mini runtime. Each component comes from one of the earlier labs:

| Component | Source Lab | Responsibility |
|------|----------|------|
| `JoiningThread` | Lab 0 | Thread lifecycle management |
| `BoundedBlockingQueue` | Lab 1 | Task queue / channel foundation |
| `ConcurrentCache` | Lab 1 | Config cache / connection pool |
| `AtomicCounter` / `AtomicMaxTracker` | Lab 2 | Runtime metrics |
| `StopFlag` | Lab 2 | Graceful stop signal |
| `ThreadPool` | Lab 3 | CPU-bound task scheduling |
| `Scheduler` + `EventLoop` | Lab 4 | Coroutine scheduling + I/O event loop |
| `Channel` | Lab 5 | Inter-component communication / pipeline |

## Milestone 1: Architecture Design and Interface Definition

### Objectives

Draw a component diagram of the mini runtime and define the interaction interfaces between components. Don't write any implementation code — this milestone is pure design.

### Why

The first step of system design isn't writing code; it's working out the relationships and responsibility boundaries between components. In particular, three questions: who creates whom, who owns whom, and who is allowed to shut whom down. In concurrent systems these questions matter far more than in single-threaded ones — a wrong ownership relationship can deadlock you, leak resources, or crash the whole thing during shutdown.

### Implementation Guide

Describe your runtime's architecture in a paragraph or a diagram. A good starting point is "the complete path of a request from entry to exit":

```cpp
Client request → epoll accept → coroutine handle_connection
    → Channel passes it to the worker pipeline
    → ThreadPool processes CPU-bound tasks
    → result returned via future
    → coroutine write response → client
```

Annotate each component's responsibilities and lifecycle relationships along this path. For example: `EventLoop` owns the epoll fd and the coroutine scheduler; `ThreadPool` owns the worker threads and the task queue; `Channel` bridges the coroutine layer and the thread pool layer.

You need to answer the following design questions:

1. Between `EventLoop` and `ThreadPool`, which is created first and which shuts down first?
2. Who is responsible for closing a `Channel` — the producer or the consumer?
3. How does an exception in one component propagate to the other components?

### Verification

Discuss your design with a peer or an AI and confirm no edge cases have been missed. No code is needed, but you must be able to answer the three design questions above.

## Milestone 2: Component Assembly and Startup

### Objectives

Put the components from all the labs together and implement the runtime's startup sequence. No need to handle network requests — just confirm that every component initializes and runs correctly.

### Why

The startup order of components matters. `ThreadPool` must be created before `Channel` (because worker threads pull tasks from the channel), and `EventLoop` must be created before `ThreadPool` (because coroutine scheduling comes before I/O events). The goal of this milestone is to confirm the startup order is correct and that the dependencies between components contain no cycles.

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
        // Register metrics callbacks
        // Start the event loop thread (if it needs a dedicated thread)
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

Pitfall warning: members initialize in declaration order and are destroyed in reverse order. Make sure `ThreadPool` is destroyed before `BoundedBlockingQueue` (because worker threads keep pulling data from the queue until it closes), and that `EventLoop` is destroyed before all channels.

### Verification

```cpp
TEST_CASE("Milestone 2: runtime starts and stops cleanly",
          "[capstone][milestone2]")
{
    MiniRuntime runtime;
    runtime.start();

    // Submit a few test tasks
    auto f1 = runtime.thread_pool().submit([]() {
        return 42;
    });
    REQUIRE(f1.get() == 42);

    runtime.stop();

    // Should not crash after stop
    // All worker threads should have exited
}
```

## Milestone 3: Failure Path Testing

### Objectives

Test the runtime's behavior under various failure scenarios: tasks throwing exceptions, clients disconnecting, queues closing, components raising exceptions.

### Why

The correctness of a concurrent system doesn't show only on the happy path. A production-grade system must handle failures gracefully — a failed task shouldn't crash the entire runtime, a disconnected client shouldn't leak resources, and a component's exception should be caught and reported rather than silently lost.

### Implementation Guide

Test the following scenarios:

1. **Task exception**: submit a task that throws, and confirm that `future::get()` re-throws it while the runtime keeps running normally
2. **Client disconnect**: simulate a client disconnecting while a coroutine is processing it, and confirm the coroutine exits correctly without leaking resources
3. **Queue closure**: close an intermediate channel while the pipeline is running, and confirm both upstream and downstream handle it correctly
4. **Repeated stop**: call `stop()` multiple times and confirm it is idempotent

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
    REQUIRE(f2.get() == 42);  // Other tasks are unaffected

    runtime.stop();
}

TEST_CASE("Milestone 3: double stop is safe",
          "[capstone][milestone3]")
{
    MiniRuntime runtime;
    runtime.start();
    runtime.stop();
    REQUIRE_NOTHROW(runtime.stop());  // Idempotent
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
    input.close();  // Closing triggers the pipeline shutdown

    REQUIRE(output.receive() == 2);
    REQUIRE(output.receive() == 4);
    REQUIRE(output.receive() == std::nullopt);
}
```

## Milestone 4: Observability and Performance Validation

### Objectives

Add metrics collection to the runtime (`AtomicCounter`, `AtomicMaxTracker`), implement at least one end-to-end benchmark, and verify correctness with TSan.

### Why

A concurrent system without observability is a black box — you can't tell what it is doing, how it performs, or whether something is wrong. This is where Lab 2's atomic metrics components come into play: counting completed tasks, current queue length, and the maximum number of concurrent connections. These metrics don't need millisecond precision — their value is letting you see "the system is running" and "the system is degrading".

### Implementation Guide

Insert metrics collection points on the runtime's critical paths:

- On task submission, `active_tasks_.increment()`
- On task completion, `active_tasks_.decrement()`
- On new connection establishment, `max_connections_.update(current_connections)`
- Sample the queue length periodically (optional)

Write an end-to-end benchmark: start the runtime, submit N tasks, wait for all futures to complete, and report total time and throughput. Reuse Lab 2's benchmark methodology — warm up first and take the median over multiple rounds, pin CPU affinity, report the test environment and its limits, and don't trust a single run or fluctuations within 5%.

Finally, run the complete test suite with TSan and confirm there are no data races.

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

## Self-Check List

- [ ] Components from all Labs 0–5 are combined correctly
- [ ] Component creation and destruction order is correct (no circular dependencies, no dangling references)
- [ ] `stop()` is idempotent — no deadlock, no leaks
- [ ] There is a clear shutdown sequence: stop accepting new requests → drain the queue → join all threads
- [ ] Task exceptions do not crash the runtime
- [ ] Channel closure propagates correctly to every stage of the pipeline
- [ ] Metrics collection doesn't affect correctness (use `relaxed` atomics)
- [ ] At least one end-to-end benchmark, reporting throughput
- [ ] The complete test suite shows no data race reports under TSan
- [ ] You can answer: where locks are used, where atomics are used, and where shared state is avoided through message passing
- [ ] You can explain what the benchmark results cannot prove (for example, "a single-machine test doesn't represent behavior under real network conditions")
- [ ] You can say which component you would improve first if you had more time
