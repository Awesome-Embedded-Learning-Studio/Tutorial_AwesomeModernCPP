---
chapter: 10
cpp_standard:
- 17
- 20
description: Master mutex, condition variable, shutdown semantics, and backpressure
  strategies through blocking queues, sharded caching, and C++20 synchronization primitives
difficulty: intermediate
order: 1
prerequisites:
- '卷五 ch00: 并发思维与基础'
- '卷五 ch01: 线程生命周期与 RAII'
- '卷五 ch02: 互斥量、条件变量与同步原语'
- 'Lab 0: Thread Lifecycle Lab'
reading_time_minutes: 21
tags:
- host
- cpp-modern
- mutex
- intermediate
title: 'Lab 1: Bounded Queue, Concurrent Cache and Sync Primitives'
translation:
  source: documents/vol5-concurrency/exercises/01-bounded-queue.md
  source_hash: fae391d05750bbd87df486d4a0c8221e4930dc5886264dce0f96a098f95a3cf3
  translated_at: '2026-06-16T04:07:18.983555+00:00'
  engine: anthropic
  token_count: 5609
---
# Lab 1: Bounded Queue, Concurrent Cache and Sync Primitives

## Objectives

Lab 0 got us up and running with the basic skeleton of multithreading—creating threads, RAII wrappers, and safe parameter passing. However, those code samples shared a common characteristic: all threads were "doing their own thing," and the main thread simply waited for them to finish. Real-world concurrent systems are far different—threads need to collaborate. Producers push data into queues, consumers pull data out, queues apply backpressure when full, and systems shut down gracefully when queues close.

The core deliverables of this lab are three components: a `BoundedQueue` with shutdown semantics, a `ShardedCache` using sharded locking, and classic concurrency patterns implemented using C++20's `latch`, `barrier`, and `semaphore`. These three components are not isolated exercises—the thread pool in Lab 3 will directly reuse `BoundedQueue` as its task queue, and the Capstone project will combine all these components.

After completing this lab, you should have muscle memory for the `mutex` + `condition_variable` combo. You will be able to correctly handle four waiting scenarios: predicate waiting, spurious wakeups, lost wakeups, and shutdown wakeups. You will also understand the performance trade-offs between coarse-grained locks and fine-grained locks.

## Prerequisites

Before starting, ensure you have read the following chapters:

- **ch02-01**: mutex and RAII locks — `std::mutex`, `std::scoped_lock`, `std::unique_lock`, lock guard
- **ch02-02**: Deadlock and lock ordering — deadlock prevention, `std::scoped_lock` for acquiring multiple locks simultaneously
- **ch02-03**: `condition_variable` and waiting semantics — predicate waiting, spurious wakeups, `notify_one` vs `notify_all`
- **ch02-04**: `shared_mutex` and read-write locks — shared locks, read-write separation scenarios
- **ch02-05**: `latch`, `barrier`, and `semaphore` — C++20 synchronization primitives
- **Lab 0**: Implementation and usage of `SafeThread`

This lab directly depends on the `SafeThread` component from Lab 0.

## Environment Setup

Use the same compiler and Catch2 configuration as Lab 0. New requirements:

- **C++20**: Milestone 6 requires `latch`, `barrier`, and `semaphore`. You need GCC 12+ or Clang 15+ with the `-std=c++20` flag enabled.
- **pthread**: Link against `pthread` on Linux.

In `CMakeLists.txt`, change `CMAKE_CXX_STANDARD` from Lab 0's setting to `20` and ensure `pthread` is linked:

```cmake
set(CMAKE_CXX_STANDARD 20)
find_package(Threads REQUIRED)
target_link_libraries(your_target PRIVATE Threads::Threads)
```

## Final Interfaces

### `BoundedQueue` — Bounded blocking queue with shutdown semantics

Member variables:

