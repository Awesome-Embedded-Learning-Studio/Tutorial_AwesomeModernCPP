---
chapter: 10
cpp_standard:
- 17
- 20
description: Master atomic, memory_order, false sharing, and benchmarking methodologies
  using atomic counters and single-producer single-consumer ring buffers.
difficulty: intermediate
order: 2
prerequisites:
- '卷五 ch03: 原子操作与内存模型'
- 'Lab 0: Thread Lifecycle Lab'
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
  translated_at: '2026-06-16T04:07:18.297825+00:00'
  engine: anthropic
  token_count: 3307
---
# Lab 2: Atomic Metrics and SPSC Ring Buffer

## Objectives

In Lab 1, we relied exclusively on mutex and condition_variable—locking, waiting, and waking up. While the logic is clear, the overhead is significant. Every lock/unlock operation involves system calls in kernel mode (futex), which is unacceptable in high-frequency scenarios (e.g., passing millions of messages per second). In this Lab, we enter a different world: implementing lock-free data exchange using atomic operations and memory ordering.

We will first implement a set of atomic metric components—counters, maximum value trackers, and stop flags—which will be used repeatedly for performance monitoring in subsequent Labs. Then, we will implement a fixed-capacity SPSC (Single-Producer Single-Consumer) ring buffer, using acquire-release semantics to guarantee data visibility and cache line padding to eliminate false sharing. Finally, we will run benchmarks against the mutex queue from Lab 1 to demonstrate the applicable scenarios for each approach with data.

## Prerequisites

Before starting, ensure you have read the following chapters:

- **ch03-01**: Atomic operations — `std::atomic`, `load`/`store`/`exchange`/`compare_exchange`, `is_lock_free`
- **ch03-02**: Memory ordering deep dive — Semantics and overhead of `relaxed`, `acquire-release`, `seq_cst`
- **ch03-03**: `memory_order_fence` and barriers — Use cases for explicit fences
- **ch03-04**: Atomic wait and reference semantics — `wait`/`notify`/`address`
- **ch03-05**: Atomic operation patterns — Common atomic usage patterns

This Lab does not depend on components from Lab 1, but it is recommended to complete Lab 1 first to understand the baseline comparison for the mutex solution.

## Environment Setup

Same as Lab 1. Additionally, for performance testing, it is recommended to run on Linux (requires `perf` support). WSL2 users can use `perf` directly.

Disabling CPU frequency scaling can improve benchmark stability (requires `sudo`):

```bash
sudo cpupower frequency-set --governor performance
```

## Final Interface

### `AtomicCounter` — Atomic Counter (Milestone 1)

Member variable: Holds a `std::atomic<uint64_t>` internally.

| Method | Signature | Description | Milestone |
|------|------|------|-----------|
| Constructor | `AtomicCounter(uint64_t initial = 0)` | Sets initial value | MS1 |
| increment | `void increment(uint64_t v = 1)` | Atomic increment (`fetch_add`) | MS1 |
| decrement | `void decrement(uint64_t v = 1)` | Atomic decrement | MS1 |
| get | `uint64_t get() const` | Read current value | MS1 |
| exchange | `uint64_t exchange(uint64_t desired)` | Atomic replace and return old value | MS1 |

### `AtomicMax` — Atomic Maximum Tracker (Milestone 1)

Member variable: Holds a `std::atomic<uint64_t>` internally.

| Method | Signature | Description | Milestone |
|------|------|------|-----------|
| Constructor | `AtomicMax(uint64_t initial = 0)` | Sets initial maximum | MS1 |
| update | `void update(uint64_t value)` | Update max via CAS loop | MS1 |
| get | `uint64_t get() const` | Read current maximum | MS1 |

### `StopToken` — Stop Flag (Milestone 1)

Member variable: Holds a `std::atomic<bool>` internally.

