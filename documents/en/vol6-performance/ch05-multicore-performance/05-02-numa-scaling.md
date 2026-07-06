---
chapter: 5
cpp_standard:
- 17
description: Multicore performance isn't just "more cores, more speed". This article covers the scalability curve (1/2/4/8 cores on the same task, measured at 1 to 2.53x sublinear, and why it's nowhere near ideal linear), Amdahl (fixed size) vs Gustafson (size scales with cores), NUMA cross-node memory-access latency doubling, thread affinity pinning, and the cost of thread creation and stack. The single-NUMA-node limit of WSL2 is marked honestly
difficulty: advanced
order: 2
platform: host
prerequisites:
- False sharing - one cacheline dragging many cores back to single-core
- Amdahl's law - the optimization ceiling (ch00)
reading_time_minutes: 5
related:
- Lock cost and "lock-free is not a silver bullet"
tags:
- host
- cpp-modern
- advanced
- 优化
- 并发
title: 'NUMA, affinity, and the scalability curve'
translation:
  source: documents/vol6-performance/ch05-multicore-performance/05-02-numa-scaling.md
  source_hash: cac21f66301af0929ecc4f4194755b9d2a1666352d8637487455663543992bb0
  translated_at: '2026-07-06T00:00:00+00:00'
  engine: manual
  token_count: 3300
---
# NUMA, affinity, and the scalability curve

## More cores does not equal linear speedup

A lot of people intuit that "4 cores means 4x faster than 1 core", and that parallelization is just "split the work into N pieces for N cores". Once you actually try it, you find **almost no program scales linearly**. We measured with the simplest parallel task (chunked sum of a hundred million elements), the speedup at 1/2/4/8 threads:

```text
===== Scalability curve (parallel sum of 100M elements) =====
threads   time(ms)    speedup
1              29.3         1.00x
2              17.2         1.70x
4              12.6         2.33x
8              11.6         2.53x
```

**8 threads buys only a 2.53x speedup, far from the ideal 8x.** Why? Three reasons, each covered below:

