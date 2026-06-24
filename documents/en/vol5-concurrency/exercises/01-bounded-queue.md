---
title: 'Lab 1: Bounded Queue, Concurrent Cache and Sync Primitives'
description: Implement a fixed-capacity blocking queue, shutdown semantics, timeouts,
  and sharded locking cache; train with mutex, condition variable, and C++20 synchronization
  primitives.
chapter: 10
order: 1
tags:
- host
- cpp-modern
- mutex
- intermediate
difficulty: intermediate
platform: host
reading_time_minutes: 20
cpp_standard:
- 17
- 20
prerequisites:
- '卷五 ch02: 互斥量、条件变量与同步原语'
- 'Lab 0: Thread Lifecycle Lab'
related:
- mutex 与 RAII 守卫
- condition_variable
- latch/barrier/semaphore
translation:
  source: documents/vol5-concurrency/exercises/01-bounded-queue.md
  source_hash: f0432a2c8a3b5959638580be863a5fc5d48ba9f04af3af4ea8cc658ee33ad9e9
  translated_at: '2026-06-24T01:09:32.446803+00:00'
  engine: anthropic
  token_count: 3110
---
# Lab 1: Bounded Queue, Concurrent Cache, and Sync Primitives

> The runnable project for this Lab is located at [`code/volumn_codes/vol5-labs/templates/lab1_bounded_queue/`](../../../../code/volumn_codes/vol5-labs/templates/lab1_bounded_queue/). Estimated hands-on time is **8–12 hours** (note: `reading_time_minutes` refers to reading time only, not coding time).

## Objectives

Lab 0 got the multithreading skeleton working—creating threads, RAII wrappers, and passing parameters safely. However, those threads were all "doing their own thing," with the main thread simply waiting for them to finish. Real concurrent systems don't work like that: threads must collaborate. Producers push data into queues, consumers take it, queues apply backpressure when full, and systems exit gracefully when shut down.

The core deliverables for this Lab are three components:

1. **`BoundedBlockingQueue<T>`**—A fixed-capacity blocking queue with shutdown semantics (evolving through MS1-4). **The ThreadPool in Lab 3 will directly reuse this as its task queue**, so we need to nail the interface design now.
2. **`ConcurrentCache<K, V>`**—A sharded lock concurrent cache (MS5), to practice the trade-offs between "coarse-grained locking vs fine-grained locking."
3. **C++20 Sync Primitive Practice** (MS6)—Using `std::latch`, `std::barrier`, and `std::counting_semaphore` to implement three classic concurrency patterns.

Upon completion, you should have muscle memory for the mutex + condition_variable combo. You will be able to correctly handle four waiting scenarios: **predicate waiting, spurious wakeups, lost wakeups, and shutdown wakeups**, and understand the performance trade-offs of lock granularity.

## Prerequisites

