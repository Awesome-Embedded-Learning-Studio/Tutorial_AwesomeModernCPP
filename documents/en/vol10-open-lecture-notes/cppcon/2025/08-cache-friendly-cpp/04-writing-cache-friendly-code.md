---
title: "Getting Cache-Friendly Code Right: Layout, Alignment, and Decisions"
description: "CppCon 2025 notes — the Cache-Friendly finale. Folds the previous parts' mechanisms into an engineering method: contiguity first, hot/cold splitting, AoS/SoA (with an honest local benchmark), access patterns, and a decision checklist"
chapter: 8
order: 4
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
  - "Data Types Are Part of the Cache Equation Too: Smaller Isn't Always Faster"
  - "A Faster Microbenchmark Is Not a Faster Program"
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/08-cache-friendly-cpp/04-writing-cache-friendly-code.md
  source_hash: dbd87b171c88e2561158899cf697a626d8e6218c69c4afadf2bffa83a9ed89c3
  translated_at: '2026-09-26T16:49:55+00:00'
  engine: anthropic
  token_count: 4800
---

# Getting Cache-Friendly Code Right: Layout, Alignment, and Decisions

Across the [first three parts](01-complexity-is-not-everything.md) we worked through the cache mechanics: complexity isn't everything, memory is roughly 100x slower than compute, data moves in whole 64-byte cache lines, and shrinking types isn't automatically faster. This part folds those mechanics into an engineering method you can actually apply — given a concrete scenario, how do you make the code cache-friendly, step by step? At the end we'll also walk through a pit we hit in our own benchmarking: the classic AoS/SoA optimization that, on this machine, simply refuses to show a difference.

## Rule 1: Lay Out Data Contiguously, Prefer It Over Scattered Nodes

