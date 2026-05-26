---
title: 'Lab 2: Atomic Metrics and SPSC Ring Buffer'
description: Master `atomic`, `memory_order`, false sharing, and benchmarking methodologies
  through atomic counters and single-producer single-consumer ring buffers.
chapter: 10
order: 2
difficulty: intermediate
tags:
- host
- cpp-modern
- atomic
- memory_order
- intermediate
cpp_standard:
- 17
- 20
reading_time_minutes: 16
prerequisites:
- '卷五 ch03: 原子操作与内存模型'
- 'Lab 0: Thread Lifecycle Lab'
translation:
  source: documents/vol5-concurrency/exercises/02-atomic-spsc.md
  source_hash: e9637902896206d664a2352ebed7453d17944c77adf70dd54cd65b5e523eb664
  translated_at: '2026-05-26T11:47:59.471636+00:00'
  engine: anthropic
  token_count: 3232
---
# Lab 2: Atomic Metrics and SPSC Ring Buffer

## Objectives

In Lab 1, we relied entirely on mutex and condition_variable—locking, waiting, and waking up. While the logic is clear, the overhead is significant. Every lock/unlock operation involves a system call into kernel space (futex). In extremely high-frequency scenarios (such as millions of messages per second), this overhead becomes unacceptable. In this lab, we enter a different world: using atomic operations and memory order to implement lock-free data exchange.

We will first implement a set of atomic metric components—counters, max value trackers, and stop flags—which will be used repeatedly for performance monitoring in subsequent labs. Then, we will implement a fixed-capacity SPSC (Single-Producer Single-Consumer) ring buffer, using acquire-release semantics to guarantee data visibility and cache line padding to eliminate false sharing. Finally, we will run a benchmark comparison against the mutex-based queue from Lab 1, using data to illustrate the applicable scenarios for each approach.

## Prerequisites

Before starting, make sure you have read the following chapters:

- **ch03-01**: atomic operations — `atomic<T>`, `load`/`store`/`fetch_add`, is_lock_free
- **ch03-02**: Memory order explained — semantics and overhead of relaxed, acquire-release, and seq_cst
- **ch03-03**: memory_order_fence and barriers — use cases for explicit fences
- **ch03-04**: atomic wait and reference semantics — `wait`/`notify_one`/`notify_all`
- **ch03-05**: Atomic operation patterns — common atomic usage patterns

This lab does not depend on Lab 1 components, but we recommend completing Lab 1 first to understand the baseline comparison for the mutex approach.

## Environment Setup

Same as Lab 1. Additionally, for the performance testing section, we recommend running on Linux (requires `perf stat` support). WSL2 users can use perf directly.

Disabling CPU frequency scaling can improve benchmark stability (requires sudo):

```bash
sudo cpupower frequency-set -g performance
```

## Final Interfaces

### `AtomicCounter` — Atomic Counter (Milestone 1)

Member variable: internally holds an `std::atomic<std::size_t>`.

| Method | Signature | Description | Milestone |
|------|------|------|-----------|
| Constructor | `AtomicCounter(size_t initial = 0)` | Sets the initial value | MS1 |
| increment | `void increment()` | Atomically increments (`relaxed`) | MS1 |
| decrement | `void decrement()` | Atomically decrements | MS1 |
| get | `size_t get() const` | Reads the current value | MS1 |
| exchange | `size_t exchange(size_t new_val)` | Atomically replaces and returns the old value | MS1 |

### `AtomicMaxTracker` — Atomic Max Tracker (Milestone 1)

Member variable: internally holds an `std::atomic<std::size_t>`.

| Method | Signature | Description | Milestone |
|------|------|------|-----------|
| Constructor | `AtomicMaxTracker(size_t initial = 0)` | Sets the initial max value | MS1 |
| update | `void update(size_t value)` | Updates max value via CAS loop | MS1 |
| get | `size_t get() const` | Reads the current max value | MS1 |

### `StopFlag` — Stop Flag (Milestone 1)

Member variable: internally holds an `std::atomic<bool>`.