1. **Amdahl's law**: there's a serial section in the program (reducing results, synchronization), and no matter how small its share, it locks the speedup ceiling. ch00-01 covered `S = 1/(s + (1-s)/N)`; 10% serial locks the unlimited-core speedup at 10x.
2. **Shared-resource contention**: in this example, all cores read the same DRAM, and **memory bandwidth is shared**. Sum reduction is memory-bound (recall ch03-01: dot's arithmetic intensity is very low, bandwidth-bound); once cores multiply, bandwidth saturates first and adding cores doesn't help. **Memory-bound tasks scale poorly by nature**, and only compute-bound tasks scale well.
3. **NUMA cross-node**: on multi-socket machines, a core accessing "remote socket memory" sees latency double to quadruple.

The scalability curve is the gold standard for diagnosing multicore programs: **run 1/2/4/8 cores once and plot it**. Ideally it's a straight 45-degree line; once it bends and flattens, you've hit one of the three bottlenecks above. Where it bends and how hard tells you how much "performance you can still buy by adding cores".

## Amdahl vs Gustafson: strong scaling vs weak scaling

There are two different yardsticks for scalability, don't mix them:

- **Amdahl (strong scaling)**: **fix the problem size**, add cores, watch the speedup. The ceiling is locked by the serial section. This is what most "I want to make this program faster" scenarios care about.
- **Gustafson (weak scaling)**: **scale the problem size with the core count**, watch whether "cores double plus data double, time stays the same" holds. This is what HPC / big-data scenarios care about: data grows, add cores to keep up.

Corollary: "this program scales poorly" is a hard flaw under Amdahl but might not matter under Gustafson. It depends on whether your question is "run this fixed size faster" or "don't crash as the data grows". Figure out which one you care about first.

## NUMA: the hidden latency of multi-socket

**NUMA (Non-Uniform Memory Access)** is the reality of multi-socket servers: each CPU socket has "its own" local memory, and accessing another socket's memory goes over the interconnect (QPI/UPI), **with latency doubling to quadruple**. So "memory bandwidth" effectively becomes "interconnect bandwidth": a thread runs on socket 0, but the data lives in socket 1's memory, and every memory access pays the interconnect toll.

The fix for NUMA is to bind "the thread" and "the data it operates on" onto the same socket:

```bash
# Bind threads and memory both to NUMA node 0
numactl --cpunodebind=0 --membind=0 ./your_app
# Or interleave allocation (let both nodes share the load evenly, avoid one side saturating) -- but with a cross-node penalty
numactl --interleave=all ./your_app
```

At the program level: the thread pool is partitioned by NUMA topology (one pool per socket, handling only data in this socket's memory), and data is partitioned by socket. This is standard gear in HPC and high-performance backends.

> **Limit of this machine**: WSL2 on a single-socket laptop (5800H, single NUMA node, `numactl --hardware` lists only node0), **can't measure the NUMA cross-node penalty**. The NUMA commands (`numactl`) and data here are drawn from Bakhvalov §11 plus multi-socket server practice. To measure NUMA yourself you need a dual-socket server. This section honestly marks "can't measure on this machine" and treats it as a measurement-environment teaching point rather than glossing over it.

## Affinity: pin threads to cores to cut migration

Even on a single socket, threads being **migrated** between cores by the OS costs something: after a migration, all of the thread's L1/L2 cache is cold and has to warm up again. `taskset` (ad-hoc) / `pthread_setaffinity_np` (in-program) **pins a thread to a fixed core** and eliminates the migration cost:

```bash
# Command line: pin the process to cores 0-3
taskset -c 0-3 ./your_app
```

```cpp
// In-program: pin a thread to a specific core
cpu_set_t cpuset; CPU_ZERO(&cpuset); CPU_SET(core_id, &cpuset);
pthread_setaffinity_np(thread.native_handle(), sizeof(cpuset), &cpuset);
```

Pinning matters for **long-running, cache-sensitive** workloads (databases, stream processing); for short tasks it doesn't matter much (the cache never warms up in time). Pinning also composes with NUMA, keeping a thread on its own socket's cores.

> The 5th and 8th items in ch01-03's "measurement pitfalls" (pinning, NUMA) are exactly what this section covers; here we expand on the why. Pinning is both a standard move in **performance measurement** (to avoid migration noise) and a standard move in **production deployment** (to stabilize cache behavior).

## The cost of thread creation and stack

One easily-overlooked multicore cost: **threads themselves aren't free**. Creating a thread allocates a stack (8MB of virtual address space by default, Linux allocates as much as touched), kernel data structures, and takes tens to a hundred-plus microseconds. So:

- **Don't `new` threads on the hot path**: `std::thread t(...)` pays the create-plus-destroy cost every time. Reuse with a **thread pool**.
- **Stack size is tunable**: the 8MB default is too much for most threads; `pthread_attr_setstacksize` can shrink it (say to 256KB-1MB), saving virtual memory and easing TLB pressure (the stack is memory too, and eats TLB entries). Common in embedded / ultra-high-concurrency scenarios.
- **`std::async` + the default policy**: may implicitly create threads, and has pitfalls with `std::launch::async` semantics (vol5 covers this in depth).

The thread pool is the classic "correctness belongs to vol5, cost belongs to vol6": how to write a UB-free thread pool is vol5's job; here we only cover the cost motivation for "why you should use a pool instead of raw `std::thread`".

In one sentence: the scalability curve is just 1/2/4/8 cores on the same task, watching whether the speedup bends flat (ideal is linear; a bend means you hit Amdahl / shared resources / NUMA); memory-bound tasks scale poorly by nature because shared memory bandwidth saturates first, and only compute-bound tasks scale well; Amdahl (fixed size) vs Gustafson (size scales with cores), figure out which one you care about first; on NUMA, threads and data must be pinned to the same socket (`numactl`), and this WSL2 single-node machine can't measure it; affinity pinning cuts migration and belongs in both measurement and production; threads themselves aren't free, use a thread pool on the hot path, and the stack size is tunable.

## References

- Bakhvalov, *Performance Analysis and Tuning on Modern CPUs* Chapter 11 *Multithreaded Apps* (written by Mark Dawson; covers NUMA/affinity/scalability)
- Drepper, *What Every Programmer Should Know About Memory* — NUMA and cache coherence from an engineering angle
- `numactl` / `taskset` / `pthread_setaffinity_np` documentation
- ch00-01 Performance mindset (this volume; the source of Amdahl's law)
- The measurement code for this article: `code/volumn_codes/vol6-performance/ch05/scalability.cpp`
