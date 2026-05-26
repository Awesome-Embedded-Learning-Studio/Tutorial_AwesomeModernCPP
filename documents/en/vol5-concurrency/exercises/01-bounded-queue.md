---
title: 'Lab 1: Bounded Queue, Concurrent Cache and Sync Primitives'
description: Master mutex, condition_variable, shutdown semantics, and backpressure
  strategies through hands-on practice with blocking queues, sharded caches, and C++20
  synchronization primitives.
chapter: 10
order: 1
difficulty: intermediate
tags:
- host
- cpp-modern
- mutex
- intermediate
cpp_standard:
- 17
- 20
reading_time_minutes: 29
prerequisites:
- '卷五 ch00: 并发思维与基础'
- '卷五 ch01: 线程生命周期与 RAII'
- '卷五 ch02: 互斥量、条件变量与同步原语'
- 'Lab 0: Thread Lifecycle Lab'
translation:
  source: documents/vol5-concurrency/exercises/01-bounded-queue.md
  source_hash: 0662020f6d904e3b61908b6d4799141b7a25d84bbc5942ed4e93af653c51cfa3
  translated_at: '2026-05-26T11:47:05.566996+00:00'
  engine: anthropic
  token_count: 5613
---
# Lab 1: Bounded Queue, Concurrent Cache and Sync Primitives

## Objectives

Lab 0 got us up and running with the basic skeleton of multithreading—creating threads, RAII wrappers, and passing arguments safely. But those examples all shared one trait: every thread did its own thing, and the main thread simply waited for them to finish. Real concurrent systems are far from this—threads need to cooperate. Producers push data into a queue, consumers pull data out, a full queue applies backpressure, and a closed queue requires a graceful exit.

The core deliverables of this Lab are three components: a bounded blocking queue with shutdown semantics, a sharded-lock cache, and classic concurrency patterns implemented with C++20's `latch`, `barrier`, and `semaphore`. These three components are not isolated exercises—Lab 3's thread pool will directly reuse the bounded queue as its task queue, and the Capstone project will combine all of these components.

After completing this Lab, you should have muscle memory for the mutex + condition_variable combo. You should be able to correctly handle four waiting scenarios: predicated waits, spurious wakeups, lost wakeups, and shutdown wakeups. You should also understand the performance trade-offs between coarse-grained and fine-grained locking.

## Prerequisites

Before starting, make sure you have read the following chapters:

- **ch02-01**: mutex and RAII locks — `std::mutex`, `std::lock_guard`, `std::unique_lock`, `std::scoped_lock`
- **ch02-02**: Deadlock and lock ordering — deadlock prevention, `std::lock` for acquiring multiple locks simultaneously
- **ch02-03**: condition_variable and wait semantics — predicated waits, spurious wakeups, notify_one vs notify_all
- **ch02-04**: shared_mutex and read-write locks — shared locks, read-write separation scenarios
- **ch02-05**: latch, barrier, and semaphore — C++20 synchronization primitives
- **Lab 0**: Implementation and usage of `jthread`

This Lab directly depends on Lab 0's `jthread` component.

## Environment Setup

Use the same compiler and Catch2 configuration as Lab 0. New requirements:

- **C++20**: Milestone 6 requires `std::latch`, `std::barrier`, and `std::counting_semaphore`, which need GCC 12+ or Clang 15+ with `-std=c++20` enabled
- **pthread**: Link with `-lpthread` on Linux

In CMakeLists.txt, change the C++ standard from Lab 0 to 20, and ensure pthread is linked:

```cmake
cmake_minimum_required(VERSION 3.14)
project(lab1_bounded_queue LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
)
FetchContent_MakeAvailable(Catch2)

add_executable(lab1_tests tests/main.cpp)
target_link_libraries(lab1_tests PRIVATE Catch2::Catch2WithMain)

target_compile_options(lab1_tests PRIVATE
    $<$<CONFIG:Debug>:-fsanitize=thread -g>
)
target_link_options(lab1_tests PRIVATE
    $<$<CONFIG:Debug>:-fsanitize=thread>
)
```