| Method | Signature | Description | Milestone |
|------|------|------|-----------|
| request_stop | `void request_stop()` | Sets the stop flag (`release`) | MS1 |
| is_stop_requested | `bool is_stop_requested() const` | Checks if stop is requested (`acquire`) | MS1 |

### `SpscRingBuffer<T, N>` — SPSC Ring Buffer (Milestone 2–4)

Member variables:

| Type | Member | Semantics |
|------|------|------|
| `std::array<T, N>` | `buffer_` | Fixed-capacity storage (determined at compile time) |
| `alignas(64) atomic<size_t>` | `head_` | Consumer read position (cache line padding added in MS4) |
| `alignas(64) atomic<size_t>` | `tail_` | Producer write position (cache line padding added in MS4) |

Interface:

| Method | Signature | Description | Milestone |
|------|------|------|-----------|
| Constructor | `SpscRingBuffer()` | Initializes head/tail to 0 | MS2 |
| try_push | `bool try_push(T item)` | Non-blocking write, returns false if full | MS2 |
| try_pop | `std::optional<T> try_pop()` | Non-blocking read, returns nullopt if empty | MS2 |
| empty | `bool empty() const` | Whether the buffer is empty | MS2 |
| full | `bool full() const` | Whether the buffer is full | MS2 |

## Milestone 1: Atomic Metric Components

### Objectives

Implement three components: `AtomicCounter`, `AtomicMaxTracker`, and `StopFlag`. The key is choosing the appropriate memory order for each operation—not all operations need the default `seq_cst`.

### Why

These three components are infrastructure tools for all subsequent labs. The thread pool needs `AtomicCounter` to count completed tasks, the echo server needs `AtomicMaxTracker` to track the maximum number of concurrent connections, and all labs need `StopFlag` to implement graceful shutdown. By implementing them correctly now, we avoid repeatedly struggling with memory order choices later.

### Implementation Guide

For `AtomicCounter`, using `fetch_add(1, std::memory_order_relaxed)` for `increment` is sufficient—we only care about the accuracy of the count and do not need to establish a synchronization relationship with other variables. The same logic applies to using `load(std::memory_order_relaxed)` for `get`. This is because a relaxed atomic guarantees atomicity (no torn reads or writes) but does not guarantee ordering with respect to other operations—for a pure counter, this is exactly what we want.

`AtomicMaxTracker` is slightly more complex. `update` requires a CAS loop: read the current max value, attempt to replace it if the new value is larger, and retry if another thread beats us to it. Using `compare_exchange_weak` is fine here—the CAS loop inherently handles failure retries, so the spurious failures of the weak version are not an issue.

```cpp
void update(size_t value) {
    size_t current = max_.load(relaxed);
    while (value > current) {
        if (max_.compare_exchange_weak(current, value,
                relaxed, relaxed)) {
            break;
        }
    }
}
```

`StopFlag` is the simplest—a `atomic<bool>`, where `request_stop` uses `store(true, release)` and `is_stop_requested` uses `load(acquire)`. The acquire-release pair is meaningful here: all write operations before `request_stop` (such as cleaning up resources or setting state) become visible to the thread that calls `is_stop_requested` and sees `true`.

### Validation

```cpp
TEST_CASE("Milestone 1: AtomicCounter under contention",
          "[lab2][milestone1]")
{
    AtomicCounter counter;
    const int kThreads = 8;
    const int kIncrements = 100000;

    std::vector<JoiningThread> threads;
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < kIncrements; ++j) {
                counter.increment();
            }
        });
    }

    REQUIRE(counter.get() ==
            kThreads * kIncrements);
}

TEST_CASE("Milestone 1: AtomicMaxTracker tracks global max",
          "[lab2][milestone1]")
{
    AtomicMaxTracker tracker(0);

    std::vector<JoiningThread> threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([&tracker, i]() {
            tracker.update(i * 10 + 5);
        });
    }

    // 最大值应该是 75 (7*10+5)
    REQUIRE(tracker.get() == 75);
}

TEST_CASE("Milestone 1: StopFlag signals stop",
          "[lab2][milestone1]")
{
    StopFlag flag;
    REQUIRE_FALSE(flag.is_stop_requested());

    flag.request_stop();
    REQUIRE(flag.is_stop_requested());
}
```