This is the most ironclad of all the cache-friendly rules, and the one the [first part's](01-complexity-is-not-everything.md) experiment proved directly. A `std::vector`'s data is one contiguous block of memory; iterating it uses cache lines and the hardware prefetcher perfectly. Node-based containers — `std::list`, `std::set`, `std::map` — allocate each node separately, scattered all over the heap, and every node hop is a potential cache miss.

Recall the table from part one: at N=262144, binary search over a sorted `vector` (8090 us) beats an equally O(log n) `std::set` (19903 us) by more than 2x. Identical complexity — the gap comes purely from contiguous versus scattered. So as long as you don't need the specific semantics of a node-based container (frequent insertion and erasure in the middle, iterator stability), `std::vector` is almost always the more cache-friendly choice.

Pushed to its limit, this rule means even your custom data structures should live in contiguous storage where possible. Storing ten thousand records? Use `std::vector<Record>`, not `std::vector<std::unique_ptr<Record>>` — the latter `new`s every `Record` separately on the heap, every access chases a pointer, and cache performance ends up as bad as `std::list`.

## Rule 2: Split Hot From Cold — Pull the Frequently Accessed Fields Out

Within one struct, fields are often accessed at wildly different frequencies. Take a `Player` struct:

```cpp
struct Player {
    std::string name;        // cold: only used when displaying the name
    int level;               // hot: every frame's logic reads it
    std::vector<Item> inventory;  // cold: only used when the inventory is opened
    Vector3 position;        // hot: physics and rendering read it every frame
    int health;              // hot: combat logic reads it frequently
    // ... plus a pile of other cold fields
};
```

If you jam all of these fields into one `Player` and store every player in a `std::vector<Player>`, the trouble starts: `Player` is big (hundreds of bytes), so a single cache line holds only a fraction of a player. Each frame the game touches only `position` and `health`, yet every read of them pulls in the entire cache line, and most of it is `name` and `inventory` that this frame will never use — cache-line utilization is rock bottom.

The fix is called **hot/cold splitting**: pull the frequently accessed hot fields out into a compact struct of their own; leave the cold fields in another struct, linked back by index or pointer.

```cpp
struct PlayerHot {
    Vector3 position;
    int level;
    int health;
};  // compact — several players fit in one cache line

struct PlayerCold {
    std::string name;
    std::vector<Item> inventory;
    // ... other cold fields
    std::uint32_t hot_index;  // links back to PlayerHot
};

std::vector<PlayerHot> hot_data;    // the main loop iterates this one — cache-friendly
std::vector<PlayerCold> cold_data;  // touched only when needed
```

When the main loop iterates `hot_data`, each cache line packs the hot fields of several players, and cache misses drop sharply. The cold fields sit untouched; only occasional operations — a player opening their inventory — go look at `cold_data`, where a little slowness doesn't matter. This is a very common design technique in game engines, database kernels, and HFT systems.

## Rule 3: AoS vs. SoA (and One Honest Measurement)

Hot/cold splitting pushed to its extreme becomes the classic **AoS vs. SoA** debate. AoS (Array of Structs) is `std::vector<Particle>`, each particle's x/y/z sitting together; SoA (Struct of Arrays) is three separate arrays `x[]`, `y[]`, `z[]`. When all you need is some computation over every particle's x (updating the x coordinate in a physics sim, say), SoA walks only the `x[]` array and every cache line is useful x; AoS drags the useless y/z into the cache line on every x read, for a utilization of just 1/3.

In theory SoA should be much faster. So we ran an experiment: 4 million particles, accumulating only the x field, AoS (array 45.8 MB, 6% cache-line utilization) against SoA (x array 15.3 MB, 100% utilization). The result:

```text
Summing only the x field, N=4000000
  AoS: 9875.1 us (array 45.8 MB, useful data per cache line 4/64=6%)
  SoA: 9902.4 us (x array 15.3 MB, useful data per cache line 16/16=100%)
  SoA / AoS = 1.00x
```

**No difference.** We pushed AoS's useless fields up to seven (struct 32 bytes, array 122 MB, cache-line utilization down to 12.5%) — still 1.01x, essentially the same.

Why didn't the theoretical advantage materialize? Because on this Zen 5 machine, several things conspired to mask this simple accumulation: the compiler's auto-vectorizer rewrites the loop to run very fast, the hardware prefetcher pulls upcoming data in ahead of time, and the `volatile` accumulation writes to memory every iteration (the writes become the bottleneck and drown out the read-side cache difference). With all of that stacked up, the cache lines AoS wastes get quietly refilled by the prefetcher.

::: warning A theoretical optimization may not hold on your machine
This is a far more important lesson than "SoA is faster." AoS/SoA's theoretical advantage is real — in plenty of real projects (game engines, physics simulations), switching to SoA does deliver multiplicative wins. But those scenarios usually have more complex access patterns, more fields, and code that isn't this easy to vectorize. In this simple accumulation, the advantage got eaten by a modern CPU's prefetching and vectorization. The conclusion fits in one sentence: **any "theoretically faster" optimization is only a hypothesis until you've measured it on your own target scenario**. That discipline is exactly the correlation point the [final part of the microbenchmarks talk](../07-microbenchmarks-that-lie/05-correlation-and-discipline.md) made — don't take a theoretical ranking as a code decision.
:::

So when is switching to SoA actually worth it? When these conditions hold: the access pattern touches only a small subset of the struct's fields, the data is large enough to blow the cache, the computation is dense enough that loads are genuinely the bottleneck, or you're committed to hand-written SIMD vectorization (SoA is a natural fit for SIMD). Otherwise AoS is simpler and more readable — use it first, and change course only when a profiler actually flags this block as the bottleneck.

## Rule 4: Mind the Access Order — Sequential Beats Random by Far

This one the [second part's](02-memory-is-100x-slower.md) experiment proved directly: the same 8 MB array, sequential access at 0.27 ns/elem, random access at 4.55 ns/elem — a 17x gap. Landed in code, it means:

- **Iterate a 2D array by row, not by column.** `for (i) for (j) a[i][j]` is contiguous (row-major storage); the reversed `for (j) for (i) a[i][j]` jumps a whole row each step, and cache misses fly everywhere.
- **In nested loops, the memory the innermost loop touches must be contiguous.** If you're writing a matrix multiply or image processing, put the contiguous-access dimension in the innermost loop.
- **Avoid "hopping" over an array.** If an algorithm's index goes something like `i = (i * 7) % n`, the prefetcher can't latch onto the pattern, and it's effectively random access.

## Rule 5: Watch Out for False Sharing Under Multithreading

One last pitfall, from multithreaded territory — this part won't benchmark it, only explain the mechanism. If two of your threads write different variables, but those two variables **happen to land on the same cache line** (within 64 bytes), trouble arrives: thread A modifies its variable and the whole cache line goes dirty in A's private L1; to keep coherence, the hardware must invalidate that line in the core running thread B, and the next time B reads its own variable it has to pull the line back over from A. Two threads are writing clearly different data, yet they keep kicking each other's cache lines back and forth, and performance collapses. This is **false sharing**.

The fix is **alignment padding**: give each frequently written shared variable its own full cache line with `alignas(64)`, guaranteeing that different threads' hot variables never land on the same line.

```cpp
struct alignas(64) PaddedCounter {
    std::atomic<std::uint64_t> value{0};
    // alignas(64) makes this struct fill an entire cache line
    // counters of different threads stay out of each other's way
};
```

This is standard practice in high-performance concurrent code. The full story of multithreaded cache coherence (the MESI protocol and friends) is left for when we cover concurrency; for now just remember this: false sharing is a real performance killer, and when multiple threads write shared data, alignment padding is the default thing to do.

## A Cache-Friendly Decision Checklist

Folding the rules above into a checklist to run through when facing a concrete scenario:

1. **Pick the contiguous data structure.** Use `std::vector` over a node-based container unless the node container's specific semantics are genuinely required.
2. **When the struct is big enough, do hot/cold splitting.** Pull the high-frequency fields out and store them compactly.
3. **Pick the right data type.** As [part three](03-data-types-and-cache.md) covered: shrink storage-heavy fields (enums especially) without hesitation, don't blindly shrink arithmetic-heavy fields, and let measurement decide.
4. **Iterate in memory-contiguous order.** The inner loop touches contiguous memory — no hopping around.
5. **When multiple threads write shared data, alignment-pad against false sharing.**
6. **The most important one: measure after every change.** All the optimizations above are only "might help" — whether one actually works, and by how much, is decided by the profiler and the benchmark.

All of this sounds like common sense, but every rule only truly sticks after you've been bitten by it. The biggest value of Jonathan's talk isn't teaching you any single trick — it's planting a reflex, so that every line of memory-accessing code you write flashes one question through your head: "For this access pattern, what's the cache-line utilization?" Build that intuition, and you're the one writing faster code than most people.

[Back to the index](index.md) · [Companion piece: why 99% of microbenchmarks lie](../07-microbenchmarks-that-lie/index.md)