## Final Interfaces

### `BoundedBlockingQueue` — Bounded blocking queue with shutdown semantics

Member variables:

| Type | Member | Semantics |
|------|--------|-----------|
| `std::queue<T>` | `queue_` | Internal data storage |
| `mutable std::mutex` | `mutex_` | Mutex protecting queue state |
| `std::condition_variable` | `not_full_` | Producer wait condition (queue not full) |
| `std::condition_variable` | `not_empty_` | Consumer wait condition (queue not empty) |
| `std::size_t` | `capacity_` | Maximum queue capacity |
| `bool` | `closed_` | Shutdown flag |

Interface:

| Method | Signature | Description | Milestone |
|--------|-----------|-------------|-----------|
| Constructor | `BoundedBlockingQueue(size_t capacity)` | Set queue capacity | MS1 |
| push | `bool push(T item)` | Blocking write; returns false after shutdown | MS1 |
| pop | `std::optional<T> pop()` | Blocking read; returns nullopt when shutdown and empty | MS1 |
| close | `void close()` | Close the queue, wake all waiting threads | MS2 |
| is_closed | `bool is_closed() const` | Query shutdown state | MS2 |
| try_push_for | `bool try_push_for(T, milliseconds)` | Timed write | MS3 |
| try_pop_for | `std::optional<T> try_pop_for(milliseconds)` | Timed read | MS3 |
| size | `size_t size() const` | Current queue length | MS1 |

### `ShardedCache` — Sharded-lock cache (Milestone 5)

Internally defines a `Shard` struct, containing a `std::shared_mutex` + `std::unordered_map`.

Member variables:

| Type | Member | Semantics |
|------|--------|-----------|
| `std::vector<Shard>` | `shards_` | Shard array, defaulting to 16 |
| `std::hash<K>` | `hasher_` | Used to hash a key to a shard |

Interface:

| Method | Signature | Description | Milestone |
|--------|-----------|-------------|-----------|
| Constructor | `ConcurrentCache(size_t num_shards = 16)` | Set number of shards | MS5 |
| put | `void put(const K&, const V&)` | Write a key-value pair (exclusive lock) | MS5 |
| get | `std::optional<V> get(const K&)` | Query a value (shared lock) | MS5 |
| erase | `void erase(const K&)` | Delete a key | MS5 |
| size | `size_t size() const` | Total number of entries | MS5 |

## Milestone 1: Fixed-Capacity Blocking Queue

### Objective

Implement the `push` and `pop` methods of `BoundedBlockingQueue`—fixed capacity, blocking writes, and blocking reads. This milestone ignores shutdown semantics and timeouts for now, focusing solely on the most basic mutex + condition_variable coordination.

### Why

The blocking queue is the most classic synchronization component in concurrent programming, and it is the most intuitive application scenario for mutex and condition_variable. It turns the abstract "producer-consumer" model into a concrete, testable data structure. All subsequent milestones build on this foundation by adding features—shutdown, timeouts, and backpressure—so we need to get it right first.

### Implementation Guide

The core data structure is simple: a `std::deque`, a `std::mutex`, two `std::condition_variable`s (one `not_full` for producers, one `not_empty` for consumers), and a capacity limit.

The logic for `push` is: lock → check if the queue is full → if full, `wait` on `not_full` → push the element into the queue → `notify_one` to wake a consumer. `pop` is the mirror operation: lock → check if the queue is empty → if empty, `wait` on `not_empty` → pop an element → `notify_one` to wake a producer.

There are a few places where we must use predicated waits. The wait in `push` cannot be written as a bare `wait(lock)`, it must be written as `wait(lock, predicate)`. Why? Because condition_variable has two annoying traits—spurious wakeups (waking up without a notify) and lost wakeups (notify happening before the wait). Predicated waits solve both problems at once: every time we wake up (whether genuinely or spuriously), we re-check the condition, and if it isn't met, we go back to waiting.