| Method | Signature | Description | Milestone |
|------|------|------|-----------|
| request_stop | `void request_stop()` | Set stop flag (`store(true, release)`) | MS1 |
| is_stop_requested | `bool is_stop_requested() const` | Check if stopped (`load(acquire)`) | MS1 |

### `SPSCQueue` — SPSC Ring Buffer (Milestone 2–4)

Member variables:

| Type | Member | Semantics |
|------|------|------|
| `std::array<T, N>` | `buffer_` | Fixed capacity storage (compile-time determined) |
| `std::atomic<size_t>` | `head_` | Consumer read position (add cache line padding in MS4) |
| `std::atomic<size_t>` | `tail_` | Producer write position (add cache line padding in MS4) |

Interface:

| Method | Signature | Description | Milestone |
|------|------|------|-----------|
| Constructor | `SPSCQueue()` | Initialize head/tail to 0 | MS2 |
| try_push | `bool try_push(const T& item)` | Non-blocking write, returns false if full | MS2 |
| try_pop | `std::optional<T> try_pop()` | Non-blocking read, returns nullopt if empty | MS2 |
| empty | `bool empty() const` | Check if buffer is empty | MS2 |
| full | `bool full() const` | Check if buffer is full | MS2 |

## Milestone 1: Atomic Metric Components

### Objectives

Implement `AtomicCounter`, `AtomicMax`, and `StopToken`. The key is to choose the appropriate memory order for each operation—not all operations require the default `seq_cst`.

### Why

These three components are infrastructure tools for all subsequent Labs. Thread pools need `AtomicCounter` to count completed tasks, echo servers need `AtomicMax` to track peak concurrent connections, and all Labs need `StopToken` for graceful shutdown. Getting them right now means we won't have to struggle with memory order choices later.

### Implementation Guide

`AtomicCounter`'s `increment` can use `memory_order_relaxed`—we only care about the accuracy of the count, not about establishing synchronization with other variables. `decrement` uses `relaxed` for the same reason. This is because relaxed atomics guarantee atomicity (no torn reads/writes), but not ordering with respect to other operations—which is exactly what we want for a pure counter.

`AtomicMax` is slightly more complex. `update` requires a CAS loop: read the current max, if the new value is larger, try to replace it; if another thread beats you to it, retry. Here, `compare_exchange_weak` is sufficient—the CAS loop handles retries, so the spurious failure of the weak version is not an issue.

```cpp
void AtomicMax::update(uint64_t value) {
    uint64_t old = get();
    while (value > old) {
        // weak is allowed: we loop anyway on spurious failure
        if (max_.compare_exchange_weak(old, value, std::memory_order_relaxed)) {
            return;
        }
        // old is updated by CAS on failure
    }
}
```

`StopToken` is the simplest—a `std::atomic<bool>`. `request_stop` uses `release`, and `is_stop_requested` uses `acquire`. This acquire-release pair is meaningful: all writes before `request_stop` (such as cleaning up resources, setting state) become visible to the thread calling `is_stop_requested` and seeing `true`.

### Verification

```bash
make test_milestone_1
```

## Milestone 2: SPSC Ring Buffer Basics

### Objectives

Implement `try_push` and `try_pop` for `SPSCQueue`. Fixed capacity N, determined at compile time, no blocking—returns `false` if full, `nullopt` if empty. For this milestone, don't worry about memory order yet; use the default `seq_cst` everywhere.

### Why

SPSC is the simplest lock-free data structure—only one producer and one consumer, so we don't have to worry about multiple threads modifying the same location simultaneously. The producer only writes `tail_`, the consumer only writes `head_`, and they check the buffer state by reading the other's index. This design of "each thread only writes its own variable" is a core pattern of lock-free programming—eliminating write contention.

### Implementation Guide

The core of a ring buffer is two indices: `head_` (consumer read position) and `tail_` (producer write position). `try_push` checks `!full()` (not full), writes to `buffer_[tail_]`, and finally increments `tail_`. `try_pop` checks `!empty()` (not empty), reads from `buffer_[head_]`, and increments `head_`.

