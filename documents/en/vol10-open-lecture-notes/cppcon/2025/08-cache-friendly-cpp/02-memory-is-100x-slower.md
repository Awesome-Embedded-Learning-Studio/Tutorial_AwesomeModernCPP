---
title: "Memory Access Really Is 100x Slower: The Cache Hierarchy and Cache Lines"
description: "CppCon 2025 notes — Cache-Friendly, part 2. Measured on this machine, sequential access over an 8M-element array runs at 0.27 ns/elem and random access at 4.55 ns/elem, a 17x gap. Covers the cache hierarchy, cache lines, the principle of locality, and cache eviction"
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
  - "Complexity Is Not Everything: How O(1) Lost to O(n)"
  - "Data Types Are Part of the Cache Equation Too: Smaller Isn't Always Faster"
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/08-cache-friendly-cpp/02-memory-is-100x-slower.md
  source_hash: c883b92da3f978b8b9ae760ac284aa789bf128728e1870a4e809de237c1cae0c
  translated_at: '2026-09-26T16:38:24+00:00'
  engine: anthropic
  token_count: 1924
  notes: '原文一处数字表述按正文修正译出：中文源 description「本机实测 8MB 数组」原文疑为「800 万元素（约 32 MB）数组」——正文承重实验为 800 万元素（约 32 MB，远超 L3）的 int 数组，与「8MB」矛盾，故英文 description 作 "an 8M-element array"。'
---

# Memory Access Really Is 100x Slower: The Cache Hierarchy and Cache Lines

[Last time](01-complexity-is-not-everything.md) we saw how cache locality let the O(n) `vector` crush the O(1) `unordered_set` on small data volumes — but we only took the conclusion on faith and skipped the mechanism. This piece fills the mechanism in: why memory access is so slow, how the cache rescues the situation, what unit it moves data in, and how much the cache levels differ. Only with all that clear can you design cache-friendly code on purpose.

## First, an Order-of-Magnitude Intuition: Compute Is 100x Faster Than Memory

Let me open with a comparison that surprises a lot of people. A contemporary CPU's compute capability, in round numbers, is on the order of tens of thousands of GB of data per second (counting floating-point throughput), while the bandwidth between it and main memory (DRAM) is only a few dozen to a bit over a hundred GB per second. That is about two orders of magnitude apart. Another angle is even more direct: an L1 cache access takes about 1 nanosecond, and a main-memory access about 70 to 100 nanoseconds — **a 100x difference**.

What does 100x mean? If your code honestly went to main memory for every single value it needs, then no matter how fast your CPU is, real-world performance would be dragged down to the one-percent level. During those 100 nanoseconds of waiting for data, the CPU gets nothing done — it just sits there idle. That is why caching is a matter of life and death.

## How the Cache Rescues You: The Principle of Locality

Fortunately, memory access in real programs is not completely random. Program execution has two universal characteristics, and together they are called the **principle of locality**:

- **Temporal locality**: data you just accessed is very likely to be needed again right away. The counter and the accumulator in a loop get read and written several times within a single beat.
- **Spatial locality**: once you access one address, the addresses right next to it are very likely to be accessed soon too. Walking through an array is the textbook example of spatial locality.

Hardware engineers exploit these two patterns by placing a few layers of smaller but extremely fast storage inside the CPU core, right next to the compute units — that is the cache (L1/L2/L3). Data you have accessed keeps a copy in the cache, so the next time you need it, it comes straight from the cache and you never touch the sluggish main memory.

## The Cache Hierarchy: Ever Bigger, Ever Slower

The cache hierarchy on this machine's AMD Ryzen 7 9700X (Zen 5) looks like this:

```text
$ lscpu | grep -iE "L1d|L2|L3 cache"
L1d cache:  384 KiB (8 instances)   ← 48 KiB per core, private
L1i cache:  256 KiB (8 instances)
L2 cache:   8 MiB   (8 instances)   ← 1 MiB per core, private
L3 cache:   32 MiB  (1 instance)    ← shared by all 8 cores
```

L1 is the fastest and the smallest, private to each core; L2 is a bit bigger and a bit slower, also private per core; L3 is the largest and the slowest (relative to the first two levels), shared by all cores. Below that is main memory — tens of GB, but a 100-nanosecond access.

Typical access latency per level (rough magnitudes for desktop-class x86; they vary across generations):

| Level | Capacity (this machine) | Typical latency | Approx. clock cycles |
|------|-----------|----------|----------------|
| L1d | 48 KiB/core | ~1 ns | 4 |
| L2 | 1 MiB/core | ~4 ns | 14 |
| L3 | 32 MiB | ~12 ns | 40-50 |
| Main memory | tens of GB | ~70-100 ns | 200+ |

This table is the anchor for everything discussed below. Notice that from L1 to main memory, latency rises nearly 100x while capacity rises a few thousand times — that is the fundamental tradeoff of caching: **the faster a level is, the smaller it is, and you cannot stuff all your data into the fastest L1**.

Why not build a bigger L1? The laws of physics say no. The larger an SRAM array is, the longer the signal lines run and the more complex address decoding gets, and the slower access becomes. If L1 were grown to several MB, its access latency would climb from 1 nanosecond to several nanoseconds — a regression for code that would otherwise have hit the small cache just fine. So the only way out is layering: check L1 first; on a miss, check L2; then L3; and only touch main memory last.

## Cache Lines: Data Moves in Whole 64-Byte Chunks

This is the **single most critical** fact for understanding cache behavior: the CPU does not move data byte by byte, not even one `int` at a time — it moves whole **cache lines**, and one cache line is typically 64 bytes.