## Milestone 2: SPSC Ring Buffer Basics

### Objectives

Implement `try_push` and `try_pop` for `SpscRingBuffer<T, N>`. Fixed capacity N, determined at compile time, with no blocking support—returns false if full, and nullopt if empty. In this milestone, we will not worry about memory order; we will use the default `seq_cst` for everything.

### Why

SPSC is the simplest lock-free data structure—there is only one producer and one consumer, so we do not need to worry about multiple threads modifying the same position simultaneously. The producer only writes to `tail_`, and the consumer only writes to `head_`. They determine the buffer state by reading each other's index. This "each thread only writes to its own spot" design is the core pattern of lock-free programming—eliminating write contention.

### Implementation Guide

The core of a ring buffer is two indexes: `head_` (consumer read position) and `tail_` (producer write position). `try_push` checks `tail_ - head_ < N` (not full), writes to `buffer_[tail_ % N]`, and finally increments `tail_`. `try_pop` checks `head_ < tail_` (not empty), reads from `buffer_[head_ % N]`, and increments `head_`.

Pseudocode:

```cpp

bool try_push(T item) {
    size_t tail = tail_.load(seq_cst);
    size_t head = head_.load(seq_cst);

    if (tail - head >= N) return false;  // 满了

    buffer_[tail % N] = std::move(item);
    tail_.store(tail + 1, seq_cst);
    return true;
}

optional<T> try_pop() {
    size_t head = head_.load(seq_cst);
    size_t tail = tail_.load(seq_cst);

    if (head >= tail) return nullopt;  // 空了

    T item = std::move(buffer_[head % N]);
    head_.store(head + 1, seq_cst);
    return item;
}

```

Pitfall warning: index overflow. If `head_` and `tail_` continuously increment, they will eventually overflow `size_t`. On a 64-bit system, this is not a practical issue (2^64 operations would take billions of years), but if you change the type to `uint32_t`, you need to be careful—the calculation result of `tail - head` will be incorrect after overflow.

### Validation

```cpp
TEST_CASE("Milestone 2: SPSC transfers sequential integers",
          "[lab2][milestone2]")
{
    SpscRingBuffer<int, 16> buf;
    const int kItems = 100000;

    JoiningThread producer([&]() {
        for (int i = 1; i <= kItems; ++i) {
            while (!buf.try_push(i)) {
                // 自旋等待
            }
        }
    });

    std::vector<int> consumed;
    int expected = 1;
    while (expected <= kItems) {
        auto val = buf.try_pop();
        if (val) {
            REQUIRE(*val == expected);
            ++expected;
        }
    }

    REQUIRE(expected == kItems + 1);
}

TEST_CASE("Milestone 2: full and empty states",
          "[lab2][milestone2]")
{
    SpscRingBuffer<int, 4> buf;

    REQUIRE(buf.empty());
    REQUIRE_FALSE(buf.full());

    REQUIRE(buf.try_push(1));
    REQUIRE(buf.try_push(2));
    REQUIRE(buf.try_push(3));
    REQUIRE(buf.try_push(4));
    REQUIRE(buf.full());

    REQUIRE_FALSE(buf.try_push(5));  // 满了

    REQUIRE(buf.try_pop() == 1);
    REQUIRE_FALSE(buf.full());  // 有空间了
    REQUIRE(buf.try_push(5));   // 现在可以了
}
```

## Milestone 3: acquire-release Optimization

### Objectives

Replace all uses of `seq_cst` memory order from Milestone 2 with the lighter acquire-release semantics. Understand which load/store operations can use `relaxed`, and which must use acquire/release.

### Why

