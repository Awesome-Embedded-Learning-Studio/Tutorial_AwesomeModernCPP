---
chapter: 10
cpp_standard:
- 17
- 20
description: Master atomics, memory_order, false sharing, and benchmarking methodology
  by building atomic counters and a single-producer single-consumer ring buffer.
difficulty: intermediate
order: 2
prerequisites:
- 'Volume 5 ch03: Atomic Operations and the Memory Model'
- 'Lab 0: Thread Lifecycle'
reading_time_minutes: 14
tags:
- host
- cpp-modern
- atomic
- memory_order
- intermediate
title: 'Lab 2: Atomic Metrics and SPSC Ring Buffer'
translation:
  source: documents/vol5-concurrency/exercises/02-atomic-spsc.md
  source_hash: 157e842782418b6fe57f5817d08c3cae0197efbb56216afea7d9a28a2f36b377
  translated_at: '2026-09-26T09:17:50+00:00'
  engine: anthropic
  token_count: 7300
---
# Lab 2: Atomic Metrics and SPSC Ring Buffer

## Objectives

Lab 1 leaned on mutex and condition_variable the whole way—lock, wait, wake up. The logic is clear, but the cost is not trivial. Every lock/unlock goes through a kernel-mode system call (futex), and at extremely high frequencies (say, millions of messages per second), that cost is unacceptable. In this Lab we step into another world: lock-free data exchange built on atomic operations and memory orders.

First we build a set of atomic metrics components—a counter, a max tracker, and a stop flag—that later Labs will reuse over and over for performance monitoring. Then we implement a fixed-capacity SPSC (Single-Producer Single-Consumer) ring buffer that guarantees data visibility with acquire-release semantics and eliminates false sharing with cache line padding. Finally, we benchmark it against Lab 1's mutex queue and let the data show where each approach fits.

## Prerequisites

Before starting, make sure you have read the following sections:

- **ch03-01** Atomic operations — `atomic<T>`, `load`/`store`/`fetch_add`, is_lock_free
- **ch03-02** A deep dive into memory ordering — the semantics and costs of relaxed, acquire-release, and seq_cst
- **ch03-03** memory_order_fence and barriers — when to use explicit fences
- **ch03-04** atomic wait and reference semantics — `wait`/`notify_one`/`notify_all`
- **ch03-05** Atomic operation patterns — common atomic usage patterns

This Lab doesn't depend on Lab 1's components, but finishing Lab 1 first is recommended so that the benchmark comparison against the mutex approach makes sense.

## Environment Setup

Same as Lab 1. Beyond that, the performance-measurement parts are best run on Linux (you need `perf stat` support). WSL2 users can use perf directly.

Disabling dynamic CPU frequency scaling improves benchmark stability (requires sudo):

```bash
sudo cpupower frequency-set -g performance
```

## Final Interface

### `AtomicCounter` — Atomic Counter (Milestone 1)

Member variables: holds a `std::atomic<std::size_t>` internally.

| Method | Signature | Description | Milestone |
|------|------|------|-----------|
| Constructor | `AtomicCounter(size_t initial = 0)` | Sets the initial value | MS1 |
| increment | `void increment()` | Atomic increment (`relaxed`) | MS1 |
| decrement | `void decrement()` | Atomic decrement | MS1 |
| get | `size_t get() const` | Reads the current value | MS1 |
| exchange | `size_t exchange(size_t new_val)` | Atomic replace, returns the old value | MS1 |

### `AtomicMaxTracker` — Atomic Max Tracker (Milestone 1)

Member variables: holds a `std::atomic<std::size_t>` internally.

| Method | Signature | Description | Milestone |
|------|------|------|-----------|
| Constructor | `AtomicMaxTracker(size_t initial = 0)` | Sets the initial maximum | MS1 |
| update | `void update(size_t value)` | Updates the maximum via a CAS loop | MS1 |
| get | `size_t get() const` | Reads the current maximum | MS1 |

### `StopFlag` — Stop Flag (Milestone 1)

Member variables: holds a `std::atomic<bool>` internally.

| Method | Signature | Description | Milestone |
|------|------|------|-----------|
| request_stop | `void request_stop()` | Sets the stop flag (`release`) | MS1 |
| is_stop_requested | `bool is_stop_requested() const` | Checks whether stop was requested (`acquire`) | MS1 |

