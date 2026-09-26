---
chapter: 5
cpp_standard:
- 17
description: False sharing is the most hidden — and most dramatic — trap in multicore performance. Two unrelated variables happen to be crammed onto the same 64-byte cacheline, two cores write them separately, and the hardware coherence protocol invalidates the other core's copy of that cacheline on every write, so the two serialize in effect. This article measures it with two threads incrementing counters (false sharing is an order of magnitude slower than alignas(64), about 18× in a single run on this machine with the multiplier varying a lot between runs) and explains the mechanics of the MESI coherence protocol
difficulty: advanced
order: 1
platform: host
prerequisites:
- 'Cachelines and locality: the 64-byte minimum unit of transfer'
- Benchmark methodology reference card
reading_time_minutes: 6
related:
- NUMA, affinity, and the scalability curve
- 'Lock overhead and "lock-free is not a silver bullet"'
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
  translated_at: '2026-09-26T06:30:24+00:00'
  engine: anthropic
  token_count: 3300
---
# False sharing: one cacheline dragging many cores back to single-core

## The foreshadowing ch02 planted, paid off here

Back in ch02-02, when we covered cachelines, we planted a piece of foreshadowing: cacheline "sharing" is a dividend on a single core (spatial locality), but on multiple cores it can turn into a tax. This article pays it off.

Recall that a cacheline is the minimum unit of the cache, 64 bytes, and **also the minimum unit of coherence**: the hardware guarantees that the same cacheline is consistent across the whole system. In a multicore setting, the price of that guarantee is **false sharing**. Two cores each frequently write **different variables sitting on the same cacheline**; to keep things coherent, the hardware invalidates the other core's copy of that cacheline on every write, so the two cores are **effectively forced to serialize** — "parallel on the outside, kicking each other's cache on the inside".

This is the most hidden trap in multicore performance. At the code level the two threads operate on **completely unrelated variables**, with no logical sharing at all, yet at the performance level they are chained together by an invisible cacheline. It never makes the program wrong (correctness is fine); it just makes the program inexplicably slow.

## MESI coherence: why the cacheline is the multicore battlefield

To understand false sharing, you first need to understand the **cache coherence protocol** (x86 uses MESI and its variants). Every cacheline carries a state in every core's cache:

- **M(odified)**: only my core has it, and I've modified it (dirty).
- **E(xclusive)**: only my core has it, unmodified (clean).
- **S(hared)**: multiple cores hold the same copy (clean).
- **I(nvalid)**: invalidated, unusable.

When core A wants to write a cacheline in the S (shared) state, it must first send the other cores a signal — "I'm about to write, invalidate this line" — and the other cores mark their copy of that cacheline as I. The next time core B wants that cacheline, it finds its own copy is I and has to fetch it again (cache-to-cache, or from memory). **This round trip of "invalidate the other side + re-fetch" is exactly where the cost of false sharing comes from.**

The key point: **the granularity of coherence is the cacheline (64 bytes), not the individual variable**. So even when core A writes `counter_a` and core B writes `counter_b`, as long as `counter_a` and `counter_b` sit inside the same 64 bytes, the hardware treats it as "the same line being modified on both sides" and triggers the full invalidate round trip. The variables are logically unrelated, but physically in the same boat.

## Run it yourself: an order-of-magnitude cost

The classic scenario: two threads, each with its own counter, each incrementing it 100 million times. Completely independent, logically.

```cpp
// A. False sharing: two atomic<long> packed side by side, same cacheline
struct BadCounters { std::atomic<long> a{0}; std::atomic<long> b{0}; };  // sizeof = 16B
// B. No false sharing: each alignas(64) owns its own cacheline
struct alignas(64) PaddedCounter { std::atomic<long> v{0}; };
struct GoodCounters { PaddedCounter a; PaddedCounter b; };              // sizeof = 128B
```

The two threads each touch only their own counter (`a` and `b`), for the same number of iterations:

```text
===== 伪共享(2 线程各自自增 1 亿次)=====
  伪共享(同 cacheline):      467.0 ms
  alignas(64)(独占 cacheline):  26.0 ms
  伪共享/对齐 = 18.0x
  sizeof(BadCounters)=16  sizeof(GoodCounters)=128
```