Pseudo-code:

```cpp
bool try_push(const T& item) {
    if (full()) return false;
    buffer_[tail_ % N] = item;
    tail_.store(tail_ + 1);
    return true;
}

std::optional<T> try_pop() {
    if (empty()) return std::nullopt;
    T item = buffer_[head_ % N];
    head_.store(head_ + 1);
    return item;
}
```

**Warning**: Index overflow. If `head_` and `tail_` increment indefinitely, they will eventually overflow `size_t`. On 64-bit systems, this isn't a practical issue (2^64 operations would take billions of years), but if you change the type to `uint32_t`, be careful—the calculation of `tail_ - head_` will be incorrect after overflow.

### Verification

```bash
make test_milestone_2
```

## Milestone 3: Acquire-Release Optimization

### Objectives

Replace the `seq_cst` memory order used in Milestone 2 with the lighter acquire-release semantics. Understand which load/store operations can use `relaxed` and which must use `acquire`/`release`.

### Why

`seq_cst` is the strongest memory order—it guarantees a consistent order of operations across all threads, but this requires extra synchronization instructions (like `mfence` or the `lock` prefix on x86). In the SPSC scenario, we don't need global consistency—we only need to guarantee that data written by the producer is visible to the consumer. This is exactly what acquire-release semantics do: all writes before the producer's `release` store become visible to the consumer after its `acquire` load.

### Implementation Guide

Key analysis: In `try_push`, writing to `buffer_` must complete before updating `tail_`—so when the consumer sees the new `tail_`, the contents of `buffer_` are ready. In `try_pop`, reading from `buffer_` must happen after updating `head_`—so when the producer sees the new `head_`, it knows the `buffer_` slot has been consumed and can be safely overwritten.

Specific replacement strategy:

- In `try_push`, reading `head_` can use `relaxed`—the producer doesn't care about the consumer's exact position, only whether there is space; slight delay is acceptable.
- In `try_push`, writing `buffer_` must use `release`—guaranteeing the buffer write completes before the tail update.
- In `try_pop`, reading `tail_` can use `relaxed`—same logic as above.
- In `try_pop`, writing `head_` must use `release`—guaranteeing the buffer read completes before the head update.

**Warning**: If you incorrectly change the `tail_` store to `relaxed`, the consumer might see data that hasn't been fully written. This bug is nearly impossible to reproduce during development (because x86's strong memory model naturally guarantees store-store ordering), but it will expose itself on ARM architectures.

### Verification

```bash
make test_milestone_3
```

## Milestone 4: Cache Line Padding and False Sharing Elimination

### Objectives

Add cache line padding to `SPSCQueue` to ensure `head_` and `tail_` do not share the same cache line. Compare performance data before and after padding.

### Why

As discussed in ch00-03, false sharing occurs when two atomic variables happen to be on the same cache line (usually 64 bytes). One thread modifying variable A invalidates the cache line holding another thread's variable B, even if B wasn't modified. In the SPSC scenario, `head_` and `tail_` are modified frequently by different threads—if they are on the same cache line, every modification causes the other's cache miss, potentially degrading performance by several times.

### Implementation Guide

The solution is to insert padding between `head_` and `tail_` to force them onto different cache lines. C++11 provides the `alignas` specifier:

```cpp
alignas(64) std::atomic<size_t> head_;
char padding1[64 - sizeof(std::atomic<size_t>)];
alignas(64) std::atomic<size_t> tail_;
```

A cleaner approach is to use `alignas(64)` directly on the class member declaration, and the compiler will automatically insert padding. In actual testing, you should see a throughput increase after eliminating false sharing—especially on ARM architectures where the difference will be very pronounced.

Verification for this milestone is primarily about performance comparison. Use Catch2's `BENCHMARK` macro (or manual timing) to measure the time taken for the same number of push/pop operations before and after padding. Specific numbers depend on your hardware, but you should observe at least an order of magnitude difference.

### Verification