### `SpscRingBuffer<T, N>` — SPSC Ring Buffer (Milestones 2–4)

Member variables:

| Type | Member | Semantics |
|------|------|------|
| `std::array<T, N>` | `buffer_` | Fixed-capacity storage (capacity fixed at compile time) |
| `alignas(64) atomic<size_t>` | `head_` | Consumer read position (cache line padding added in MS4) |
| `alignas(64) atomic<size_t>` | `tail_` | Producer write position (cache line padding added in MS4) |

Interface:

| Method | Signature | Description | Milestone |
|------|------|------|-----------|
| Constructor | `SpscRingBuffer()` | Initializes head/tail to 0 | MS2 |
| try_push | `bool try_push(T item)` | Non-blocking write; returns false when full | MS2 |
| try_pop | `std::optional<T> try_pop()` | Non-blocking read; returns nullopt when empty | MS2 |
| empty | `bool empty() const` | Whether the buffer is empty | MS2 |
| full | `bool full() const` | Whether the buffer is full | MS2 |

## Milestone 1: Atomic Metrics Components

### Objective

Implement the three components `AtomicCounter`, `AtomicMaxTracker`, and `StopFlag`. The focus is on picking the right memory order for each operation—not every operation needs the default `seq_cst`.

### Why

These three components are infrastructure tools for all the Labs that follow. The thread pool needs `AtomicCounter` to count completed tasks, the echo server needs `AtomicMaxTracker` to track peak concurrent connections, and every Lab needs `StopFlag` for graceful shutdown. Get them right once, and you won't have to agonize over memory order choices again.

### Implementation Guide

For `AtomicCounter`, `increment` with `fetch_add(1, std::memory_order_relaxed)` is enough—all we care about is an accurate count, not a synchronization relationship with other variables. `get` with `load(std::memory_order_relaxed)` is the same story. A relaxed atomic guarantees atomicity (no half-written values) but no ordering relative to other operations—and for pure counting, that is exactly what we want.

`AtomicMaxTracker` is a bit more involved. `update` needs a CAS loop: read the current maximum, try to replace it if the new value is larger, and retry if another thread got there first. `compare_exchange_weak` is fine here—the CAS loop itself already handles failed retries, so the weak version's spurious failures are not a problem.

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

`StopFlag` is the simplest of the three—an `atomic<bool>`: `request_stop` does `store(true, release)`, and `is_stop_requested` does `load(acquire)`. The acquire-release pairing here is meaningful: everything written before `request_stop` (releasing resources, setting state, and so on) becomes visible to a thread that calls `is_stop_requested` and sees `true`.

### Verification

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

    // The maximum should be 75 (7*10+5)
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

### Objective

Implement `try_push` and `try_pop` for `SpscRingBuffer<T, N>`. The capacity N is fixed at compile time, and blocking is not supported—return false when full, nullopt when empty. This milestone doesn't fuss over memory orders yet; everything uses the default `seq_cst`.

### Why

SPSC is the simplest lock-free data structure—there is exactly one producer and one consumer, so we never have to worry about several threads modifying the same slot at once. The producer only writes `tail_`, the consumer only writes `head_`, and each side reads the other's index to judge the buffer's state. This "each side writes only its own piece" design is the core pattern of lock-free programming—eliminating write contention.

### Implementation Guide

The heart of a ring buffer is two indices: `head_` (the consumer's read position) and `tail_` (the producer's write position). `try_push` checks that `tail_ - head_ < N` (not full), then writes `buffer_[tail_ % N]` and finally increments `tail_`. `try_pop` checks that `head_ < tail_` (not empty), reads `buffer_[head % N]`, and increments `head_`.

Pseudocode:

```cpp

bool try_push(T item) {
    size_t tail = tail_.load(seq_cst);
    size_t head = head_.load(seq_cst);

    if (tail - head >= N) return false;  // full

    buffer_[tail % N] = std::move(item);
    tail_.store(tail + 1, seq_cst);
    return true;
}

optional<T> try_pop() {
    size_t head = head_.load(seq_cst);
    size_t tail = tail_.load(seq_cst);

    if (head >= tail) return nullopt;  // empty

    T item = std::move(buffer_[head % N]);
    head_.store(head + 1, seq_cst);
    return item;
}

```

