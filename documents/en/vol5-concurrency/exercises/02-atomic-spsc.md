---
chapter: 10
cpp_standard:
- 17
- 20
description: Master atomic, memory_order, false sharing, and benchmarking methodologies
  via atomic counters and single-producer single-consumer ring buffers.
difficulty: intermediate
order: 2
prerequisites:
- '卷五 ch03: 原子操作与内存模型'
- 'Lab 0: Thread Lifecycle Lab'
reading_time_minutes: 11
tags:
- host
- cpp-modern
- atomic
- memory_order
- intermediate
title: 'Lab 2: Atomic Metrics and SPSC Ring Buffer'
translation:
  engine: anthropic
  source: documents/vol5-concurrency/exercises/02-atomic-spsc.md
  source_hash: adad8f737d9d3ef0b4cce931937876d7cf38f554eb2e1aaa2041d918845dec4c
  token_count: 3311
  translated_at: '2026-06-14T00:20:24.057615+00:00'
---
# Lab 2: Atomic Metrics and SPSC Ring Buffer

## Objectives

In Lab 1, we relied entirely on mutexes and condition variables—locking, waiting, and waking up. While the logic is clear, the overhead is significant. Every lock/unlock operation involves system calls in kernel mode (futex), which is unacceptable in high-frequency scenarios (e.g., passing millions of messages per second). In this Lab, we enter a different world: using atomic operations and memory ordering to implement lock-free data exchange.

We will first implement a set of atomic metric components—counters, maximum value trackers, and stop flags—which will be used repeatedly for performance monitoring in subsequent Labs. Then, we will implement a fixed-capacity SPSC (Single-Producer Single-Consumer) ring buffer, using acquire-release semantics to guarantee data visibility and cache line padding to eliminate false sharing. Finally, we will run benchmarks against the mutex-based queue from Lab 1 to demonstrate the applicable scenarios for each approach with real data.

## Prerequisites

Before starting, ensure you have read the following chapters:

- **ch03-01**: Atomic operations — `std::atomic`, `load`/`store`/`exchange`/`compare_exchange`, `is_lock_free`
- **ch03-02**: Memory ordering deep dive — Semantics and overhead of `relaxed`, `acquire-release`, `seq_cst`
- **ch03-03**: `memory_order_fence` and barriers — Use cases for explicit fences
- **ch03-04**: Atomic wait and reference semantics — `wait`/`notify`/`address`
- **ch03-05**: Atomic operation patterns — Common atomic usage patterns

This Lab does not depend on components from Lab 1, but it is recommended to complete Lab 1 first to understand the baseline for benchmark comparison.

## Environment Setup

Same as Lab 1. Additionally, for performance testing, it is recommended to run on Linux (requires `perf` support). WSL2 users can use `perf` directly.

Disabling CPU frequency scaling can improve benchmark stability (requires `sudo`):

```bash
sudo cpupower frequency-set --governor performance
```

## Final Interface

### `AtomicCounter` — Atomic Counter (Milestone 1)

Member variable: Internally holds `std::atomic`.

| Method | Signature | Description | Milestone |
|------|------|------|-----------|
| Constructor | `AtomicCounter(T initial = 0)` | Set initial value | MS1 |
| increment | `void increment(T delta = 1)` | Atomic increment (`fetch_add`) | MS1 |
| decrement | `void decrement(T delta = 1)` | Atomic decrement | MS1 |
| get | `T get() const` | Read current value | MS1 |
| exchange | `T exchange(T desired)` | Atomically replace and return old value | MS1 |

### `AtomicMax` — Atomic Maximum Tracker (Milestone 1)

Member variable: Internally holds `std::atomic`.

| Method | Signature | Description | Milestone |
|------|------|------|-----------|
| Constructor | `AtomicMax(T initial = 0)` | Set initial maximum value | MS1 |
| update | `void update(T value)` | Update max via CAS loop | MS1 |
| get | `T get() const` | Read current maximum value | MS1 |