Pitfall warning: If you use `notify_one` instead of `notify_all`, make sure the awakened thread can actually make progress. In our scenario, a single push operation releases at most one consumer (the queue transitions from empty to non-empty), so `notify_one` is correct. But if you change something to a batch operation (like `push_many`), you might need `notify_all`.

### Verification

```cpp
#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <vector>
#include <atomic>

TEST_CASE("Milestone 1: single producer single consumer",
          "[lab1][milestone1]")
{
    BoundedBlockingQueue<int> queue(5);
    const int kItems = 100;
    std::atomic<int> sum{0};

    // 生产者
    JoiningThread producer([&]() {
        for (int i = 1; i <= kItems; ++i) {
            queue.push(i);
        }
    });

    // 消费者
    JoiningThread consumer([&]() {
        for (int i = 0; i < kItems; ++i) {
            auto val = queue.pop();
            if (val) {
                sum += *val;
            }
        }
    });

    // 等待完成后验证
    // sum 应该等于 1+2+...+100 = 5050
    // 注意：因为队列没有关闭，消费者目前会死锁
    // 这个测试需要 Milestone 2 的 close() 才能正确运行
    // 现在先验证 push/pop 基本功能
}

TEST_CASE("Milestone 1: queue respects capacity",
          "[lab1][milestone1]")
{
    BoundedBlockingQueue<int> queue(3);

    REQUIRE(queue.push(1));
    REQUIRE(queue.push(2));
    REQUIRE(queue.push(3));
    // 队列满了，下一个 push 会阻塞
    // 需要先 pop 一个才能继续 push
    auto val = queue.pop();
    REQUIRE(val.has_value());
    REQUIRE(*val == 1);
    REQUIRE(queue.push(4));  // 现在有空间了
}

TEST_CASE("Milestone 1: multiple producers multiple consumers",
          "[lab1][milestone1]")
{
    BoundedBlockingQueue<int> queue(10);
    const int kProducers = 4;
    const int kItemsPerProducer = 50;
    const int kTotalItems = kProducers * kItemsPerProducer;

    std::atomic<int> produced_sum{0};
    std::atomic<int> consumed_sum{0};
    std::atomic<int> consumed_count{0};

    std::vector<JoiningThread> producers;
    for (int p = 0; p < kProducers; ++p) {
        producers.emplace_back([&, p]() {
            for (int i = 0; i < kItemsPerProducer; ++i) {
                int val = p * kItemsPerProducer + i + 1;
                queue.push(val);
                produced_sum += val;
            }
        });
    }

    // 单个消费者收集所有数据
    JoiningThread consumer([&]() {
        while (consumed_count.load() < kTotalItems) {
            auto val = queue.pop();
            if (val) {
                consumed_sum += *val;
                consumed_count.fetch_add(1);
            }
        }
    });

    // 注意：这个测试在生产者全部 push 完后，消费者恰好消费完时结束
    // 实际上需要 close() 来正确终止，见 Milestone 2
}
```

## Milestone 2: Shutdown Semantics

### Objective

Add a `close` method to `BoundedBlockingQueue`. After shutdown, no more pushes are allowed (returning `false`), but existing elements in the queue can still be popped (returning remaining data). When the queue is both empty and closed, pop returns `nullopt`. All currently blocking pushes and pops must be woken up.

### Why

A blocking queue without shutdown semantics is a ticking time bomb. Consider a typical producer-consumer scenario: the producer thread has already finished (end of file reached, data generation complete), but the consumer is still blocking on `not_empty`—waiting for data that will never arrive. The program just hangs. `close` is the tool that tells the consumer "no new data is coming, you can leave." It is not just an API method, but a critical part of the lifecycle management for the entire concurrent component—the thread pool shutdown in Lab 3 and the Channel close in Lab 5 both follow this exact same pattern.

### Implementation Guide

The idea behind `close` is: lock → set the shutdown flag to true → `notify_all` to wake every waiting producer and consumer. The key is that the wait loops in `push` and `pop` need to add a check for the shutdown flag.