When you read `data[5]`, a single `int` (4 bytes), the CPU does not fetch just those 4 bytes. It notices that the entire 64-byte cache line containing those 4 bytes is not in the cache, so it pulls the **entire 64-byte line** in from main memory and drops it into L1 along the way. If you then read `data[6]`, `data[7]`, ... they all land on the line that is already in the cache — direct hits, at zero cost.

This is the physical reason spatial locality pays off so heavily: accessing one element effectively ushers the 16 `int`s around it (64 bytes / 4 bytes) into the cache for free. When you traverse an array sequentially, you pay the main-memory access cost only once every 16 elements; the other 15 are pure profit.

Flip it around, and this is also the root cause of slow random access: you grab one element here and another there, every grab may be a brand-new cache line, and every time you pay the full main-memory latency.

## The Load-Bearing Experiment: Sequential vs. Random, a 17x Gap

Conclusions alone are boring — let's run one. Prepare an `int` array of 8 million elements (~32 MB, far beyond L3), then accumulate its elements once through **sequential indices** and once through **shuffled random indices**:

```cpp
constexpr int N = 8'000'000;  // 32MB, far beyond L3
std::vector<int> data(N);
std::iota(data.begin(), data.end(), 0);

std::vector<int> seq_idx(N);
std::iota(seq_idx.begin(), seq_idx.end(), 0);       // sequential
std::vector<int> rand_idx = seq_idx;
std::shuffle(rand_idx.begin(), rand_idx.end(), rng); // randomly shuffled

// Iterate over data with seq_idx and rand_idx respectively, accumulating the sum
```

Results on this machine:

```text
顺序访问 8000000 元素: 2.1 ms (0.27 ns/elem)
随机访问 8000000 元素: 36.4 ms (4.55 ns/elem)
```

**Random access came out 17x slower than sequential access.** Same data, same amount of computation (both add up 8 million numbers), and the only difference is access order. Under sequential access, the hardware prefetcher notices you reading in increasing address order and pulls later cache lines into L1 ahead of you, so you hit almost every time. Under random access, the prefetcher can't catch any pattern; every jump may land on a new cache line, cache misses pour in, and each element costs an average of 4.55 nanoseconds — close to the latency between L3 and main memory.

17x. That is the power of the cache line. When you write code — traversing a 2D array by row or by column, using a contiguous `vector` or a node-scattered `list` — once the data volume grows a bit, the gap is of this order of magnitude.

## Cache Eviction and Cache Thrashing

Cache capacity is limited and cannot hold all the data, which brings in the other side of cache behavior: **eviction**. When the cache is full and you need to load new data, the hardware has to pick an old entry to kick out to make room. L1 is a mere 48 KiB and can't hold much, so eviction is happening practically all the time.

Eviction itself is not scary; what is scary is **kicking out the wrong entry**. If the entry the hardware evicts happens to be exactly the one you are about to use next, you have to go back to main memory for it once more. Worse, the new data fetched in this time may push out another entry that is just about to be used, so entries get kicked over and over, misses repeat, and the hit rate trends toward zero. This phenomenon has a proper name: **cache thrashing**.

Once a program falls into thrashing, having a cache performs the same as having none — the program regresses to the 100x-slow "every access goes to main memory" regime. I once wrote a program that walked a large array at some fixed stride; no amount of algorithm tuning helped. When I finally looked, the cache-miss rate was absurdly high — the access stride conflicted exactly with how addresses map to cache sets, so every load evicted the data the next step needed. I changed the data layout, without touching a single line of the algorithm, and performance jumped more than tenfold. That is the weight the phrase "the memory wall" carries.

With eviction understood, you can explain a common observation: why the "working-set size vs. access latency" curve in benchmarks slides down in a staircase rather than falling off a cliff. When the working set is smaller than L1, everything hits and latency is at its lowest; once it exceeds L1 but still fits within L2, L1 starts missing and latency rises slowly; past L2 into L3 territory, there is one more source of misses and latency keeps climbing; past L3, a large fraction of accesses strike main memory and latency shoots to the top. Every cache level that gets swamped adds another source of misses, so the curve is a downhill slope in segments, not one clean-cut step.

## What About Writes: Write-Through vs. Write-Back

Everything above was about reads. On a write to an address that is already in the cache, there are two hardware models underneath.

**Write-through**: every write updates both the cache copy and the original value in main memory. Simple, and consistency comes guaranteed — but every write makes an extra trip to main memory, and write performance gets dragged down.

**Write-back**: on a write, only the copy in the cache is updated, and main memory is left alone. Only when that cache line is about to be evicted is the dirty data (the modified cache line) written back to main memory.

Modern CPUs almost all use write-back — if you write the same cache line 8 times in a row, write-through makes 8 trips to main memory while write-back makes only 1 at eviction time, an overwhelming advantage. But write-back raises cache-coherence problems under multithreading (thread A changes a value in its own cache while main memory still holds the old one, and thread B reads stale data from main memory). The hardware handles this with protocols like MESI, and the cost is real. That topic is saved for a later piece on concurrency and cache coherence; this one keeps to the single-threaded intuition.

## The Boundary of This Layer

At this point the cache mechanism is fully told: why it exists (compute is 100x faster than memory), how it rescues us (the principle of locality), what unit it moves data in (64-byte cache lines), how much the levels differ (L1 to main memory is 100x), and why it thrashes (eviction kicks out the wrong entry). With this intuition in hand, go back to the previous piece's `vector` vs. `unordered_set` and every phenomenon there lines up.

But there is one less obvious route to cache friendliness, hidden in your choice of data type. Swapping `int` for `uint8_t` looks like it saves 4x the cache space, so it should be faster, right? Next time we run that experiment and see whether it is actually true.

[Next: Data Types Are Part of the Cache Equation Too: Smaller Isn't Always Faster →](03-data-types-and-cache.md)