### `StopToken` — Stop Flag (Milestone 1)

Member variable: Internally holds `std::atomic`.

| Method | Signature | Description | Milestone |
|------|------|------|-----------|
| request_stop | `void request_stop()` | Set stop flag (`store true`) | MS1 |
| is_stop_requested | `bool is_stop_requested() const` | Check if stopped (`load`) | MS1 |

### `SPSCRingBuffer` — SPSC Ring Buffer (Milestone 2–4)

Member variables:

| Type | Member | Semantics |
|------|------|------|
| `std::array` | `buffer_` | Fixed capacity storage (compile-time determined) |
| `std::atomic` | `head_` | Consumer read position (MS4 add cache line padding) |
| `std::atomic` | `tail_` | Producer write position (MS4 add cache line padding) |

Interface:

| Method | Signature | Description | Milestone |
|------|------|------|-----------|
| Constructor | `SPSCRingBuffer()` | Initialize head/tail to 0 | MS2 |
| try_push | `bool try_push(const T& value)` | Non-blocking write, return false if full | MS2 |
| try_pop | `std::optional try_pop()` | Non-blocking read, return nullopt if empty | MS2 |
| empty | `bool empty() const` | Is buffer empty? | MS2 |
| full | `bool full() const` | Is buffer full? | MS2 |

## Milestone 1: Atomic Metric Components

### Objectives

Implement `AtomicCounter`, `AtomicMax`, and `StopToken`. The key is to choose the appropriate memory order for each operation—not all operations require the default `seq_cst`.

### Why

These three components are infrastructure tools for all subsequent Labs. The thread pool needs `AtomicCounter` to count completed tasks, the echo server needs `AtomicMax` to track peak concurrent connections, and all Labs need `StopToken` for graceful shutdown. Getting them right now means we won't have to struggle with memory order choices later.

### Implementation Guide

`AtomicCounter`'s `increment` can use `memory_order_relaxed`—we only care about the accuracy of the count, not establishing synchronization with other variables. `decrement` uses `relaxed` for the same reason. This is because relaxed atomics guarantee atomicity (no torn reads/writes), but not ordering with respect to other operations—which is exactly what we want for a pure counter.

`AtomicMax` is slightly more complex. `update` needs a CAS loop: read the current max, if the new value is larger, try to replace it; if another thread beats us to it, retry. `compare_exchange_weak` is fine here—the CAS loop handles retries, so the spurious failure of the weak version isn't an issue.

```cpp
void update(T value) {
    T old = max_.load(std::memory_order_relaxed);
    while (value > old) {
        if (max_.compare_exchange_weak(old, value, std::memory_order_relaxed)) {
            return;
        }
    }
}
```

`StopToken` is the simplest—one `std::atomic`, `request_stop` uses `release`, `is_stop_requested` uses `acquire`. This acquire-release pair is meaningful: all writes before `request_stop` (like cleaning up resources, setting state) become visible to the thread calling `is_stop_requested` and seeing `true`.

### Verification

```bash
make test_milestone1
```

## Milestone 2: SPSC Ring Buffer Basics

### Objectives

Implement `try_push` and `try_pop` for `SPSCRingBuffer`. Fixed capacity N, determined at compile time, no blocking support—return false if full, nullopt if empty. For this milestone, don't worry about memory order; use the default `seq_cst` everywhere.

### Why

SPSC is the simplest lock-free data structure—only one producer and one consumer, so we don't have to worry about multiple threads modifying the same location simultaneously. The producer only writes `tail_`, the consumer only writes `head_`, and they check the buffer state by reading the other's index. This design of "each thread only writes its own index" is a core pattern of lock-free programming—eliminating write contention.

### Implementation Guide