`seq_cst` is the strongest memory order—it guarantees that all threads see a consistent order of operations, but this requires extra synchronization instructions (the `MFENCE` or `LOCK` prefix on x86). In an SPSC scenario, we do not need global consistency—we only need to guarantee that the data written by the producer is visible to the consumer. This is exactly what acquire-release semantics do: all write operations before the producer's `store(release)` become visible after the consumer's `load(acquire)`.

### Implementation Guide

Key analysis: in `try_push`, writing to `buffer_[tail % N]` must complete before `tail_.store(tail + 1, release)`—so that when the consumer sees the new `tail_`, the contents of `buffer_` are already ready. In `try_pop`, reading `buffer_[head % N]` must happen after `head_.store(head + 1, release)`—so that when the producer sees the new `head_`, the contents of `buffer_` have already been fetched and can be safely overwritten.

Specific replacement strategy:

- Reading `head_` in `try_push` can use `relaxed`—the producer does not care about the consumer's exact position, only whether there is space, so a slight delay is acceptable
- Writing `tail_` in `try_push` must use `release`—guarantees that the buffer write completes before the tail update
- Reading `tail_` in `try_pop` can use `relaxed`—same as above
- Writing `head_` in `try_pop` must use `release`—guarantees that the buffer read completes before the head update