The wait in `push` becomes: `wait(lock, [this] { return !is_full() || closed_; })`. After waking up, we first check `closed_`; if it is closed, we immediately return `false` without pushing data. The wait in `pop` becomes: `wait(lock, [this] { return !is_empty() || closed_; })`. After waking up, if the queue is empty and `closed_`, we return `nullopt`; if the queue is not empty (it might be closed but still has data), we pop the element normally.

There is a subtle point that is easy to overlook: `push` returns `false` after detecting `closed_`, which means this element was indeed not pushed in. But if you happen to have a `pop` waiting on `not_empty` right before `close`, the `notify_all` will wake it up, and it will return `nullopt` after detecting `closed_`. This behavior is reasonable—the caller knows the queue is closed and won't try again.

Pitfall warning: `close` must use `notify_all` instead of `notify_one`. Because shutdown is a "global event"—all waiting threads need to know the state has changed. Using `notify_one` might only wake one thread, leaving the others blocked.

### Verification

```cpp
TEST_CASE("Milestone 2: close prevents further pushes",
          "[lab1][milestone2]")
{
    BoundedBlockingQueue<int> queue(5);

    REQUIRE(queue.push(1));
    REQUIRE(queue.push(2));

    queue.close();
    REQUIRE(queue.is_closed());

    // 关闭后 push 应该失败
    REQUIRE_FALSE(queue.push(3));
}

TEST_CASE("Milestone 2: close allows draining remaining items",
          "[lab1][milestone2]")
{
    BoundedBlockingQueue<int> queue(5);

    queue.push(10);
    queue.push(20);
    queue.push(30);
    queue.close();

    // 关闭后仍可 pop 已有数据
    REQUIRE(queue.pop() == 10);
    REQUIRE(queue.pop() == 20);
    REQUIRE(queue.pop() == 30);

    // 耗尽后返回 nullopt
    REQUIRE(queue.pop() == std::nullopt);
}

TEST_CASE("Milestone 2: close wakes blocked threads",
          "[lab1][milestone2]")
{
    BoundedBlockingQueue<int> queue(2);

    // 塞满队列
    queue.push(1);
    queue.push(2);

    // push 会阻塞（队列满了）
    std::atomic<bool> push_returned{false};
    JoiningThread t([&]() {
        bool ok = queue.push(3);
        push_returned.store(true);
        // 应该返回 false（被 close 唤醒）
    });

    // 等一小段时间确保线程进入了 wait
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    queue.close();

    // push 线程应该被唤醒并返回
    // (JoiningThread 析构时会 join，确保线程结束)
}

TEST_CASE("Milestone 2: producer-consumer with close",
          "[lab1][milestone2]")
{
    BoundedBlockingQueue<int> queue(10);
    const int kItems = 100;
    std::vector<int> consumed;
    std::mutex consumed_mutex;

    // 生产者：生产完就关闭队列
    JoiningThread producer([&]() {
        for (int i = 1; i <= kItems; ++i) {
            queue.push(i);
        }
        queue.close();
    });

    // 消费者：pop 到 nullopt 就停止
    JoiningThread consumer([&]() {
        while (auto val = queue.pop()) {
            std::lock_guard lock(consumed_mutex);
            consumed.push_back(*val);
        }
    });

    // consumed 应该包含 1..100
    REQUIRE(consumed.size() == kItems);
    // 验证总和
    int sum = 0;
    for (int v : consumed) sum += v;
    REQUIRE(sum == kItems * (kItems + 1) / 2);
}
```

## Milestone 3: Timed Waits

### Objective

Implement `try_push_for` and `try_pop_for` to support timed waits. If the queue state does not change within the specified duration, return failure instead of waiting indefinitely.

### Why

In real systems, waiting indefinitely is dangerous—if a consumer's processing speed suddenly drops (for example, a downstream service times out), an entire group of producer threads might get stuck on `push`. Timed waits give the caller a chance to adopt alternative strategies when a wait takes too long: retry, drop, log a warning, or degrade gracefully. The backpressure strategy in Milestone 4 will directly use timed waits.