| Type | Member | Semantics |
|------|--------|-----------|
| `std::deque<T>` | `queue_` | Internal data storage |
| `std::mutex` | `mutex_` | Mutex protecting queue state |
| `std::condition_variable` | `cv_not_full_` | Producer wait condition (queue not full) |
| `std::condition_variable` | `cv_not_empty_` | Consumer wait condition (queue not empty) |
| `size_t` | `capacity_` | Queue capacity upper limit |
| `bool` | `closed_` | Shutdown flag |

Interface:

| Method | Signature | Description | Milestone |
|--------|-----------|-------------|-----------|
| Constructor | `BoundedQueue(size_t capacity)` | Set queue capacity | MS1 |
| push | `bool push(T value)` | Blocking write; returns `false` after close | MS1 |
| pop | `std::optional<T> pop()` | Blocking read; returns `nullopt` if closed and empty | MS1 |
| close | `void close()` | Close queue, wake all waiting threads | MS2 |
| is_closed | `bool is_closed()` | Query closed status | MS2 |
| try_push_for | `bool try_push_for(T value, std::chrono::milliseconds timeout)` | Write with timeout | MS3 |
| try_pop_for | `std::optional<T> try_pop_for(std::chrono::milliseconds timeout)` | Read with timeout | MS3 |
| size | `size_t size()` | Current queue length | MS1 |

### `ShardedCache` — Sharded lock cache (Milestone 5)

Internal definition of `Shard` struct, containing `std::mutex` + `std::shared_mutex`.

Member variables:

| Type | Member | Semantics |
|------|--------|-----------|
| `std::vector<Shard>` | `shards_` | Shard array, default 16 shards |
| `size_t` | `num_shards_` | Used for hashing key to shard |

Interface:

| Method | Signature | Description | Milestone |
|------|------|------|-----------|
| Constructor | `ShardedCache(size_t num_shards = 16)` | Set shard count | MS5 |
| put | `void put(K key, V value)` | Write key-value pair (exclusive lock) | MS5 |
| get | `std::optional<V> get(K key)` | Query value (shared lock) | MS5 |
| erase | `void erase(K key)` | Delete key | MS5 |
| size | `size_t size()` | Total entry count | MS5 |

## Milestone 1: Fixed-Capacity Blocking Queue

### Goal

Implement the `push` and `pop` methods for `BoundedQueue`—fixed capacity, blocking writes, blocking reads. For this milestone, ignore shutdown semantics and timeouts; focus only on the basic `mutex` + `condition_variable` collaboration.

### Why

The blocking queue is the most classic synchronization component in concurrent programming and the most intuitive application scenario for `mutex` and `condition_variable`. It turns the abstract "producer-consumer" model into a concrete, testable data structure. All subsequent milestones add features on top of this foundation—shutdown, timeouts, backpressure—so we need to get it right first.

### Implementation Guide

The core data structure is simple: a `std::deque`, a `std::mutex`, two `std::condition_variable`s (one `cv_not_full` for producers, one `cv_not_empty` for consumers), and a capacity limit.

The logic for `push` is: lock → check if queue is full → if full, `wait` on `cv_not_full` → push element into queue → `notify_one` to wake a consumer. `pop` is the mirror operation: lock → check if queue is empty → if empty, `wait` on `cv_not_empty` → pop element → `notify_one` to wake a producer.

There are several places here where you must use predicated waiting. The wait in `push` cannot be written as `cv_not_full.wait(lock)`, it must be `cv_not_full.wait(lock, [this]{ return size() < capacity_ || closed_; })`. Why? Because `condition_variable` has two annoying characteristics—spurious wakeups (waking up without a `notify`) and lost wakeups (`notify` happening before `wait`). Predicated waiting solves both problems simultaneously: every time it wakes up (whether real or spurious), it rechecks the condition; if the condition isn't met, it continues to wait.

Pitfall warning: If you use `notify_one` instead of `notify_all`, ensure the awakened thread can actually proceed. In our scenario, one `push` operation releases at most one consumer (queue transitions from not empty to empty), so `notify_one` is correct. However, if you change to batch operations somewhere (like `push_all`), you might need `notify_all`.