Pitfall warning: if you incorrectly change the store of `tail_` to `relaxed`, the consumer might see data that has not been fully written yet. This type of bug is almost impossible to reproduce during development (because x86's strong memory model naturally guarantees store-store ordering), but it will surface on ARM architectures.

### Validation

```cpp
TEST_CASE("Milestone 3: acquire-release SPSC correctness",
          "[lab2][milestone3]")
{
    // 跟 Milestone 2 一样的测试，但跑在 acquire-release 版本上
    SpscRingBuffer<int, 64> buf;
    const int kItems = 500000;

    JoiningThread producer([&]() {
        for (int i = 1; i <= kItems; ++i) {
            while (!buf.try_push(i)) {}
        }
    });

    int expected = 1;
    while (expected <= kItems) {
        auto val = buf.try_pop();
        if (val) {
            REQUIRE(*val == expected);
            ++expected;
        }
    }
}
```

## Milestone 4: Cache Line Padding and False Sharing Elimination

### Objectives

Add cache line padding to `SpscRingBuffer` to ensure that `head_` and `tail_` do not share the same cache line. Compare the performance data before and after padding.

### Why

As covered in ch00-03, false sharing occurs when two atomic variables happen to reside on the same cache line (typically 64 bytes). When one thread modifies variable A, it invalidates the cache line containing the other thread's variable B, even if B was not modified at all. In an SPSC scenario, `head_` and `tail_` are modified at high frequency by different threads—if they are on the same cache line, every modification will cause a cache miss for the other, potentially degrading performance by several times.

### Implementation Guide

The solution is to insert padding between `head_` and `tail_`, forcing them onto different cache lines. C++11 provides the `alignas` specifier:

```cpp

alignas(64) atomic<size_t> head_{0};
// 64 字节对齐，确保 head_ 独占一个 cache line

char padding_[64 - sizeof(atomic<size_t>)];
// 填充剩余空间（如果需要）

alignas(64) atomic<size_t> tail_{0};
// tail_ 也独占一个 cache line

```

A more concise approach is to use `alignas(64)` directly on class member declarations, and the compiler will automatically insert padding. In actual testing, you should observe a throughput improvement after eliminating false sharing—especially on ARM architectures, where the difference will be very pronounced.

Validation for this milestone is primarily a performance comparison. Use Catch2's `BENCHMARK` macro (or manual timing) to measure the time taken for the same number of push/pop operations before and after padding. The exact numbers depend on your hardware, but you should observe at least an order-of-magnitude difference.

### Validation

```cpp
TEST_CASE("Milestone 4: padded SPSC maintains correctness",
          "[lab2][milestone4]")
{
    SpscRingBuffer<int, 64> buf;
    const int kItems = 100000;

    JoiningThread producer([&]() {
        for (int i = 1; i <= kItems; ++i) {
            while (!buf.try_push(i)) {}
        }
    });

    int expected = 1;
    while (expected <= kItems) {
        auto val = buf.try_pop();
        if (val) {
            REQUIRE(*val == expected);
            ++expected;
        }
    }
}

TEST_CASE("Milestone 4: benchmark padded vs unpadded",
          "[lab2][milestone4]")
{
    // 性能对比测试——不需要 REQUIRE，只需观察输出
    const int kItems = 1000000;
    const int kRounds = 10;

    // 测量当前（padded）版本
    auto padded_time = benchmark_spsc<SpscRingBuffer<int, 256>>(
        kItems, kRounds);

    // 你可以额外实现一个 UnpaddedSpscRingBuffer 来对比
    // auto unpadded_time = benchmark_spsc<UnpaddedSpscRingBuffer<int, 256>>(
    //     kItems, kRounds);

    // 报告结果（不做 REQUIRE，因为性能数字因环境而异）
    std::cout << "Padded SPSC: " << padded_time << " us\n";
}
```

## Milestone 5: Benchmark Comparison with Mutex Queue

### Objectives

Use a unified benchmark methodology to compare the throughput of `SpscRingBuffer` (lock-free) and `BoundedBlockingQueue` (mutex) in an SPSC scenario.

### Why

Many people assume that "lock-free" is always faster, but the reality is not that simple. In low-contention scenarios, the overhead of a mutex is actually quite small (on x86, an uncontended futex is just a single atomic instruction); in high-frequency single-threaded scenarios, atomic busy-waiting can consume more CPU than mutex sleep-waiting. Only by letting the data speak can we clarify under exactly what conditions "faster" holds true.

### Implementation Guide

Follow the requirements from the "Concurrent Performance Measurement Methodology" chapter in `.claude/chapter-projects-outline.md`:

1. Warm-up phase—run 5 rounds first without recording data
2. Formal collection—at least 10 rounds, take the median
3. Report format—test environment, parameters, results, conclusions, and boundaries

Pseudocode:

```cpp
auto benchmark = [&](auto& queue, int items) -> double {
    // 热身
    for (int i = 0; i < 3; ++i) {
        run_spsc_benchmark(queue, items);
    }

    // 正式采集
    vector<double> samples;
    for (int i = 0; i < 10; ++i) {
        auto start = steady_clock::now();
        run_spsc_benchmark(queue, items);
        auto elapsed = steady_clock::now() - start;
        samples.push_back(elapsed in microseconds);
    }

    sort(samples);
    return samples[samples.size() / 2];  // 中位数
};
```

Your report should include: CPU model and core count, compiler and optimization level, data scale, median latency, and an explanation of your conclusion's boundaries—"this conclusion only applies to SPSC scenarios and does not hold for MPMC scenarios."

### Validation

Validation for this milestone is not a traditional `REQUIRE`, but rather a sanity check of the performance data. You need to confirm:

- The lock-free version is indeed faster than the mutex version in SPSC scenarios (typically 2-10x faster)
- The trend of performance differences across data scales is reasonable
- You can explain why the mutex version might actually be faster under certain conditions (for example, when contention is extremely low, the mutex overhead is nearly zero)

## Self-Check List

- [ ] `AtomicCounter` uses `relaxed` order, and `StopFlag` uses an acquire-release pair
- [ ] The CAS loop in `AtomicMaxTracker` correctly handles concurrent updates
- [ ] SPSC data transfer has no loss, no duplication, and correct ordering
- [ ] Tests still pass after replacing seq_cst with acquire-release
- [ ] After cache line padding, `head_` and `tail_` are not on the same cache line
- [ ] Benchmarks follow a unified methodology (warm-up, multiple collections, take median)
- [ ] You can explain the performance differences between relaxed, acquire-release, and seq_cst
- [ ] You can explain the principle of false sharing and how padding eliminates it
- [ ] You can describe the conditions under which the lock-free approach outperforms the mutex approach, and when it might not
- [ ] All tests pass under TSan with no data race reports