The core of the ring buffer is two indices: `head_` (consumer read position) and `tail_` (producer write position). `try_push` checks `!full` (not full), writes to `buffer_[tail_]`, then increments `tail_`. `try_pop` checks `!empty` (not empty), reads `buffer_[head_]`, increments `head_`.

Pseudo-code:

```cpp
bool try_push(const T& value) {
    size_t curr_tail = tail_.load();
    if (full(curr_tail, head_.load())) return false;
    buffer_[curr_tail] = value;
    tail_.store((curr_tail + 1) % N);
    return true;
}

std::optional try_pop() {
    size_t curr_head = head_.load();
    if (empty(curr_head, tail_.load())) return std::nullopt;
    T value = buffer_[curr_head];
    head_.store((curr_head + 1) % N);
    return value;
}
```

Pitfall warning: Index overflow. If `head_` and `tail_` increment continuously, they will eventually overflow `size_t`. On 64-bit systems this isn't a practical issue (2^64 operations takes billions of years), but if you change the type to `uint32_t`, be careful—the calculation of `full`/`empty` will be wrong after overflow.

### Verification

```bash
make test_milestone2
```

## Milestone 3: acquire-release Optimization

### Objectives

Replace the `seq_cst` memory order used in Milestone 2 with the lighter acquire-release semantics. Understand which load/store operations can use `relaxed` and which must use acquire/release.

### Why

`seq_cst` is the strongest memory order—it guarantees a consistent order of operations across all threads, but this requires extra synchronization instructions (like `mfence` or `lock` prefix on x86). In the SPSC scenario, we don't need global consistency—we only need to guarantee that data written by the producer is visible to the consumer. This is exactly what acquire-release semantics do: all writes before the producer's `release` store become visible to the consumer after its `acquire` load.

### Implementation Guide

Key analysis: In `try_push`, writing to `buffer_` must complete before `tail_` is updated—so when the consumer sees the new `tail_`, the content of `buffer_` is ready. In `try_pop`, reading `buffer_` must happen after `head_` is loaded—so when the producer sees the new `head_`, the content of `buffer_` has been taken and can be safely overwritten.

Specific replacement strategy:

- Reading `head_` in `try_push` can use `relaxed`—the producer doesn't care about the consumer's exact position, only whether there is space; slight delay is acceptable.
- Writing `buffer_[tail_]` in `try_push` must be followed by a `release` store to `tail_`—guaranteeing the buffer write finishes before the tail update.
- Reading `tail_` in `try_pop` can use `relaxed`—same as above.
- Writing `head_` in `try_pop` must be an `release` store—guaranteeing the buffer read finishes before the head update.

Pitfall warning: If you mistakenly change the store to `tail_` to `relaxed`, the consumer might see data that hasn't been fully written. This bug is nearly impossible to reproduce during development (because x86's strong memory model naturally guarantees store-store order), but it will expose itself on ARM architectures.

### Verification

```bash
make test_milestone3
```

## Milestone 4: cache line padding and False Sharing Elimination

### Objectives

Add cache line padding to `SPSCRingBuffer` to ensure `head_` and `tail_` do not share the same cache line. Compare performance data before and after padding.

### Why

As discussed in ch00-03, false sharing occurs when two atomic variables happen to be on the same cache line (usually 64 bytes). One thread modifying variable A invalidates the cache line holding variable B for another thread, even if B wasn't modified. In the SPSC scenario, `head_` and `tail_` are modified frequently by different threads—if they are on the same cache line, every modification causes the other's cache miss, potentially degrading performance by several times.

### Implementation Guide

The solution is to insert padding between `head_` and `tail_` to force them onto different cache lines. C++11 provides the `alignas` specifier:

```cpp
alignas(64) std::atomic head_{0}; // Force start of cache line
char padding1[64 - sizeof(std::atomic)];
alignas(64) std::atomic tail_{0}; // Force start of new cache line
```