**Close to 20× (an order of magnitude).** Same amount of computation, same number of threads — the only difference is whether the two counters are crammed onto the same cacheline, and that alone spans an order of magnitude. **The absolute multiplier fluctuates a lot between runs**: reproducing many times on the same hardware here, we've seen the multiplier anywhere from 15×–48× (the run above is 18×), and WSL2's scheduling noise amplifies the jitter; but the conclusion "an order of magnitude apart" is stable. `BadCounters` is 16 bytes (two `atomic<long>` crammed into one 64B cacheline); `GoodCounters` uses `alignas(64)` so each counter owns its own line, 128 bytes total, and the false sharing is gone.

That is the killing power of false sharing: **it can drag a program that "looks perfectly parallel" down to nearly single-threaded speed**. The more insidious part: a clean TSan/ASan run won't catch it (it's not a data race and not UB — the logic is correct); only a **performance-oriented profiler** can grab it:

```bash
# perf's dedicated false-sharing tool (no perf on this WSL2 machine; command from KDAB/Brendan Gregg):
perf c2c record -- ./your_app
perf c2c report
# Watch the HITM (Hit Modified) counts; wherever they run high is a false-sharing disaster zone
```

## The fix: alignas(64) gives hot variables their own cacheline

The fix is almost brutally direct: **give any variable that different cores will write frequently a cacheline of its own**. In C++, that's `alignas(64)`:

```cpp
struct alignas(64) AlignedCounter { std::atomic<long> v{0}; };
```

`alignas(64)` forces this struct's address to be 64-byte aligned, and `sizeof` is padded up to a multiple of 64 too, so it occupies a whole cacheline and nobody else can squeeze in. Common uses:

- **Per-thread statistics counters**: thread `i` writes `counters[i]`; if `counters` is an `atomic<long>[]`, that's false sharing — switch to an array of `alignas(64)` structs and it's gone.
- **Per-thread data in lock-free data structures**: per-thread slots in a ring buffer.
- **Per-worker state in a thread pool**.

Note that `alignas(64)` **wastes memory** (each counter takes 64B instead of 8B), but for hot variables the trade is worth it: the cacheline round trips you save are worth far more than the bytes you waste. Don't sprinkle it on cold variables.

Since C++17 there's a more elegant option: put per-thread data in `thread_local`, and the compiler naturally gives each thread its own instance, eliminating the sharing at the root. But `thread_local` has its own pitfalls (initialization cost, interaction with thread pools); weigh the trade-offs per scenario.

> Boundary reminder: false sharing is a **performance** problem and belongs to vol6; "how to write correct multithreaded synchronization, and the memory-ordering semantics of atomic operations" belongs to vol5. This article covers only "the performance cost of cachelines under multicore".

Compress this article into one sentence: multiple cores frequently writing different variables on the same cacheline triggers MESI invalidate round trips and effectively serializes them; measured on this machine the gap is an order of magnitude (about 18× in a single run, with the multiplier drifting between 15×–48× across runs — "an order of magnitude" is the stable conclusion); the fix is `alignas(64)` giving each frequently-written variable its own cacheline, or `thread_local` for per-thread data; to hunt false sharing use `perf c2c` (watch the HITM counts) — TSan/ASan won't catch it, because it isn't a correctness problem. The **depth** of coherence protocols (the MESI state machine, MOESI/MESIF variants, cache-to-cache transfer) belongs to an architecture course; vol6 covers only the layer "the cacheline is the unit of coherence", which is enough to guide code changes.

The next article covers the other multicore amplifier, NUMA, and how to use the scalability curve to judge whether your parallel program "scales well".

## References

- CppCoreGuidelines CP.3 *false sharing* — the definition of and fix for false sharing
- Bakhvalov, *Performance Analysis and Tuning on Modern CPUs*, §11.7 *Detecting Coherence Issues* (written by Mark Dawson)
- Drepper, *What Every Programmer Should Know About Memory* — MESI and multicore cache coherence
- perf c2c documentation (KDAB has a detailed tutorial)
- This article's measurement code: `code/volumn_codes/vol6-performance/ch05/false_sharing.cpp`