### Verification

```cpp
// Test basic push/pop
BoundedQueue<int> q(2);
q.push(1);
q.push(2);
REQUIRE(q.size() == 2);
REQUIRE(q.pop() == 1);
REQUIRE(q.pop() == 2);
```

## Milestone 2: Shutdown Semantics

### Goal

Add the `close` method to `BoundedQueue`. After closing, no more `push` operations are allowed (returns `false`), but existing elements in the queue can still be `pop`ped (draining remaining data). `pop` returns `nullopt` when the queue is empty and closed. All currently blocking `push` and `pop` operations must be woken up.

### Why

A blocking queue without shutdown semantics is a ticking time bomb. Consider a typical producer-consumer scenario: the producer thread has finished (file read complete, data generation done), but the consumer is still blocked on `cv_not_empty`—waiting for data that will never arrive. The program hangs indefinitely. `close` is the tool to tell the consumer "no more data, you can go now." It is not just an API method, but a critical link in the lifecycle management of the entire concurrent component—the thread pool shutdown in Lab 3 and the Channel close in Lab 5 follow this same pattern.

### Implementation Guide

The implementation idea for `close` is: lock → set `closed_` to true → `notify_all` to wake all waiting producers and consumers. The key is adding the `closed_` check to the wait loops in `push` and `pop`.

The wait in `push` becomes: `cv_not_full.wait(lock, [this]{ return size() < capacity_ || closed_; })`. After waking, check `closed_` first; if closed, return `false` immediately without pushing data. The wait in `pop` becomes: `cv_not_empty.wait(lock, [this]{ return size() > 0 || closed_; })`. After waking, if the queue is empty AND `closed_`, return `nullopt`; if the queue is not empty (possibly closed but with data), pop the element normally.

There is a subtle detail easy to overlook: `push` returns `false` after checking `closed_`, which means the element was not inserted. However, if you happen to have a `pop` waiting on `cv_not_empty` before `close`, `notify_all` will wake it. It checks `closed_` and returns `nullopt`. This behavior is reasonable—the caller knows the queue is closed and won't try again.

Pitfall warning: `close` must use `notify_all` instead of `notify_one`. Because `close` is a "global event"—all waiting threads need to know the state changed. Using `notify_one` might only wake one thread, leaving others blocked.

### Verification

```cpp
BoundedQueue<int> q(2);
q.close();
REQUIRE(q.is_closed() == true);
REQUIRE(q.push(1) == false); // push rejected
REQUIRE(q.pop() == std::nullopt); // pop returns nullopt
```

## Milestone 3: Timeout Waiting

### Goal

Implement `try_push_for` and `try_pop_for` to support waiting with a timeout. If the queue state doesn't change within the specified time, return failure instead of waiting indefinitely.

### Why

In real systems, waiting indefinitely is dangerous—if the consumer suddenly slows down (e.g., downstream service timeout), the producer might get stuck on `push` with a whole group of threads. Timeout waiting gives the caller a chance to adopt other strategies if the wait takes too long: retry, drop, log an alert, or degrade. The backpressure strategy in Milestone 4 will use timeout waiting directly.

### Implementation Guide

The difference between `try_push_for` and `push` is simply swapping `wait` for `wait_for`. `wait_for` checks the predicate when it times out or is woken up; if the predicate isn't met and it has timed out, it returns `false`.

Pseudocode is as follows:

```cpp
bool try_push_for(T value, std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    // Wait for queue not full or closed, but respect timeout
    if (!cv_not_full_.wait_for(lock, timeout, [this] {
        return size() < capacity_ || closed_;
    })) {
        return false; // Timeout
    }
    if (closed_) return false;
    queue_.push_back(std::move(value));
    cv_not_empty_.notify_one();
    return true;
}
```