A simpler approach is to use `alignas(64)` directly on class member declarations, and the compiler will automatically insert padding. In actual testing, you should see a throughput improvement after eliminating false sharing—especially on ARM architectures where the difference will be very pronounced.

Verification for this milestone is primarily performance comparison. Use Catch2's `BENCHMARK` macro (or manual timing) to measure the time taken for the same number of push/pop operations before and after padding. Specific numbers depend on your hardware, but you should observe at least an order of magnitude difference.

### Verification

```bash
make test_milestone4
```

## Milestone 5: Benchmark Comparison with Mutex Queue

### Objectives

Use a unified benchmark methodology to compare the throughput of `SPSCRingBuffer` (lock-free) and `MutexQueue` (mutex) in an SPSC scenario.

### Why

Many people see "lock-free" and assume it must be faster, but the reality is not that simple. In low-contention scenarios, mutex overhead is actually small (on x86, a futex is just one atomic instruction when uncontended); in high-frequency single-threaded scenarios, atomic busy-waiting might consume more CPU than mutex sleep-waiting. Only by letting the data speak can we clarify under what conditions "faster" actually holds.

### Implementation Guide

Follow a unified benchmark methodology (shared across all subsequent Labs):

1. **Measurement Target** — Clearly define what is being measured: throughput (ops/s), latency, or scalability. Measure only one at a time.
2. **Warm-up** — Run 5 rounds that don't count, allowing caches and branch prediction to reach a steady state.
3. **Multiple Runs** — Run at least 10 official rounds and take the **median** (don't just take the average or a single run).
4. **Fix CPU Affinity** — Use `pthread_setaffinity_np` or `std::os::linux::set_cpu_affinity` to pin threads to fixed cores, avoiding noise from OS migration; distinguish between physical cores and hyperthreading logical cores.
5. **Two Data Scales** — One dataset size fits within L3 cache, one exceeds L3, to observe cache effects.
6. **Prevent Optimization** — Use `DoNotOptimize` or write to `volatile` to ensure calculations aren't eliminated by the compiler; pre-allocate memory to avoid allocator lock interference.
7. **Report Format** — Test environment, parameters, results, conclusions, and boundaries (differences within 5% are usually insignificant; focus on order-of-magnitude differences).

Pseudo-code:

```cpp
// Benchmark Loop
for (int round = 0; round < warmup + rounds; ++round) {
    auto start = now();
    // Producer/Consumer loop
    producer();
    consumer();
    auto end = now();
    if (round >= warmup) record_latency(end - start);
}
report_median(latencies);
```

Your report should include: CPU model and core count, compiler and optimization level, data scale, median latency, and an explanation of your conclusion boundaries—"This conclusion applies only to SPSC scenarios; it does not hold for MPMC scenarios."

### Verification

Verification for this milestone is not a traditional `TEST_CASE`, but a sanity check of performance data. You need to confirm:

- The lock-free version is indeed faster than the mutex version in SPSC scenarios (usually 2-10x faster).
- The trend of performance difference changing with data scale is reasonable.
- You can explain why the mutex version might be faster under certain conditions (e.g., when contention is extremely low, mutex overhead is near zero).

## Checklist

- [ ] `AtomicCounter` uses `relaxed` order, `StopToken` uses acquire-release pair
- [ ] `AtomicMax`'s CAS loop correctly handles concurrent updates
- [ ] SPSC data transfer has no loss, no duplication, and correct order
- [ ] Tests pass after replacing `seq_cst` with acquire-release
- [ ] After cache line padding, `head_` and `tail_` are not on the same cache line
- [ ] Benchmarks follow unified methodology (warm-up, multiple runs, median)
- [ ] Can explain the performance difference between relaxed, acquire-release, and seq_cst
- [ ] Can explain the principle of false sharing and how padding eliminates it
- [ ] Can articulate under what conditions the lock-free solution outperforms mutex, and when it might not
- [ ] All tests pass under TSan with no data race reports