```bash
make test_milestone_4
```

## Milestone 5: Benchmark Comparison with Mutex Queue

### Objectives

Use a unified benchmark methodology to compare the throughput of `SPSCQueue` (lock-free) and `MutexQueue` (mutex) in an SPSC scenario.

### Why

Many people assume "lock-free" automatically means faster, but the reality is not that simple. In low-contention scenarios, mutex overhead is actually quite small (on x86, a uncontended futex is just one atomic instruction); in high-frequency single-threaded scenarios, atomic busy-waiting might consume more CPU than mutex sleep-waiting. Only by looking at data can we clarify under what conditions "faster" actually holds true.

### Implementation Guide

Follow this unified benchmark methodology (shared across subsequent Labs):

1. **Measurement Target** — Clearly define what is being measured: throughput (ops/s), latency, or scalability. Measure only one at a time.
2. **Warm-up** — Run 5 rounds that don't count, allowing caches and branch prediction to reach a steady state.
3. **Multiple Rounds** — Run at least 10 formal rounds and take the **median** (don't just take the average or a single run).
4. **Fix CPU Affinity** — Use `pthread_setaffinity_np` or `std::os::linux::set_cpu_affinity` to pin threads to fixed cores, avoiding noise from OS migration; distinguish between physical cores and hyperthreading logical cores.
5. **Two Data Scales** — One dataset size fits within L3 cache, another exceeds L3, to observe cache effects.
6. **Prevent Optimization** — Use `DoNotOptimize` or write to `volatile` to ensure calculations aren't eliminated by the compiler; pre-allocate memory to avoid allocator lock interference.
7. **Report Format** — Test environment, parameters, results, conclusions, and boundaries (differences within 5% are usually insignificant; focus on order-of-magnitude differences).

Pseudo-code:

```cpp
// Pseudo-code for benchmark
void benchmark_spsc() {
    // 1. Pin threads to Core 0 and Core 1
    set_affinity(producer_thread, 0);
    set_affinity(consumer_thread, 1);

    // 2. Warm-up
    for (int i = 0; i < 5; ++i) { run_test(); }

    // 3. Collect data
    std::vector<double> latencies;
    for (int i = 0; i < 10; ++i) {
        auto start = now();
        run_test(); // Run 1,000,000 ops
        auto end = now();
        latencies.push_back(end - start);
    }

    // 4. Report median
    std::sort(latencies.begin(), latencies.end());
    double median = latencies[latencies.size() / 2];
    std::cout << "Median latency: " << median << " ns\n";
}
```

Your report should include: CPU model and core count, compiler and optimization level, data scale, median latency, and an explanation of your conclusion boundaries—e.g., "This conclusion applies only to SPSC scenarios and does not hold for MPMC scenarios."

### Verification

Verification for this milestone is not a traditional `TEST_CASE`, but a sanity check of performance data. You need to confirm:

- The lock-free version is indeed faster than the mutex version in SPSC scenarios (usually 2-10x faster).
- The trend of performance difference changing with data scale is reasonable.
- You can explain why the mutex version might be faster under certain conditions (e.g., when contention is extremely low, mutex overhead is near zero).

## Self-Check List

- [ ] `AtomicCounter` uses `relaxed` order, `StopToken` uses acquire-release pair
- [ ] `AtomicMax`'s CAS loop correctly handles concurrent updates
- [ ] SPSC data transfer has no loss, no duplication, and correct ordering
- [ ] Tests pass after replacing `seq_cst` with acquire-release
- [ ] After cache line padding, `head_` and `tail_` are not on the same cache line
- [ ] Benchmarks follow unified methodology (warm-up, multiple runs, median)
- [ ] Can explain the performance difference between relaxed, acquire-release, and seq_cst
- [ ] Can explain the principle of false sharing and how padding eliminates it
- [ ] Can explain under what conditions the lock-free solution outperforms the mutex solution, and when it might not
- [ ] All tests pass under TSan with no data race reports
