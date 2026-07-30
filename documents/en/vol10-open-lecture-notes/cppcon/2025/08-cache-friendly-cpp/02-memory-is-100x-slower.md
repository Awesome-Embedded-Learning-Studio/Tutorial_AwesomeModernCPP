---
title: "Memory Is 100x Slower — For Real: The Cache Hierarchy and Cache Lines"
description: "CppCon 2025 notes — Cache-Friendly, part 2. On this machine, sequential access over an 8 MB array measures 0.27 ns/elem and random access 4.55 ns/elem — a 17x gap. We work through the cache hierarchy, cache lines, the principle of locality, and cache eviction."
chapter: 8
order: 2
conference: cppcon
conference_year: 2025
talk_title: 'Cache-Friendly C++'
speaker: Jonathan Müller
cpp_standard: [20]
difficulty: intermediate
platform: host
reading_time_minutes: 13
tags:
  - cpp-modern
  - host
  - intermediate
  - 优化
related:
  - "Complexity Isn't Everything: How O(1) Lost to O(n)"
  - "Data Type Is a Cache Variable Too: Smaller Isn't Always Faster"
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/08-cache-friendly-cpp/02-memory-is-100x-slower.md
  source_hash: c883b92da3f978b8b9ae760ac284aa789bf128728e1870a4e809de237c1cae0c
  translated_at: '2026-07-29T00:33:58.679864+00:00'
  engine: anthropic
  token_count: 2941
---

# Memory Is 100x Slower — For Real: The Cache Hierarchy and Cache Lines

[Last time](01-complexity-is-not-everything.md) we saw how cache locality lets O(n) `vector` crush O(1) `unordered_set` on small data, but I only used the conclusion and skipped the mechanism. This post fills that in: why memory access is so slow, how the cache rescues us, what unit it moves data in, and how the latency differs across levels. Once this is clear, you can design cache-friendly code on purpose.

## First Build an Order-of-Magnitude Intuition: Compute Is ~100x Faster Than Memory

Let me start with a comparison that surprises a lot of people. A modern CPU's compute throughput, in round numbers, is on the order of tens of thousands of GB per second (counting floating-point throughput). The bandwidth between the CPU and main memory (DRAM) is, at most, a few dozen to a bit over a hundred GB per second. That's about two orders of magnitude apart. Put another way, it's even more blunt: an L1 access is roughly 1 nanosecond, a main-memory access roughly 70 to 100 nanoseconds — **a 100x difference**.

What does 100x mean? If your code fetched every value it touched straight from main memory, then no matter how fast your CPU is, real-world performance gets dragged down to about the one-percent level. During those 100 nanoseconds of waiting for data, the CPU does literally nothing — it just sits there. That is exactly why caching is a life-or-death matter.

## How the Cache Rescues You: The Principle of Locality

The good news is that real programs don't access memory completely at random. Two patterns show up everywhere, and together they're called the **principle of locality**:

- **Temporal locality**: data you just touched is very likely to be touched again soon. Loop counters and accumulators get read and written several times in a single beat.
- **Spatial locality**: once you access an address, the addresses right next to it are very likely to be accessed soon. Walking through an array is the textbook example.

Hardware engineers exploit these two patterns by placing a few layers of smaller but much faster storage right inside the CPU core, next to the execution units — that's the cache (L1/L2/L3). Data you've touched keeps a copy in the cache, so the next time you need it, you pull it straight from there and never touch slow main memory.

## The Cache Hierarchy: Bigger Means Slower

The hierarchy on this machine's AMD Ryzen 7 9700X (Zen 5) looks like this:

```text
$ lscpu | grep -iE "L1d|L2|L3 cache"
L1d cache:  384 KiB (8 instances)   ← 48 KiB per core, private
L1i cache:  256 KiB (8 instances)
L2 cache:   8 MiB   (8 instances)   ← 1 MiB per core, private
L3 cache:   32 MiB  (1 instance)    ← all 8 cores, shared
```

L1 is the fastest and smallest, private to each core; L2 is a bit bigger and slower, also per-core private; L3 is the largest and slowest (relative to the first two) and shared across all cores. Below that is main memory — tens of GB, but 100-nanosecond access.

Typical latency at each level (rough numbers for a desktop x86; varies by generation):

| Level | Capacity (this machine) | Typical latency | ~Clock cycles |
|------|-----------|----------|----------------|
| L1d | 48 KiB/core | ~1 ns | 4 |
| L2 | 1 MiB/core | ~4 ns | 14 |
| L3 | 32 MiB | ~12 ns | 40-50 |
| Main memory | tens of GB | ~70-100 ns | 200+ |

This table is the anchor for everything that follows. Notice that from L1 to main memory, latency climbs nearly 100x while capacity climbs a few thousand times — that's the fundamental tradeoff of caching: **the faster a level is, the smaller it is, and you simply can't fit all your data into the fastest L1**.

Why not just build a bigger L1? Physics won't let you. The bigger the SRAM array, the longer the signal has to travel and the more complex address decoding gets, so access gets slower. If you blew L1 up to several MB, its latency would climb from ~1 ns to several nanoseconds — a regression for code that would otherwise hit a small cache just fine. So we have to split into layers: check L1 first, fall back to L2, then L3, and only hit main memory as a last resort.

## The Cache Line: Data Moves in 64-Byte Chunks

Here is the single most important fact for understanding cache behavior: the CPU does not move data one byte at a time, or even one `int` at a time. It moves whole **cache lines**, and a cache line is usually 64 bytes.