Pitfall warning: `wait_for` returning `false` doesn't necessarily mean a timeout occurred—it could also mean it was woken up but the predicate still isn't satisfied. You need to distinguish between "timed out" and "woken by spurious wakeup but condition not met." Actually, when using the predicate version of `wait_for`, the return value indicates "whether the predicate was satisfied"—`true` means satisfied, `false` means not satisfied (could be timeout or other reasons). In your logic, if it returns `false`, it means the operation failed within the timeout period.

### Verification

```cpp
BoundedQueue<int> q(1);
q.push(1); // Queue is now full
// Try push with short timeout, should fail
REQUIRE(q.try_push_for(2, std::chrono::milliseconds(10)) == false);
```

## Milestone 4: Backpressure Strategy

### Goal

Implement two backpressure strategies based on `BoundedQueue`: **Blocking Wait** (already implemented) and **Caller-Runs**. Write a producer-consumer pipeline to compare the behavior of these two strategies under different production/consumption speed ratios.

### Why

Backpressure is a core engineering problem in concurrent systems. When producers are faster than consumers, without a backpressure mechanism, the queue will grow indefinitely (if unbounded) or producers will block (if bounded). Blocking is the simplest backpressure, but it occupies a thread—if all producers block, the system deadlocks. The caller-runs strategy is an alternative: when the queue is full, instead of blocking the producer, let the producer execute the consumer's work itself—reducing queue pressure without wasting threads.

### Implementation Guide

The blocking strategy is already implemented in Milestone 1. The core idea of the caller-runs strategy is: if the queue is full, don't call `push`, but execute the consumer logic directly on the current thread (producer thread).

Pseudocode:

```cpp
void caller_runs_push(BoundedQueue<Task>& q, Task t) {
    if (!q.try_push_for(t, std::chrono::milliseconds(0))) {
        // Queue full, run directly
        t.execute();
    }
}
```

You need to write a simple benchmark to compare the two strategies: fix the production rate (e.g., 1000 tasks per second), make the consumer processing speed adjustable (simulated by `std::this_thread::sleep_for`), and observe how queue length and throughput change under different speed ratios. Don't aim for precise numbers; the focus is on using data to illustrate the applicable scenarios for each strategy.

### Verification

```cpp
// Simple test: verify caller-runs doesn't block
BoundedQueue<int> q(1);
q.push(0); // Fill queue
bool executed = false;
auto task = [&]() { executed = true; };
caller_runs_push(q, task); // Should run immediately
REQUIRE(executed == true);
```

## Milestone 5: Sharded Lock Cache

### Goal

Implement `ShardedCache` using sharded locking to reduce lock contention. Compare the throughput of a single-lock cache and observe the impact of shard count on performance.

### Why

`BoundedQueue` uses a single `mutex` to protect the entire queue—in high-concurrency multi-threaded scenarios, this lock can become a bottleneck. Sharded locking is a common optimization strategy: split data into N shards, each with its own lock. Different shards can be accessed in parallel. A hash function determines which shard a key belongs to, and only that shard is locked during operation. This way, operations on different keys no longer compete for the same lock. ch02-04 discussed read-write separation with `std::shared_mutex`; here we can go further by using `std::shared_mutex` to implement read-write sharding—read operations use shared locks, write operations use exclusive locks.

### Implementation Guide

The core data structure of `ShardedCache` is `std::vector<Shard>`, where each `Shard` contains a `std::unordered_map` and a `std::shared_mutex`. `put` and `erase` operations first calculate the hash of the key, then modulo the shard count to get the target shard, then lock that shard for operation.

Pseudocode for `put`:

```cpp
void put(K key, V value) {
    size_t shard_index = std::hash<K>{}(key) % num_shards_;
    auto& shard = shards_[shard_index];
    std::unique_lock lock(shard.mutex); // Exclusive lock
    shard.map[key] = value;
}
```

Pseudocode for `get`:

```cpp
std::optional<V> get(K key) {
    size_t shard_index = std::hash<K>{}(key) % num_shards_;
    auto& shard = shards_[shard_index];
    std::shared_lock lock(shard.mutex); // Shared lock
    auto it = shard.map.find(key);
    if (it != shard.map.end()) return it->second;
    return std::nullopt;
}
```