- **ch02-01** Mutex and RAII guards — `std::mutex`, `lock_guard`, `unique_lock`
- **ch02-03** condition_variable — Predicate waiting, spurious wakeups, `notify_one` vs `notify_all`
- **ch02-05** latch / barrier / semaphore — C++20 synchronization primitives
- **Lab 0** — `JoiningThread` (used in this Lab's tests and examples)

## Project Scaffold (Get this running first)

Each Lab has two versions under `vol5-labs/`: **`templates/lab1_bounded_queue/`** is the empty implementation skeleton (copy this one to work on), and **`examples/lab1_bounded_queue/`** is the reference implementation (use this to compare if you get stuck, but don't copy it upfront). Both are standalone projects. You will be working on the `templates` version:

```text
templates/lab1_bounded_queue/
├── CMakeLists.txt       # standalone: FetchContent Catch2 + INTERFACE 库 + test
├── include/lab1/        ← 你在这里补全实现
│   ├── bounded_blocking_queue.h   #   MS1-4
│   ├── concurrent_cache.h         #   MS5
│   └── sync_practice.h            #   MS6
└── test/                # 教程提供的测试（不用改）
    └── test_milestone1.cpp … test_milestone6.cpp
```

**Note that lab1 uses C++20** (not C++17 like lab0), because MS6 requires `std::latch/barrier/counting_semaphore`.

```bash
cd code/volumn_codes/vol5-labs/templates/lab1_bounded_queue
cmake -B build -DCMAKE_BUILD_TYPE=Debug   # Debug 默认开 ThreadSanitizer
cmake --build build
```

**Expected:** The build stops at the linking stage, reporting `undefined reference to lab1::BoundedBlockingQueue<...>::push(...)`. This is intentional; the files in `include/lab1/*.h` contain declarations only. Complete the implementation in milestone order to turn the corresponding tests from red to green.

## Final Interface

Before starting, let's clarify the target shape (identical to the headers in `include/lab1/`).

### `BoundedBlockingQueue<T>` — Evolution across MS1-4 (Interface remains constant, internals are filled in step-by-step)

| Method | Signature | Milestone |
|------|------|-----------|
| Constructor | `explicit BoundedBlockingQueue(std::size_t capacity)` | MS1 |
| Blocking push | `void push(T value)` — waits if full; throws `std::runtime_error` after close | MS1/MS2 |
| Blocking pop | `std::optional<T> pop()` — waits if empty; returns `nullopt` if closed and empty | MS1/MS2 |
| Close | `void close()` — wakes all blocking threads | MS2 |
| Check closed | `bool is_closed() const noexcept` | MS2 |
| Timed push | `bool try_push_for(T, std::chrono::nanoseconds)` — true on success; false on timeout or close | MS3 |
| Timed pop | `std::optional<T> try_pop_for(std::chrono::nanoseconds)` | MS3 |
| Approximate size | `std::size_t size() const noexcept` | MS4 |

### `ConcurrentCache<K, V, Hash>` — MS5 (Sharded Locking)

| Method | Signature |
|------|------|
| Constructor | `explicit ConcurrentCache(std::size_t shard_count = 16)` |
| Get | `std::optional<V> get(const K&) const` |
| Put | `void put(K key, V value)` |
| Erase | `bool erase(const K&)` |
| Size | `std::size_t size() const noexcept` |

### `sync_practice` — MS6 (Three free functions, each using a C++20 primitive)

| Function | Primitive used | Why this one? |
|------|----------|-----------|
| `fork_join_sum(n, task)` | `std::latch` | One-time "wait for N tasks to finish" (countdown to 0) |
| `two_phase_sum(n, val)` | `std::barrier` | Multi-phase, synchronization between phases (reusable) |
| `measure_max_concurrency(n, max)` | `std::counting_semaphore` | Counting semaphore for "allow N concurrent" operations |

Next, we break it down by milestone.

## Milestone 1: Blocking push / pop

### Goal

Implement `push` (blocks waiting for space when the queue is full) and `pop` (blocks waiting for data when the queue is empty). Let's get "passing data between threads via a queue" working first; closing and timeouts come later.

### Why start here

This is the most basic form of the mutex + condition_variable combo. All subsequent milestones (waking on close, timed waits) add branches to this structure, so we need to get the skeleton right here.

### Implementation Guide

The core is a fixed-capacity circular buffer (or just `std::queue<T>`) + one `mutex` + two `condition_variable`s (`not_full_` for producers, `not_empty_` for consumers). Two design points:

**First, waiting must use a predicate; do not use bare `wait()`.** The producer needs to "wait until there is space":

```cpp
std::unique_lock lock(m_);
not_full_.wait(lock, [this] { return queue_.size() < capacity_; });  // ← 谓词
queue_.push(std::move(value));
not_empty_.notify_one();
```

That lambda predicate is critical. If we write `not_full_.wait(lock)` (without a predicate), we fall victim to **spurious wakeups** and **lost wakeups**: the OS allows `wait` to return for no apparent reason (spurious wakeup), or a notify might occur before we enter wait (lost wakeup). In both cases, we would proceed when "there is actually no space," overflowing the queue. The predicate wait **re-checks the condition** upon return, plugging both of these holes.

**Second, `notify_one` or `notify_all`?** For the MS1 single-producer, single-consumer scenario, `notify_one` is sufficient (it only wakes one waiter). However, for MS2's `close`, we must use `notify_all` (to wake all blocked consumers so they can exit). We use `notify_one` for now and will change it in MS2.

> **Pitfall Warning**: Never `notify` while holding the lock — not because it's wrong, but because "unlock then notify" avoids the unnecessary context switch of "the woken thread immediately fails to contend for the lock and goes back to sleep." However, the standard pattern for predicate wait (still holding lock when `wait` returns, releasing lock when the function returns and the lock destructs) already implies the correct order. Don't gild the lily by manually unlocking before notifying and then re-locking.

### Verification

> **Don't be fooled by the tests**: `test_milestone1` checks "can push/pop, FIFO, blocking behavior, and multi-producer integrity without loss or duplication" — **it checks behavior, not whether you used a predicate wait**. You could completely get away with using a bare `wait()` (no predicate) and pass the tests (by luck, not triggering spurious wakeups). But that is a ticking time bomb: it will inevitably crash under high concurrency or specific scheduling. **The real acceptance criteria: `push`/`pop` waiting must use predicate wait (`cv.wait(lock, predicate)`), with no bare `wait()`.** Run the MS4 stress tests with TSan; a bare wait will eventually expose itself via a data race or out-of-bounds access.

[`test/test_milestone1.cpp`](../../../../code/volumn_codes/vol5-labs/templates/lab1_bounded_queue/test/test_milestone1.cpp) covers four scenarios: single push/pop, FIFO, pop blocks until push, and concurrent multi-producer without loss or duplication.

## Milestone 2: close Semantics

### Goal

Implement `close()`: wake up all currently blocked push/pop operations; after close, push throws an exception, and pop returns `nullopt` after remaining elements are consumed.

### Why

The MS1 queue has no way to "end" — the consumer `while (auto v = q.pop())` would wait forever. Real-world producer-consumer systems must have a "production finished" signal for the consumer to exit. `close()` is this signal: it causes `pop` to return `nullopt` once the queue is exhausted, allowing the consumer loop to terminate naturally.

### Implementation Guide

Inside `close`, we need to: acquire the lock, set `closed_ = true`, **`notify_all()` on both condition variables** (to wake all blocked producers and consumers). Then, the predicates for both push and pop must include the `closed_` condition:

```cpp
// push: 既等"有空位"，也要在 close 时立刻失败（抛）
not_full_.wait(lock, [this] { return queue_.size() < capacity_ || closed_; });
if (closed_) throw std::runtime_error("push on closed queue");
// ...

// pop: 既等"有数据"，close 后队列空了也要立刻返回 nullopt
not_empty_.wait(lock, [this] { return !queue_.empty() || closed_; });
if (queue_.empty()) return std::nullopt;   // 一定是 closed_ && 空
// ...
```

**Crucial:** `close` **must** call `notify_all` (not `notify_one`). Since multiple consumers might be blocked in `pop`, we need to wake them all up—`notify_one` only wakes one, leaving the rest stuck forever (deadlock).

> **Gotcha Warning**: The post-`close` `pop` semantics are "drain remaining → nullopt", not "immediately nullopt". Consumers must still be able to retrieve elements enqueued before the close. Therefore, the `pop` predicate is `!queue_.empty() || closed_`—prioritize satisfying data retrieval first, and only check `closed_` when empty.

### Verification

> **Don't be fooled by the test**: `test_milestone2` checks that push throws after close, pop drains and returns nullopt, and close wakes consumers to exit. However, it **does not verify that close actually wakes *all* blocked waiters**—if you slip up and write `notify_one`, the test still passes because it only uses one consumer. **The real acceptance criteria: use `notify_all()` for both condition variables in `close()`.** Multi-consumer scenarios in the MS4 stress tests will expose the deadlock caused by `notify_one`.

## Milestone 3: Timeout try_push_for / try_pop_for

### Goal

Add timeout versions for push/pop: give up if the wait times out, return failure (push returns `false`, pop returns `nullopt`), without throwing or blocking indefinitely.

### Why

Blocking versions could wait forever—a breeding ground for deadlocks (e.g., producer and consumer waiting on each other). Timeout versions provide an "exit strategy" to give up and continue, used in real-world systems for health checks, graceful degradation, and avoiding permanent blocking.

### Implementation Guide

Use `condition_variable::wait_for(lock, timeout, predicate)`. Just like in MS1, a **predicate is mandatory**—`wait_for` is also subject to spurious wakeups; without a predicate, you might return incorrectly before the timeout expires. The return value indicates whether it returned "because the predicate was satisfied" (`true`) or "due to timeout" (`false`):

```cpp
bool try_push_for(T value, std::chrono::nanoseconds timeout) {
    std::unique_lock lock(m_);
    bool ok = not_full_.wait_for(lock, timeout,
        [this] { return queue_.size() < capacity_ || closed_; });
    if (!ok || closed_) return false;   // 超时或已关闭
    queue_.push(std::move(value));
    not_empty_.notify_one();
    return true;
}
```

Note that `wait_for` returning `false` does not mean closed—it only means a timeout. Therefore, we must still check `closed_` separately afterwards.

### Verification

> **Don't be fooled by the tests**: `test_milestone3` checks the timeout return value and timing, but **it does not verify whether your `wait_for` includes a predicate**. A bare `wait_for(lock, timeout)` (without a predicate) might return early upon spurious wakeups before the time is up, but the test's time assertion has a 10ms tolerance, so it might slip through. **The real acceptance criteria: `wait_for` takes a predicate, and after it returns, we use the return value + `closed_` for a dual check.**

## Milestone 4: Backpressure and Concurrency Stress

### Goal

Run a real MPMC (Multi-Producer Multi-Consumer) stress test with a small-capacity queue to verify that capacity limits are effective, no data is lost or duplicated, and TSan is clean.

### Why

The first three milestones were about "point correctness," whereas MS4 is about "system correctness"—when multiple producers and consumers are truly concurrent, will your locks, condition variables, and predicates reveal flaws under pressure? This is the hard threshold for a BoundedBlockingQueue to be "usable."

### Implementation Guide

You basically don't need to add new code—the implementation from MS1-3 should be sufficient. The focus here is **understanding capacity limits**: `capacity_` is a hard upper limit. Producers **must block** when the queue is full (backpressure), rather than expanding indefinitely. If your `push` doesn't block and you change it to dynamic resizing, it's no longer a "bounded" queue—the test's `size()` assertions and the backpressure semantics of MS4 will fail.

Before running the tests, clarify the exit sequence for multiple consumers: all producers finish pushing → main thread calls `close()` → consumers' `while (auto v = q.pop())` loops exit after consuming remaining items and receiving `nullopt`. This chain relies on the `notify_all` + `nullopt` semantics from MS2, which couldn't be tested with the single consumer in MS1.

### Verification

> **Don't be fooled by the tests**: The MPMC stress test in `test_milestone4` checks "no loss/duplication + size tracking". If you secretly change the queue to unbounded (where `push` never blocks), the "no loss/duplication" property still holds and the test passes—but you lose backpressure capability, and Lab 3's ThreadPool will cause an OOM (Out Of Memory) when using it. **The real acceptance criteria: queue size is always ≤ capacity (`push` actually blocks when full), and multiple consumers exit via MS2's close mechanism.** This stress test must be race-free under TSan.

## Milestone 5: ConcurrentCache (Sharded Locking)

### Goal

Implement a sharded locking concurrent cache: keys are hashed into `shard_count` shards, where each shard holds its own independent mutex, allowing different shards to operate in parallel.

### Why

This is a classic trade-off between "coarse-grained locking" and "fine-grained locking." The naive approach uses a single lock for the entire cache—all threads access serially, and throughput is choked by lock contention. After sharding, different keys land in different shards, allowing reads and writes to truly parallelize, increasing throughput linearly with the shard count (until hitting other bottlenecks).

### Implementation Guide

Internally use `std::vector<Shard>`, where each Shard holds its own `mutex` + `unordered_map`. To locate a shard: `shard_idx = hash(key) % shard_count` (if `shard_count` is a power of two, you can use `& (shard_count - 1)` bitwise operation for speed). A `shard_count` of 16 is recommended (sufficiently dispersed, manageable overhead).

```cpp
template <typename K, typename V, typename Hash = std::hash<K>>
class ConcurrentCache {
    struct Shard {
        mutable std::mutex m;
        std::unordered_map<K, V> map;
    };
    std::vector<Shard> shards_;
    Hash hash_{};
    // get/put/erase: hash(key) % shards_.size() 定位 shard, 只锁那一个
};
```

> **About `mutable`**: `get` is a `const` method (it logically does not modify the cache), but it needs to acquire a lock (locking modifies the mutex state). Therefore, the shard's mutex is `mutable`—it can be modified even within `const` methods. This is the standard pattern for combining `const` with concurrency, not a hack.

### Verification

> **Don't be fooled by the tests**: `test_milestone5` checks that concurrent `put` operations do not lose data, `get` returns correct values, and `size` is accurate. **However, it does not verify if you actually implemented sharding**—you could pass all tests with a "single global lock" (the results would be equally correct). The difference lies only in throughput: a single lock is significantly slower under high concurrency. **The real acceptance criteria: the internals must use multiple shards, each with an independent mutex, so that accesses to different keys lock different shards.** You can write a micro-benchmark yourself (single lock vs. sharding) to compare throughput and experience the difference—this is the whole point of MS5.

## Milestone 6: C++20 Synchronization Primitives Practice

### Objective

Implement a classic concurrency pattern using each of the following: `std::latch`, `std::barrier`, and `std::counting_semaphore` (specifically `fork_join_sum`, `two_phase_sum`, and `measure_max_concurrency`).

### Why

Chapters 02-05 covered the concepts of these three primitives, but there is a big gap between "knowing" and "choosing the right one". The core of this milestone is not writing a lot of code, but **judging "which primitive fits this scenario"**—each function corresponds to a typical use case, so think clearly about *why* it is the correct choice while implementing.

### Implementation Guide

**`fork_join_sum` (latch)**: Dispatch N tasks to threads; the main thread must wait for all to complete. Initialize `std::latch` with N, call `count_down()` when each task finishes, and have the main thread call `wait()`. Why a latch and not a barrier? Because this is a one-time "wait for N completions" (countdown to 0), whereas a barrier is for reusable phase synchronization.

**`two_phase_sum` (barrier)**: Multiple workers perform phase 1 independently (writing their own contributions), synchronize at a barrier (no one enters phase 2 until all finish phase 1), and then aggregate. `std::barrier` allows specifying a completion function (executed by the last arriving thread), which fits "aggregation between phases". Why not a latch? Because there might be multiple rounds (barrier is reusable), and we need to perform actions at the phase boundary.

**`measure_max_concurrency` (semaphore)**: N threads want to enter a critical section, but at most `max_concurrent` are allowed. Initialize `std::counting_semaphore<max>` with `max`; each thread calls `acquire()` to enter and `release()` to exit, using an atomic variable to record the peak count inside. Why a semaphore? Because this is the classic scenario of "allowing N concurrent access", which is wrong for both latch and barrier.

> **Pitfall Warning**: The template parameter of `counting_semaphore` is the maximum value, while the constructor parameter is the initial value. `std::counting_semaphore<4>` + constructor `(4)` means an initial count of 4 permits and a maximum of 4. In `measure_max_concurrency`, observing the peak value requires `compare_exchange` on the `atomic`; don't use a plain `int++` (multiple threads writing to the same variable is a data race).

### Verification

> **Don't be fooled by the tests**: `test_milestone6` checks the return values of the three functions (the sum of `fork_join_sum`, the product of `two_phase_sum`, the limit of `max_concurrency`). **But it doesn't verify if you actually used the corresponding primitives**—you could completely simulate the same results with mutexes (e.g., `fork_join` using a mutex + atomic counter to mimic a latch). **The real acceptance criteria: the three functions must genuinely use `std::latch` / `std::barrier` / `std::counting_semaphore`**, not hand-rolled equivalents. Appreciate that "the standard library gives you the right tools; don't reinvent the wheel".

## Self-Checklist

Confirm each item before submission:

- [ ] MS1 tests pass — push/pop, FIFO, blocking behavior, multi-producer correctness (no loss or duplication)
- [ ] MS2 tests pass — push throws after close, pop returns `nullopt` after consuming remaining items, close wakes consumers
- [ ] MS3 tests pass — timeout return values and timing are correct
- [ ] MS4 tests pass — MPMC stress test (no loss or duplication), `size` tracks capacity
- [ ] MS5 tests pass — concurrent `put` does not lose data, `get` is correct, `size` is accurate
- [ ] MS6 tests pass — results of the three synchronization primitive functions are correct
- [ ] **MS1 Real Acceptance**: `push`/`pop` waiting uses predicate wait (`cv.wait(lock, predicate)`), no bare `wait()`
- [ ] **MS2 Real Acceptance**: `close()` calls `notify_all()` on both condition variables (not `notify_one`)
- [ ] **MS4 Real Acceptance**: Queue size is always ≤ capacity (backpressure works), relying on `close` to signal multiple consumers to exit
- [ ] **MS5 Real Acceptance**: Internals use multiple shards, each holding an independent mutex (not a single global lock)
- [ ] **MS6 Real Acceptance**: The three functions genuinely use `std::latch` / `std::barrier` / `std::counting_semaphore`
- [ ] **All tests pass under TSan with no data race reports** (run directly on Debug build)
- [ ] Can explain why predicate wait prevents both spurious wakeups and lost wakeups
- [ ] Can explain the semantics of "consume remaining then return nullopt" for `pop` after `close` (as opposed to immediately returning `nullopt`)
- [ ] Can explain the throughput advantage of sharded locks vs. a single lock, and the trade-offs in choosing `shard_count`

## Extensions (Bonus)

- Add `try_push`/`try_pop` to `BoundedBlockingQueue` (non-blocking versions, returning success/failure immediately)
- Use `std::shared_mutex` to create a read-write lock version of `ConcurrentCache` (better than sharded mutexes for read-heavy workloads), and compare throughput
- Implement "strict == max" verification for `measure_max_concurrency` (requires enough callers + synchronized start)

## References

- [`std::condition_variable` — cppreference](https://en.cppreference.com/w/cpp/thread/condition_variable)
- [`std::condition_variable::wait` predicate overload — cppreference](https://en.cppreference.com/w/cpp/thread/condition_variable/wait)
- [`std::latch` / `std::barrier` / `std::counting_semaphore — cppreference`](https://en.cppreference.com/w/cpp/thread)
- [ThreadSanitizer — Clang documentation](https://clang.llvm.org/docs/ThreadSanitizer.html)