When you read `data[5]`, a single `int` (4 bytes), the CPU doesn't just grab those 4 bytes. It notices the 64-byte cache line holding those 4 bytes isn't in the cache, so it pulls the **entire 64-byte line** from main memory and drops it into L1. Now when you read `data[6]`, `data[7]`, and so on, they all land on a line that's already cached — instant hits, zero cost.

That's the physical reason spatial locality pays off so heavily: touching one element effectively drags the 16 surrounding `int`s (64 bytes / 4 bytes) into the cache for free. When you walk an array sequentially, you pay the main-memory access penalty only once every 16 elements — the other 15 are pure profit.

Flip it around and you get the real reason random access is slow: you grab one element here, one there, and each grab might be a brand-new cache line, paying the full main-memory latency every time.

## The Load-Bearing Experiment: Sequential vs. Random, a 17x Gap

Conclusions are boring, so let's run one. Prepare an `int` array of 8 million elements (~32 MB, well past L3), then sum its elements once with **sequential indices** and once with **shuffled random indices**:

```cpp
constexpr int N = 8'000'000;  // 32MB, exceeds L3
std::vector<int> data(N);
std::iota(data.begin(), data.end(), 0);

std::vector<int> seq_idx(N);
std::iota(seq_idx.begin(), seq_idx.end(), 0);       // sequential
std::vector<int> rand_idx = seq_idx;
std::shuffle(rand_idx.begin(), rand_idx.end(), rng); // randomly shuffled

// Iterate over data using seq_idx and rand_idx respectively, accumulate sum
```

Results on this machine:

```text
Sequential access over 8000000 elements: 2.1 ms (0.27 ns/elem)
Random access over 8000000 elements: 36.4 ms (4.55 ns/elem)
```

**Random access is 17x slower than sequential access.** Same data, same arithmetic (both add up 8 million numbers), and the only difference is access order. In sequential access the hardware prefetcher sees you reading in increasing address order and pulls the next cache lines into L1 ahead of you, so you almost always hit. In random access the prefetcher can't find a pattern, every jump might land on a fresh cache line, and you get a flood of cache misses — every element averages 4.55 ns (sitting somewhere between L3 and main-memory latency).

17x. That's the power of the cache line. Whether you traverse a 2D array by row or by column, whether you use a contiguous `vector` or a node-scattered `list` — once the dataset is even slightly large, the gap is on this order of magnitude.

## Cache Eviction and Cache Thrashing

Cache capacity is finite and can't hold everything, which brings us to the other side of cache behavior: **eviction**. When the cache is full and you need to load new data, the hardware has to pick an old entry to kick out to make room. L1 is only 48 KiB — it doesn't hold much — so eviction is happening practically all the time.

Eviction itself isn't scary; what's scary is **kicking the wrong thing**. If the entry the hardware evicts happens to be the one you're about to use next, you have to go back to main memory to fetch it again. Worse, the new data that comes back in may push out yet another entry you're about to use, so you keep kicking and keep missing, and the hit rate trends toward zero. This has a name: **cache thrashing**.

Once you're in a thrashing state, you might as well not have a cache — your program regresses to the 100x-slow "every access goes to main memory" regime. I once wrote a program that walked a large array at a fixed stride; no amount of algorithmic tuning helped. Looking closer, the cache-miss rate was absurdly high — my access stride conflicted exactly with how addresses mapped to cache sets, so every load evicted the data I needed next. I changed the data layout without touching the algorithm, and performance jumped by more than 10x. That's the weight behind the phrase "the memory wall."

Once eviction makes sense, you can explain a common observation: why a benchmark curve of "working-set size vs. access latency" slides down in steps rather than dropping off a cliff. When the working set fits in L1, everything hits and latency is lowest; once it exceeds L1 but stays within L2, L1 starts missing and latency creeps up; past L2 into L3, there's another source of misses and latency climbs again; past L3, a large fraction of accesses hit main memory and latency spikes to the top. Each time a cache level gets swamped, a new miss source appears — so the curve is a staircase of downhill slopes, not a single sharp step.

## What About Writes: Write-Through vs. Write-Back

Everything above was about reads. On a write to an address already in the cache, there are two hardware models under the hood.

**Write-through**: every write updates both the cache copy and the original value in main memory. Simple, and consistency comes for free, but every write pays an extra round trip to main memory, and write performance tanks.

**Write-back**: on a write, you update only the cache copy and leave main memory alone. Only when that cache line is about to be evicted do you write the dirty (modified) line back to main memory.

Modern CPUs almost all use write-back — if you write the same cache line 8 times in a row, write-through would make 8 round trips to main memory, while write-back makes just 1 at eviction time. The advantage is overwhelming. But write-back creates cache-coherence problems under multithreading (thread A updates a value in its own cache while main memory still holds the old value, and thread B reads stale data straight from main memory). Hardware handles this with protocols like MESI, but the cost is real. That's a topic for a later post on concurrency and cache coherence; here we'll keep to the single-threaded intuition.

## The Boundary of This Layer

That covers the mechanism: why the cache exists (compute is ~100x faster than memory), how it rescues you (the principle of locality), what unit it moves data in (64-byte cache lines), how much the levels differ (L1 to main memory is ~100x), and why it thrashes (eviction kicks the wrong entry). With this intuition in hand, go back and re-read the `vector` vs. `unordered_set` piece — every phenomenon there now has a slot to click into.

But cache-friendliness has one more, less obvious lever hiding in your choice of data type. Swapping `int` for `uint8_t` looks like it saves 4x the cache space, so it should be faster, right? Next time we'll run the experiment and see whether that's actually true.

[Next: Data type is a cache variable too →](03-data-types-and-cache.md)