### Implementation Guide

The only difference between `try_push_for`/`try_pop_for` and `push`/`pop` is swapping `wait` for `wait_for`. `wait_for` checks the predicate on timeout or wakeup; if the predicate is not met and a timeout occurred, it returns `false`.

Pseudocode is as follows:

```cpp
bool try_push_for(T item, milliseconds timeout) {
    unique_lock lock(mutex_);
    bool ok = not_full_.wait_for(lock, timeout,
        [&] { return queue_.size() < capacity_ || closed_; });

    if (closed_) return false;
    if (!ok) return false;  // 超时

    queue_.push(std::move(item));
    not_empty_.notify_one();
    return true;
}
```

Pitfall warning: `wait_for` returning `false` does not necessarily mean a timeout occurred—it could also mean it was woken up but the predicate still isn't satisfied. You need to distinguish between "timed out" and "spuriously woken up but the condition still isn't met." In practice, when using the predicated version of `wait_for`, the return value simply indicates "whether the predicate is satisfied"—`true` means satisfied, `false` means not satisfied (which could be due to a timeout or other reasons). In your logic, if it returns `false`, it means the operation could not succeed within the timeout duration.

### Verification

```cpp
TEST_CASE("Milestone 3: try_push_for times out on full queue",
          "[lab1][milestone3]")
{
    BoundedBlockingQueue<int> queue(2);
    queue.push(1);
    queue.push(2);

    auto start = std::chrono::steady_clock::now();
    bool ok = queue.try_push_for(3, std::chrono::milliseconds(100));
    auto elapsed = std::chrono::steady_clock::now() - start;

    REQUIRE_FALSE(ok);
    // 应该在 100ms 左右超时，而不是立即返回
    REQUIRE(elapsed >= std::chrono::milliseconds(80));
}

TEST_CASE("Milestone 3: try_pop_for times out on empty queue",
          "[lab1][milestone3]")
{
    BoundedBlockingQueue<int> queue(5);

    auto start = std::chrono::steady_clock::now();
    auto val = queue.try_pop_for(std::chrono::milliseconds(100));
    auto elapsed = std::chrono::steady_clock::now() - start;

    REQUIRE_FALSE(val.has_value());
    REQUIRE(elapsed >= std::chrono::milliseconds(80));
}

TEST_CASE("Milestone 3: try_push_for succeeds when space available",
          "[lab1][milestone3]")
{
    BoundedBlockingQueue<int> queue(5);

    bool ok = queue.try_push_for(42, std::chrono::milliseconds(100));
    REQUIRE(ok);

    auto val = queue.try_pop_for(std::chrono::milliseconds(100));
    REQUIRE(val.has_value());
    REQUIRE(*val == 42);
}
```

## Milestone 4: Backpressure Strategies

### Objective

Build on `BoundedBlockingQueue` to implement two backpressure strategies: **blocking wait** (already implemented) and **caller-runs**. Write a producer-consumer pipeline to compare the behavior of both strategies under different producer/consumer speed ratios.

### Why