Pitfall alert: index overflow. If `head_` and `tail_` keep incrementing, they will eventually overflow `size_t`. On a 64-bit system that is not a practical problem (2^64 operations would take billions of years), but if you change the type to `uint32_t`, be careful—after the wrap-around, the `tail - head` computation goes wrong.

### Verification

```cpp
TEST_CASE("Milestone 2: SPSC transfers sequential integers",
          "[lab2][milestone2]")
{
    SpscRingBuffer<int, 16> buf;
    const int kItems = 100000;

    JoiningThread producer([&]() {
        for (int i = 1; i <= kItems; ++i) {
            while (!buf.try_push(i)) {
                // spin-wait
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

    REQUIRE_FALSE(buf.try_push(5));  // full

    REQUIRE(buf.try_pop() == 1);
    REQUIRE_FALSE(buf.full());  // there's room now
    REQUIRE(buf.try_push(5));   // now it works
}
```

## Milestone 3: acquire-release Optimization

### Objective

Replace the all-`seq_cst` memory orders from Milestone 2 with the lighter acquire-release semantics. Understand which loads/stores can be `relaxed` and which must be acquire/release.

### Why

`seq_cst` is the strongest memory order—it guarantees that all threads see one consistent order of operations, but that requires extra synchronization instructions (an `MFENCE` or `LOCK` prefix on x86). In the SPSC scenario we don't need global consistency—we only need the producer's written data to become visible to the consumer. That is exactly what acquire-release semantics do: everything the producer writes before its `store(release)` becomes visible to the consumer after its `load(acquire)`.

### Implementation Guide

The key analysis: in `try_push`, the write to `buffer_[tail % N]` must complete before `tail_.store(tail + 1, release)`—that way, when the consumer sees the new `tail_`, the contents of `buffer_` are already in place. In `try_pop`, the read of `buffer_[head % N]` must complete before `head_.store(head + 1, release)`—that way, when the producer sees the new `head_`, the contents of `buffer_` have already been taken out and the slot can be safely overwritten.

The concrete replacement strategy:

- The load of `head_` in `try_push` can be `relaxed`—the producer doesn't care about the consumer's exact position, only about whether there is still room; a slightly stale value doesn't matter
- The store of `tail_` in `try_push` must be `release`—it guarantees the buffer write completes before the tail update
- The load of `tail_` in `try_pop` can be `relaxed`—same reasoning
- The store of `head_` in `try_pop` must be `release`—it guarantees the buffer read completes before the head update

