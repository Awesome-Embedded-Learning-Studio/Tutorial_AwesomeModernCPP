---
chapter: 5
cpp_standard:
- 17
description: False sharing is the most hidden and most dramatic trap in multicore performance. Two unrelated variables happen to sit on the same 64-byte cacheline; two cores write them separately and the hardware coherence protocol invalidates the other core's cacheline on every write, forcing them to run serially in effect. This article measures it with two threads incrementing counters, false sharing is about an order of magnitude slower than alignas(64) (about 18x in a single run on this machine, multiplier varies a lot between runs), and explains the MESI coherence protocol
difficulty: advanced
order: 1
platform: host
prerequisites:
- Cachelines and locality - the 64-byte minimum unit of transfer
- Benchmark methodology reference card
reading_time_minutes: 6
related:
- NUMA, affinity, and the scalability curve
- Lock cost and "lock-free is not a silver bullet"
tags:
- host
- cpp-modern
- advanced
- 优化
- 并发
title: 'False sharing: one cacheline dragging many cores back to single-core'
translation:
  source: documents/vol6-performance/ch05-multicore-performance/05-01-false-sharing.md
  source_hash: 2654b223c503fb37c157be4f33a5b46835fa269a121e4d81caccb19e2923adfd
  translated_at: '2026-07-06T00:00:00+00:00'
  engine: manual
  token_count: 3200
---
# False sharing: one cacheline dragging many cores back to single-core

## The foreshadowing ch02 planted, paid off here

In ch02-02 on cachelines I planted a foreshadowing: cacheline "sharing" is a dividend on a single core (spatial locality), but on multicore it can turn into a tax. This article pays it off.

Recall that a cacheline is the minimum unit of cache, 64 bytes, and **also the minimum unit of coherence**: the hardware guarantees that the same cacheline is consistent across the whole system. Under multicore, the price of that guarantee is **false sharing**. Two cores frequently write **different variables sitting on the same cacheline**, and to maintain coherence the hardware invalidates the other core's copy of that cacheline on every write, so the two cores are **forced to run serially in effect**: it looks parallel, but they keep kicking each other's cache.

This is the most hidden trap in multicore performance. At the code level the two threads operate on **completely unrelated variables**, with no logical sharing, but at the performance level they're tied together by an invisible cacheline. It doesn't make the program wrong (correctness is fine), it just makes the program mysteriously slow.

## MESI coherence: why the cacheline is the multicore battlefield

To understand false sharing you first have to understand **the cache coherence protocol** (x86 uses MESI and its variants). Every cacheline has a state in every core's cache:

- **M(odified)**: only my core has it, and I've modified it (dirty).
- **E(xclusive)**: only my core has it, not modified (clean).
- **S(hared)**: multiple cores have the same copy (clean).
- **I(nvalid)**: invalid, can't be used.

When core A wants to write an S-state (shared) cacheline, it has to signal the other cores first, "I'm about to write, invalidate this line", and the others mark their copy of that cacheline as I. Next time core B wants to use that cacheline, it finds its copy is I and has to fetch it again (cache-to-cache or from memory). **This "invalidate the other side plus re-fetch" round-trip is where the cost of false sharing comes from.**

The key point: **the granularity of coherence is the cacheline (64 bytes), not a single variable**. So even if core A writes `counter_a` and core B writes `counter_b`, as long as `counter_a` and `counter_b` sit in the same 64 bytes, the hardware treats it as "the same line being modified on both sides" and triggers the full invalidate round-trip. The variables are logically unrelated but physically on the same boat.

## Run it: the cost of an order of magnitude

The classic scenario: two threads each have a counter and each increments it a hundred million times. Completely independent logically.

```cpp
// A. False sharing: two atomic<long> packed together, same cacheline
struct BadCounters { std::atomic<long> a{0}; std::atomic<long> b{0}; };  // sizeof = 16B
// B. No false sharing: each alignas(64) occupies its own cacheline
struct alignas(64) PaddedCounter { std::atomic<long> v{0}; };
struct GoodCounters { PaddedCounter a; PaddedCounter b; };              // sizeof = 128B
```

Two threads each touch only their own counter (`a` and `b`), running the same number of times:

```text
===== False sharing (2 threads each incrementing 100M times) =====
  false sharing (same cacheline):      467.0 ms
  alignas(64) (own cacheline):          26.0 ms
  false sharing / aligned = 18.0x
  sizeof(BadCounters)=16  sizeof(GoodCounters)=128
```

**Close to 20x (an order of magnitude).** Same amount of computation, same number of threads, and the only difference is whether the two counters are crammed onto the same cacheline. That spreads them apart by an order of magnitude. **The absolute multiplier varies a lot between runs**: on this machine, reproducing on the same hardware, I've seen the multiplier anywhere from 15x to 48x (the run above is 18x), and WSL2 scheduling noise amplifies the jitter; but "an order of magnitude apart" is a stable conclusion. `BadCounters` is 16 bytes (two `atomic<long>` crammed into one 64B cacheline); `GoodCounters` uses `alignas(64)` so each counter owns its own line, 128 bytes total, and false sharing is gone.

That's how lethal false sharing is: **it can drag a program that "looks perfectly parallel" down to nearly single-threaded speed**. The nastier part is that a correct TSan/ASan run won't catch it (because it's not a data race, not UB, logically correct); only a **performance-oriented profiler** can:

```bash
# perf's dedicated false-sharing tool (this WSL2 machine has no perf; command from KDAB/Brendan Gregg):
perf c2c record -- ./your_app
perf c2c report
# Look at the HITM (Hit Modified) counts; the hot spots are the false-sharing disaster zones
```

## The fix: alignas(64) gives hot variables their own cacheline

The fix is almost brutally direct: **make any variable that different cores will write frequently occupy its own cacheline**. In C++ that's `alignas(64)`:

```cpp
struct alignas(64) AlignedCounter { std::atomic<long> v{0}; };
```

`alignas(64)` forces this struct's address to be 64-byte aligned, and `sizeof` is also padded to a multiple of 64, so it occupies the whole cacheline and nobody else can cram in. Common usages:

- **Per-thread statistics counters**: thread `i` writes `counters[i]`; if `counters` is `atomic<long>[]` you get false sharing; switch to an array of `alignas(64)` structs and you don't.
- **Per-thread data in lock-free data structures**: per-thread slots in a ring buffer.
- **Per-worker state in a thread pool**.

Note that `alignas(64)` **wastes memory** (each counter takes 64B instead of 8B), but for hot variables this trade is worth it: the cacheline round-trips you save are worth far more than the bytes you waste. Don't sprinkle it on cold variables.

Since C++17 there's a cleaner way: put per-thread data in `thread_local` and the compiler gives each thread its own instance, eliminating sharing at the root. But `thread_local` has its own pitfalls (initialization cost, interaction with thread pools); pick by scenario.

> Boundary reminder: false sharing is a **performance** problem and belongs to vol6; "how to write correct multithreaded synchronization and the memory-ordering semantics of atomic operations" belongs to vol5. This article only covers "the performance cost of cachelines under multicore".

Compress this article into one sentence: multiple cores frequently writing different variables on the same cacheline triggers MESI invalidate round-trips and effectively serializes them; on-machine measurement is an order of magnitude apart (about 18x in a single run on this machine, multiplier varies from 15x to 48x between runs; "an order of magnitude" is the stable conclusion); the fix is `alignas(64)` giving each frequently-written variable its own cacheline, or putting per-thread data in `thread_local`; to find false sharing use `perf c2c` (look at HITM counts), TSan/ASan won't catch it because it isn't a correctness problem. The **depth** of coherence protocols (MESI state machines, MOESI/MESIF variants, cache-to-cache transfer) belongs to an architecture course; vol6 only covers the "cacheline is the unit of coherence" layer, enough to guide code changes.

The next article covers the other multicore amplifier, NUMA, and how to use the scalability curve to judge whether your parallel program "scales well".

## References

- CppCoreGuidelines CP.3 *false sharing* — the definition and fix for false sharing
- Bakhvalov, *Performance Analysis and Tuning on Modern CPUs* §11.7 *Detecting Coherence Issues* (written by Mark Dawson)
- Drepper, *What Every Programmer Should Know About Memory* — MESI and multicore cache coherence
- perf c2c documentation (KDAB has a detailed tutorial)
- The measurement code for this article: `code/volumn_codes/vol6-performance/ch05/false_sharing.cpp`