Backpressure is a core engineering problem in concurrent systems. When producers are faster than consumers, the queue will grow indefinitely (if unbounded) or producers will block (if it's a blocking queue) without a backpressure mechanism. Blocking is the simplest form of backpressure, but it occupies a thread—if all producers are blocked, the system deadlocks. The caller-runs strategy is an alternative: when the queue is full, instead of blocking the producer, we let the producer execute the consumer's work itself—relieving queue pressure without wasting a thread.

### Implementation Guide

The blocking strategy is already implemented in Milestone 1. The core idea behind the caller-runs strategy is: if the queue is full, don't call `push`; instead, directly execute the consumer logic on the current thread (the producer thread).

Pseudocode:

```cpp

// caller-runs 策略的提交逻辑
void submit_with_caller_runs(BoundedBlockingQueue<Task>& queue,
                              Task task,
                              std::function<void(Task&)> processor)
{
    if (!queue.try_push_for(std::move(task),
                            std::chrono::milliseconds(0))) {
        // 队列满了，生产者自己执行
        processor(task);
    }
}

```

You need to write a simple benchmark to compare the two strategies: fix the production rate (for example, 1,000 tasks per second), make the consumer's processing speed adjustable (simulated via `std::this_thread::sleep_for`), and observe how queue length and throughput change under different speed ratios. You don't need to pursue exact numbers; the focus is on using data to illustrate the applicable scenarios for each strategy.

### Verification

```cpp
TEST_CASE("Milestone 4: blocking strategy backpressures producers",
          "[lab1][milestone4]")
{
    BoundedBlockingQueue<int> queue(5);
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};

    // 慢速消费者
    JoiningThread consumer([&]() {
        while (auto val = queue.pop()) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(10));
            consumed.fetch_add(1);
        }
    });

    // 快速生产者：队列满了就阻塞
    JoiningThread producer([&]() {
        for (int i = 0; i < 50; ++i) {
            queue.push(i);
            produced.fetch_add(1);
        }
        queue.close();
    });

    // producer 会被阻塞在 push 上，因为消费者太慢
    // 验证 produced 和 consumed 最终一致
}

TEST_CASE("Milestone 4: caller-runs avoids blocking",
          "[lab1][milestone4]")
{
    BoundedBlockingQueue<int> queue(5);
    std::atomic<int> processed_by_caller{0};
    std::atomic<int> processed_by_consumer{0};

    auto processor = [&](int val) {
        // 模拟处理
    };

    // caller-runs 提交
    for (int i = 0; i < 20; ++i) {
        if (!queue.try_push_for(i,
                std::chrono::milliseconds(0))) {
            processor(i);
            processed_by_caller.fetch_add(1);
        }
    }
    queue.close();

    // 消费者处理队列中的任务
    JoiningThread consumer([&]() {
        while (auto val = queue.pop()) {
            processor(*val);
            processed_by_consumer.fetch_add(1);
        }
    });

    // 验证：caller 处理了一部分，消费者处理了一部分
    int total = processed_by_caller.load() +
                processed_by_consumer.load();
    REQUIRE(total == 20);
}
```

## Milestone 5: Sharded-Lock Cache

### Objective

Implement `ShardedCache` using sharded locking to reduce lock contention. Compare its throughput against a single-lock cache, and observe the impact of shard count on performance.

### Why

`BoundedBlockingQueue` uses a single mutex to protect the entire queue—in highly concurrent multithreaded scenarios, this lock can become a bottleneck. Sharded locking is a common optimization approach: split the data into N shards, where each shard has its own lock, and different shards can be accessed in parallel. A hash function determines which shard a key belongs to, and operations only lock the corresponding shard. This way, operations on different keys no longer contend for the same lock. Chapter ch02-04 covered the read-write separation of `shared_mutex`, and here we can go a step further by using `shared_mutex` for read-write sharding—read operations use a shared lock, while write operations use an exclusive lock.

### Implementation Guide

The core data structure of `ShardedCache` is `std::vector<Shard>`, where each `Shard` contains a `std::shared_mutex` and a `std::unordered_map`. The `put` and `get` operations first compute the hash of the key, take the modulo of the shard count to get the target shard, and then lock that shard to perform the operation.

Pseudocode for `put`:

```cpp
void put(const K& key, const V& value) {
    auto& shard = get_shard(key);           // 哈希到具体分片
    unique_lock lock(shard.mutex);          // 独占锁
    shard.map[key] = value;
}
```

Pseudocode for `get`:

```cpp
optional<V> get(const K& key) {
    auto& shard = get_shard(key);
    shared_lock lock(shard.mutex);          // 共享锁，允许多读
    auto it = shard.map.find(key);
    if (it != shard.map.end()) {
        return it->second;
    }
    return nullopt;
}
```

The shard count is typically chosen as a power of two (16, 32, 64) for efficient bitwise modulo operations. Too few shards (like 1) degrades to a single lock, while too many (like 1024) wastes memory. 16 is a good starting point.

### Verification

```cpp
TEST_CASE("Milestone 5: concurrent put and get",
          "[lab1][milestone5]")
{
    ConcurrentCache<int, std::string> cache(16);

    // 并发写入
    std::vector<JoiningThread> writers;
    for (int i = 0; i < 8; ++i) {
        writers.emplace_back([&cache, i]() {
            for (int j = 0; j < 100; ++j) {
                int key = i * 100 + j;
                cache.put(key,
                    "value_" + std::to_string(key));
            }
        });
    }

    // 并发读取
    std::atomic<int> hits{0};
    std::vector<JoiningThread> readers;
    for (int i = 0; i < 4; ++i) {
        readers.emplace_back([&cache, &hits, i]() {
            for (int j = 0; j < 100; ++j) {
                int key = i * 100 + j;
                if (cache.get(key)) {
                    hits.fetch_add(1);
                }
            }
        });
    }

    // 验证所有写入的数据都能读到
    for (int i = 0; i < 800; ++i) {
        auto val = cache.get(i);
        REQUIRE(val.has_value());
        REQUIRE(*val == "value_" + std::to_string(i));
    }
}

TEST_CASE("Milestone 5: erase removes entries",
          "[lab1][milestone5]")
{
    ConcurrentCache<std::string, int> cache(4);

    cache.put("a", 1);
    cache.put("b", 2);

    REQUIRE(cache.get("a") == 1);
    cache.erase("a");
    REQUIRE_FALSE(cache.get("a").has_value());
    REQUIRE(cache.get("b") == 2);
}
```

## Milestone 6: C++20 Synchronization Primitives in Practice

### Objective

Use `std::latch`, `std::barrier`, and `std::counting_semaphore` to implement three classic concurrency patterns: fork-join, phased parallel processing, and a resource pool.

### Why

Chapter ch02-05 introduced the APIs for these three C++20 synchronization primitives, but just reading the API is no substitute for using them in a real scenario. Each of these primitives solves a specific class of synchronization problems—latch solves "wait for a group of tasks to complete," barrier solves "multi-round synchronization," and semaphore solves "limiting the number of concurrent accesses." In real-world engineering, they are more concise and less error-prone than hand-rolled mutex + condition_variable combinations.

### Implementation Guide

**Fork-join pattern** (`latch`): The main thread dispatches N tasks to a thread pool, uses a latch to wait for all of them to complete, and then aggregates the results.

```cpp

void fork_join_example() {
    const int kTasks = 8;
    latch done(kTasks);
    vector<int> results(kTasks);

    for (int i = 0; i < kTasks; ++i) {
        JoiningThread([&done, &results, i]() {
            results[i] = compute(i);
            done.count_down();    // 完成一个任务
        });
    }

    done.wait();  // 等待所有任务完成
    // 汇总 results
}

```

**Phased parallel processing** (`barrier`): Multi-round map-reduce, where a barrier synchronizes at the end of each round, ensuring the output of the previous phase is the input to the next phase.

```cpp

void phased_parallel_example() {
    const int kWorkers = 4;
    barrier sync_point(kWorkers, `[]()` noexcept {
        // 每轮结束后的回调（可选）
    });

    vector<JoiningThread> workers;
    for (int i = 0; i < kWorkers; ++i) {
        workers.emplace_back([&sync_point, i]() {
            // Phase 1: map
            do_map_phase(i);
            sync_point.arrive_and_wait();

            // Phase 2: reduce
            do_reduce_phase(i);
            sync_point.arrive_and_wait();

            // Phase 3: sort
            do_sort_phase(i);
        });
    }
}

```

**Resource pool** (`semaphore`): Simulate a database connection pool with a maximum of 5 connections, competed for by multiple threads.

```cpp

void resource_pool_example() {
    counting_semaphore<5> pool(5);  // 5 个连接
    const int kClients = 20;

    vector<JoiningThread> clients;
    for (int i = 0; i < kClients; ++i) {
        clients.emplace_back([&pool, i]() {
            pool.acquire();           // 获取连接（最多 5 个并发）
            use_database(i);          // 使用连接
            pool.release();           // 释放连接
        });
    }
}
```

Pitfall warning: The completion function of `barrier` must be `noexcept`. If your completion function throws an exception, compilation will fail. The acquire/release of `semaphore` do not need to be on the same thread—a producer can release and a consumer can acquire, which differs from mutex's lock/unlock that must occur on the same thread.

### Verification

```cpp
TEST_CASE("Milestone 6: latch fork-join collects all results",
          "[lab1][milestone6]")
{
    const int kTasks = 8;
    std::latch done(kTasks);
    std::vector<int> results(kTasks, 0);

    std::vector<JoiningThread> threads;
    for (int i = 0; i < kTasks; ++i) {
        threads.emplace_back([&done, &results, i]() {
            results[i] = i * i;
            done.count_down();
        });
    }

    done.wait();

    // 所有任务都完成了
    for (int i = 0; i < kTasks; ++i) {
        REQUIRE(results[i] == i * i);
    }
}

TEST_CASE("Milestone 6: barrier synchronizes phases",
          "[lab1][milestone6]")
{
    const int kWorkers = 4;
    std::atomic<int> phase1_done_count{0};
    std::atomic<int> phase2_started_count{0};

    std::barrier sync(kWorkers);

    std::vector<JoiningThread> threads;
    for (int i = 0; i < kWorkers; ++i) {
        threads.emplace_back([&, i]() {
            // Phase 1
            phase1_done_count.fetch_add(1);
            sync.arrive_and_wait();

            // Phase 2: 确保 Phase 1 全部完成
            REQUIRE(phase1_done_count.load() == kWorkers);
            phase2_started_count.fetch_add(1);
        });
    }
}

TEST_CASE("Milestone 6: semaphore limits concurrency",
          "[lab1][milestone6]")
{
    std::counting_semaphore<5> sem(5);
    std::atomic<int> max_concurrent{0};
    std::atomic<int> current{0};

    const int kClients = 20;
    std::vector<JoiningThread> threads;
    for (int i = 0; i < kClients; ++i) {
        threads.emplace_back([&]() {
            sem.acquire();
            int c = current.fetch_add(1) + 1;
            // 更新最大并发数
            int old_max = max_concurrent.load();
            while (c > old_max &&
                   !max_concurrent.compare_exchange_weak(
                       old_max, c)) {}

            std::this_thread::sleep_for(
                std::chrono::milliseconds(10));

            current.fetch_sub(1);
            sem.release();
        });
    }

    // 最大并发数不应超过 5
    REQUIRE(max_concurrent.load() <= 5);
    REQUIRE(max_concurrent.load() >= 1);
}
```

## Self-Check List

- [ ] Milestone 1: `push` and `pop` use predicated waits, with no spurious wakeups or lost wakeups
- [ ] Milestone 2: After `close`, no more pushes are allowed, existing data can be popped, and blocked threads are woken up
- [ ] Milestone 3: `try_push_for` and `try_pop_for` return correctly after a timeout
- [ ] Milestone 4: Both backpressure strategies behave as expected, with simple performance comparison data
- [ ] Milestone 5: The sharded cache produces correct data under multithreaded stress tests, with no data races reported by TSan
- [ ] Milestone 6: The use cases for latch, barrier, and semaphore are correct, and all tests pass
- [ ] All tests pass under TSan with no data race reports
- [ ] Can explain when to use `notify_one` vs `notify_all`
- [ ] Can explain why `close` must use `notify_all`
- [ ] Can explain the performance advantages and costs of sharded locking compared to a single lock (extra memory, hash computation overhead)
- [ ] Can verbally explain that `BoundedBlockingQueue` will be reused in Lab 3's thread pool