The shard count is usually chosen as a power of two (16, 32, 64) for efficient modulo via bit operations. Too few (e.g., 1) degenerates to a single lock; too many (e.g., 1024) wastes memory. 16 is a good starting point.

### Verification

```cpp
ShardedCache<std::string, int> cache(16);
cache.put("key1", 100);
REQUIRE(cache.get("key1") == 100);
cache.erase("key1");
REQUIRE(cache.get("key1") == std::nullopt);
```

## Milestone 6: C++20 Synchronization Primitives Practice

### Goal

Use `latch`, `barrier`, and `semaphore` to implement three classic concurrency patterns: fork-join, phased parallel processing, and resource pooling.

### Why

ch02-05 introduced the APIs for these three C++20 synchronization primitives, but using them in real scenarios cements understanding better than just reading the API. Each primitive solves a specific class of synchronization problems—`latch` solves "wait for a set of tasks to complete," `barrier` solves "multi-phase synchronization," and `semaphore` solves "limiting concurrent access count." In actual engineering, they are more concise and less error-prone than hand-rolled `mutex` + `condition_variable` combinations.

### Implementation Guide

**Fork-Join Pattern** (`latch`): The main thread dispatches N tasks to a thread pool and uses a latch to wait for all to complete before aggregating results.

```cpp
std::latch done(10);
for (int i = 0; i < 10; ++i) {
    threads.emplace_back([&done, i] {
        do_work(i);
        done.count_down();
    });
}
done.wait(); // Main thread waits
```

**Phased Parallel Processing** (`barrier`): Multi-round map-reduce, where a barrier synchronizes at the end of each round, ensuring the output of the previous phase is the input to the next.

```cpp
std::barrier sync_point(4); // 4 threads
for (int round = 0; round < 5; ++round) {
    map_phase();
    sync_point.arrive_and_wait();
    reduce_phase();
    sync_point.arrive_and_wait();
}
```

**Resource Pool** (`semaphore`): Simulate a database connection pool with a max of 5 connections, competed for by multiple threads.

```cpp
std::counting_semaphore<5> pool(5);
void access_db() {
    pool.acquire();
    // use connection
    pool.release();
}
```

Pitfall warning: The callback function for `barrier` must be `noexcept`. If your callback throws an exception, compilation will fail. `semaphore`'s acquire/release do not need to be on the same thread—a producer can release and a consumer can acquire, unlike `mutex` lock/unlock which must be on the same thread.

### Verification

```cpp
// Latch test
std::latch work_done(3);
std::atomic<int> counter{0};
auto job = [&] { counter++; work_done.count_down(); };
std::thread t1(job);
std::thread t2(job);
std::thread t3(job);
work_done.wait();
REQUIRE(counter == 3);
```

## Self-Check List

- [ ] Milestone 1: `push` and `pop` use predicated waiting, no spurious wakeups or lost wakeups
- [ ] Milestone 2: Cannot `push` after `close`, existing data can be `pop`ed, blocking threads are woken
- [ ] Milestone 3: `try_push_for` and `try_pop_for` return correctly after timeout
- [ ] Milestone 4: Behavior of both backpressure strategies matches expectations, with simple performance comparison data
- [ ] Milestone 5: Sharded cache data is correct under multi-threaded stress test, TSan reports no data race
- [ ] Milestone 6: Usage scenarios for `latch`, `barrier`, and `semaphore` are correct, tests pass
- [ ] All tests pass with no data race reports under TSan
- [ ] Can explain when to use `notify_one` vs `notify_all`
- [ ] Can explain why `close` must use `notify_all`
- [ ] Can explain the performance benefits and costs of sharded locking vs single locking (extra memory, hash calculation overhead)
- [ ] Can verbally describe how `BoundedQueue` will be reused in the Lab 3 thread pool