Pitfall alert: if you mistakenly change the `tail_` store to `relaxed`, the consumer may see data whose write has not finished yet. This kind of bug is nearly impossible to reproduce during development (x86's strong memory model hands you store-store ordering for free), but it will show up on ARM.

### Verification

```cpp
TEST_CASE("Milestone 3: acquire-release SPSC correctness",
          "[lab2][milestone3]")
{
    // Same test as Milestone 2, but running on the acquire-release version
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

## Milestone 4: Cache Line Padding and Eliminating False Sharing

### Objective

Add cache line padding to `SpscRingBuffer` so that `head_` and `tail_` don't share the same cache line. Compare performance numbers before and after padding.

### Why

ch00-03 covered false sharing: when two atomic variables happen to land on the same cache line (usually 64 bytes), one thread modifying variable A invalidates the cache line holding the other thread's variable B, even though B was never modified at all. In the SPSC scenario, `head_` and `tail_` are modified at high frequency by different threads—if they sit on the same cache line, every modification causes a cache miss on the other side, and performance can drop several-fold.

### Implementation Guide

The fix is to insert padding between `head_` and `tail_`, forcing them onto different cache lines. C++11 provides the `alignas` specifier:

```cpp

alignas(64) atomic<size_t> head_{0};
// 64-byte alignment ensures head_ owns a cache line exclusively

char padding_[64 - sizeof(atomic<size_t>)];
// Pads the remaining space (if needed)

alignas(64) atomic<size_t> tail_{0};
// tail_ also gets a cache line of its own

```

The cleaner approach is to put `alignas(64)` directly on the member declarations and let the compiler insert the padding automatically. In real tests you should see throughput improve once false sharing is eliminated—especially on ARM, where the difference can be dramatic.

Verification for this milestone is mainly a performance comparison. Use Catch2's `BENCHMARK` macro (or manual timing) to measure how long the same number of push/pop operations takes before and after padding. The exact numbers depend on your hardware, but you should observe at least a difference at the order-of-magnitude level.

### Verification

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
    // Performance comparison test—no REQUIRE needed, just observe the output
    const int kItems = 1000000;
    const int kRounds = 10;

    // Measure the current (padded) version
    auto padded_time = benchmark_spsc<SpscRingBuffer<int, 256>>(
        kItems, kRounds);

    // You can additionally implement an UnpaddedSpscRingBuffer for comparison
    // auto unpadded_time = benchmark_spsc<UnpaddedSpscRingBuffer<int, 256>>(
    //     kItems, kRounds);

    // Report the result (no REQUIRE, because performance numbers vary by environment)
    std::cout << "Padded SPSC: " << padded_time << " us\n";
}
```

## Milestone 5: Benchmarking Against the Mutex Queue

### Objective

Use a unified benchmark methodology to compare the throughput of `SpscRingBuffer` (lock-free) and `BoundedBlockingQueue` (mutex) in the SPSC scenario.

### Why

Many people see the words "lock-free" and assume it must be faster, but the reality is not that simple. Under low contention, a mutex doesn't cost much (on x86, an uncontended futex is just one atomic instruction); in high-frequency single-thread scenarios, an atomic busy-wait can burn more CPU than a mutex's sleep-wait. Only by letting the data speak can you pin down under exactly which conditions "faster" holds.

### Implementation Guide

Measure according to the unified benchmark methodology (later Labs share this same set of rules):

1. **Measurement target**—state clearly whether you are measuring throughput (ops/s), latency, or scalability; measure one thing at a time.
2. **Warm-up**—run 5 rounds that don't count, letting caches and branch predictors reach steady state.
3. **Multiple rounds**—at least 10 official rounds; take the **median** (don't settle for the mean or a single run).
4. **Pin CPU affinity**—use `taskset` or `pthread_setaffinity_np` to pin threads to fixed cores so that OS core migrations don't inject noise; distinguish physical cores from hyperthreaded logical cores.
5. **Two data sizes**—one working set that fits in L3 cache and one that exceeds it, to observe cache effects.
6. **Prevent results from being optimized away**—use `benchmark::DoNotOptimize` or write to `volatile` to make sure the compiler can't eliminate the computation; pre-allocate memory to avoid allocator-lock interference.
7. **Report format**—test environment, parameters, results, conclusion and its boundaries (differences within 5% are usually not significant; focus on order-of-magnitude differences).

Pseudocode:

```cpp
auto benchmark = [&](auto& queue, int items) -> double {
    // Warm-up
    for (int i = 0; i < 3; ++i) {
        run_spsc_benchmark(queue, items);
    }

    // Official measurement
    vector<double> samples;
    for (int i = 0; i < 10; ++i) {
        auto start = steady_clock::now();
        run_spsc_benchmark(queue, items);
        auto elapsed = steady_clock::now() - start;
        samples.push_back(elapsed in microseconds);
    }

    sort(samples);
    return samples[samples.size() / 2];  // median
};
```

Your report should include: CPU model and core count, compiler and optimization level, data size, median latency, and a statement of your conclusion's boundaries—"this conclusion only holds for the SPSC scenario; it does not hold under MPMC".

### Verification

Verification for this milestone is not a traditional `REQUIRE` but a sanity check on the performance data. You need to confirm:

- The lock-free version really is faster than the mutex version in the SPSC scenario (typically 2-10x faster)
- The way the performance gap changes with data size is plausible
- You can explain why the mutex version might actually be faster under certain conditions (for example, with extremely low contention, a mutex costs almost nothing)

## Self-Check List

- [ ] `AtomicCounter` uses `relaxed` ordering; `StopFlag` uses an acquire-release pair
- [ ] The `AtomicMaxTracker` CAS loop handles concurrent updates correctly
- [ ] SPSC data transfer loses nothing, duplicates nothing, and keeps the order
- [ ] Tests still pass after acquire-release replaces seq_cst
- [ ] After cache line padding, `head_` and `tail_` are no longer on the same cache line
- [ ] The benchmark follows the unified methodology (warm-up, multiple rounds, take the median)
- [ ] You can explain the performance differences among relaxed vs acquire-release vs seq_cst
- [ ] You can explain how false sharing arises and how padding eliminates it
- [ ] You can state under which conditions the lock-free approach beats mutex, and under which it may not
- [ ] All tests report no data races under TSan
