---
chapter: 5
cpp_standard:
- 17
description: Multicore performance is not just "add cores, go faster". This article
  covers the scalability curve (1/2/4/8 cores running the same task, measured at
  1→2.53× sublinear, and why that is far from ideal linear), Amdahl (fixed size) vs
  Gustafson (size grows with cores), doubled NUMA cross-node memory access latency,
  thread affinity pinning, and the cost of thread creation and stacks. The single-NUMA-node
  limit of WSL2 is honestly flagged
difficulty: advanced
order: 2
platform: host
prerequisites:
- 'False sharing: one cacheline dragging many cores back to single-core'
- "Amdahl's law: the optimization ceiling (ch00)"
reading_time_minutes: 5
related:
- 'Lock overhead and "lock-free is not a silver bullet"'
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
  translated_at: '2026-09-26T06:32:33+00:00'
  engine: anthropic
  token_count: 1250
  notes: '原文一处明显笔误按正确拼写译出：中文源第 107 行书名《What Every Programmer Should About Memory》原文疑为 What Every Programmer Should Know About Memory（缺 "Know"）。'
---
# NUMA, affinity, and the scalability curve

## Adding cores does not mean linear speedup

Many people's intuition is "4 cores means 4x faster than 1 core", and that parallelizing is just "split the task into N pieces and hand one to each of the N cores". Once you actually try it, you discover: **almost no program speeds up linearly**. We measured with the simplest parallel task there is (a chunked sum of 100 million elements) and got these speedups at 1/2/4/8 threads:

```text
===== 扩展性曲线(并行累加 1 亿元素)=====
线程数   耗时(ms)    加速比
1              29.3         1.00x
2              17.2         1.70x
4              12.6         2.33x
8              11.6         2.53x
```

**8 threads bought only a 2.53x speedup, nowhere near the ideal 8x.** Why? Three reasons, each of which we unpack below:

1. **Amdahl's law**: the program contains serial sections (reducing the results, synchronization), and however small their share, they lock the speedup ceiling. ch00-01 covered `S = 1/(s + (1-s)/N)`: with 10% serial work, even unlimited cores cap the speedup at 10x.
2. **Shared-resource contention**: in this example, all cores read the same DRAM, and **memory bandwidth is shared**. A sum reduction is memory-bound (recall ch03-01: dot has extremely low arithmetic intensity and is bandwidth-bound); once cores multiply, bandwidth saturates first and adding cores helps nothing. **Memory-bound tasks scale poorly by nature**; compute-bound tasks are the ones that scale.
3. **NUMA cross-node traffic**: on multi-socket machines, a core accessing "the remote socket's memory" sees latency 2-4x higher.

The scalability curve is the gold standard for diagnosing multicore programs: **run the same task at 1/2/4/8 cores and plot it**. Ideally you get a straight line with a 45° slope; once the curve bends over and flattens, you have hit one of the three bottlenecks above. Where it bends and how hard tells you how much "performance that more cores can still buy" is left.

## Amdahl vs Gustafson: strong scaling vs weak scaling

There are two different yardsticks for scalability — don't mix them up:

- **Amdahl (strong scaling)**: **fix the problem size**, add cores, watch the speedup. The ceiling is locked down by the serial fraction. This is what most "I want to make this program run faster" scenarios care about.
- **Gustafson (weak scaling)**: **scale the problem size proportionally with the core count**, and check whether "cores double + data doubles, time stays the same" actually holds. This is what HPC / big-data scenarios care about: the data keeps growing, and added cores keep up.

Corollary: "this program scales badly" is a hard defect under Amdahl but may not matter at all under Gustafson. It depends on whether your question is "run this fixed size faster" or "the data keeps growing, don't fall over" — figure out which one you care about first.

## NUMA: the hidden latency of multi-socket

**NUMA (Non-Uniform Memory Access)** is the reality of multi-socket servers: each CPU socket has "its own" local memory, and reaching another socket's memory goes over the interconnect (QPI/UPI), **with latency 2-4x higher**. Your "memory bandwidth" effectively gets measured as "interconnect bandwidth": the thread runs on socket 0 while the data sits in socket 1's memory, and every memory access pays the interconnect toll.

The countermeasure for NUMA is to bind "the thread" and "the data it works on" onto the same socket:

```bash
# Bind both the threads and the memory to NUMA node 0
numactl --cpunodebind=0 --membind=0 ./your_app
# Or interleave allocation (both nodes share the load evenly, neither saturates first) -- but with a cross-node penalty
numactl --interleave=all ./your_app
```

At the program level: group the thread pool by NUMA topology (one pool per socket, serving only data in that socket's memory), and partition the data by socket. These are standard practice in HPC and high-performance backends.

> **Limit of this machine**: WSL2 here runs on a single-socket laptop (5800H, a single NUMA node; `numactl --hardware` lists only node0), so we **cannot measure the NUMA cross-node penalty**. The NUMA commands (`numactl`) and the numbers cited here come from Bakhvalov §11 plus multi-socket server practice. If you want to measure NUMA yourself, you need a dual-socket server. This section honestly flags "not measurable on this machine" and treats that as a teaching point about the measurement environment rather than glossing over it.

## Affinity: pin threads to cores to reduce migration

Even on a single socket, the OS **migrating** a thread between cores has a cost: after a migration, the thread's L1/L2 caches are entirely cold and have to warm up again. `taskset` (ad-hoc) / `pthread_setaffinity_np` (in-program) **pins a thread to a fixed core** and removes the migration overhead:

```bash
# Command line: pin the process to cores 0-3
taskset -c 0-3 ./your_app
```

```cpp
// In-program: pin the thread to a specific core
cpu_set_t cpuset; CPU_ZERO(&cpuset); CPU_SET(core_id, &cpuset);
pthread_setaffinity_np(thread.native_handle(), sizeof(cpuset), &cpuset);
```

Pinning matters for **long-running, cache-sensitive** workloads (databases, stream processing); for short tasks it barely matters (the cache never warms up in the first place). Pinning also composes with NUMA: it keeps a thread running only on the cores of its own socket.

> Items 5 and 8 of ch01-03's "measurement pitfalls" (pinning, NUMA) are exactly what this section covers; here we expand on the why. Pinning is a standard move in **performance measurement** (to avoid migration noise) and equally a standard move in **production deployment** (to stabilize cache behavior).

## The cost of thread creation and stacks

One easily overlooked multicore cost: **threads themselves are not free**. Creating a thread allocates a stack (8MB of virtual address space by default; Linux commits memory only for the pages actually touched) plus kernel data structures, and takes tens to a hundred-plus microseconds. So:

- **Don't `new` threads on the hot path**: `std::thread t(...)` pays the create-plus-destroy cost every single time. Reuse threads with a **thread pool**.
- **Stack size is tunable**: the 8MB default is more than most threads need; shrinking it with `pthread_attr_setstacksize` (say to 256KB-1MB) saves virtual memory and eases TLB pressure (the stack is memory too and takes up TLB entries). Common in embedded and ultra-high-concurrency settings.
- **`std::async` + the default policy**: may implicitly create threads, and has semantic pitfalls around `std::launch::async` (vol5 covers this in depth).

The thread pool is the classic case of "correctness belongs to vol5, cost belongs to vol6": how to write a UB-free thread pool is vol5's business; here we only cover the cost motivation for "why you should use a pool instead of raw `std::thread`".

One-sentence wrap-up: the scalability curve is just running the same task at 1/2/4/8 cores and watching whether the speedup bends flat (ideal is linear; a bend means you hit Amdahl / shared resources / NUMA); memory-bound tasks scale poorly by nature because the shared memory bandwidth saturates first, while compute-bound tasks scale well; Amdahl (fixed size) vs Gustafson (size grows with cores) — figure out which one you care about first; on NUMA, threads and data must be bound to the same socket (`numactl`), which our single-node WSL2 machine cannot measure; affinity pinning reduces migration and belongs in both measurement and production; threads themselves are not free — use a thread pool on the hot path, and the stack size is tunable.

## References

- Bakhvalov, *Performance Analysis and Tuning on Modern CPUs*, Chapter 11, *Multithreaded Apps* (written by Mark Dawson; covers NUMA/affinity/scalability)
- Drepper, *What Every Programmer Should Know About Memory* — an engineering view of NUMA and cache coherence
- Documentation for `numactl` / `taskset` / `pthread_setaffinity_np`
- ch00-01 performance mindset (this volume; where Amdahl's law comes from)
- The measurement code for this article: `code/volumn_codes/vol6-performance/ch05/scalability.cpp`
