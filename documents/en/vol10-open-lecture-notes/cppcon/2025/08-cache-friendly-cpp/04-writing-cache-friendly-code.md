---
title: "Writing Cache-Friendly Code: Layout, Alignment, and Decisions"
description: "CppCon 2025 notes — the Cache-Friendly finale. Collapses the previous parts' mechanisms into an engineering method: continuity first, hot/cold split, AoS/SoA (with an honest local benchmark), access patterns, and a decision checklist"
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
  - "Data Type Is Also a Cache Variable: Shrinking Types Isn't Always Faster"
  - "A Faster Microbenchmark Doesn't Mean a Faster Program"
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/08-cache-friendly-cpp/04-writing-cache-friendly-code.md
  source_hash: 9cb280be5e89071e613a4693b8d588900ad7dde725365981029ebc6ff1ef811e
  translated_at: '2026-07-29T00:33:58.679864+00:00'
  engine: anthropic
  token_count: 2918
---

# Writing Cache-Friendly Code: Layout, Alignment, and Decisions

In the [first three parts](01-complexity-is-not-everything.md) we worked through the mechanisms: complexity isn't everything, memory is ~100x slower than compute, data moves in 64-byte cache lines, and shrinking types isn't always faster. This part folds those mechanisms into an engineering method you can actually apply — given a concrete scenario, how do you step by step make the code cache-friendly? At the end I'll also walk through a pit I hit in my own benchmarking: the classic AoS/SoA optimization that, on my machine, simply refuses to show a difference.

## Rule one: lay data out contiguously, prefer it over scattered nodes

This is the hardest of all the cache-friendly rules, and it's the one the [first part's](01-complexity-is-not-everything.md) experiment proved directly. A `std::vector` is one contiguous block of memory; iterating it uses cache lines and hardware prefetch perfectly. Node-based containers like `std::list`, `std::set`, and `std::map` allocate each node separately, scattered across the heap, and every node hop is a potential cache miss.

Recall the table from part one: at N=262144, a binary search over a sorted `vector` (8090 us) beats an equally O(log n) `std::set` (19903 us) by more than 2x. Identical complexity — the gap comes purely from contiguous vs. scattered. So unless you genuinely need the specific semantics of a node-based container (frequent mid-container insert/erase, iterator stability), `std::vector` is almost always the more cache-friendly choice.

Pushed to its limit, this rule means even your custom data structures should be contiguous where possible. Storing ten thousand records? Use `std::vector<Record>`, not `std::vector<std::unique_ptr<Record>>` — the latter `new`s each `Record` separately on the heap, every access chases a pointer, and cache performance is as bad as `std::list`.

## Rule two: split hot and cold, pull frequently accessed fields out

Within a struct, fields often get accessed at wildly different frequencies. Take a `Player` struct:

```cpp
struct Player {
    std::string name;        // cold: only used when showing name
    int level;               // hot: every frame's logic checks it
    std::vector<Item> inventory;  // cold: only used when opening inventory
    Vector3 position;        // hot: physics and rendering read every frame
    int health;              // hot: combat logic reads frequently
    // ... plus a bunch of other cold fields
};
```

If you jam all these fields into one `Player` and store all players in `std::vector<Player>`, here's the problem: `Player` is large (hundreds of bytes), so a single cache line holds only a fraction of a player. Each frame the game only touches `position` and `health`, but every time it reads them the whole cache line gets pulled in, and most of that line is `name` and `inventory` that won't be used this frame — the cache-line utilization is terrible.

The fix is called **hot/cold splitting**: pull the high-frequency hot fields out into a compact struct of their own; keep the cold fields in another struct, linked by an index or pointer.

```cpp
struct PlayerHot {
    Vector3 position;
    int level;
    int health;
};  // compact, multiple players per cache line

struct PlayerCold {
    std::string name;
    std::vector<Item> inventory;
    // ... other cold fields
    std::uint32_t hot_index;  // links to PlayerHot
};

std::vector<PlayerHot> hot_data;    // main loop iterates this, cache-friendly
std::vector<PlayerCold> cold_data;  // accessed only when needed
```

When the main loop iterates `hot_data`, each cache line packs the hot fields of several players, and cache misses drop sharply. The cold fields sit untouched, and only the occasional operation — a player opening their inventory — goes to look at `cold_data`, where a bit of slowness is fine. This is a very common design idiom in game engines, database kernels, and HFT systems.

## Rule three: AoS vs. SoA (and an honest benchmark)

Pushed to its limit, hot/cold splitting becomes the classic **AoS vs. SoA** debate. AoS (Array of Structs) is `std::vector<Particle>`, where each particle's x/y/z sit together; SoA (Struct of Arrays) is three separate arrays `x[]`, `y[]`, `z[]`. When you only need to do some computation over every particle's x (say, updating the x coordinate in a physics sim), SoA iterates only the `x[]` array and every cache line is useful x data; AoS, reading each x, also drags the useless y/z into the cache line, for a utilization of only 1/3.

In theory SoA should be much faster. So I ran an experiment: 4 million particles, accumulating only the x field, AoS (array 45.8 MB, 6% cache-line utilization) vs. SoA (x array 15.3 MB, 100% utilization). Result:

```text
Summing only x field, N=4000000
  AoS: 9875.1 us (array 45.8 MB, useful bytes per cache line 4/64=6%)
  SoA: 9902.4 us (x array 15.3 MB, useful bytes per cache line 16/16=100%)
  SoA / AoS = 1.00x
```

**No difference.** I pushed AoS's useless fields up to 7 (struct 32 bytes, array 122 MB, cache-line utilization down to 12.5%), and it was still 1.01x — essentially the same.

Why didn't the theoretical advantage show up? Because on my Zen 5 machine, this simple accumulation is jointly masked by a few things: the compiler's auto-vectorizer rewrites the loop to run very fast, the hardware prefetcher pulls in upcoming data ahead of time, and the `volatile` accumulation writes to memory every iteration (the write becomes the bottleneck and drowns out the read-side cache difference). With all of that stacked together, the cache lines AoS wastes get quietly refilled by the prefetcher.

::: warning A theoretical optimization isn't necessarily true on your machine
This is a far more important lesson than "SoA is faster." The theoretical advantage of AoS/SoA is real — in many real projects (game engines, physics sims), switching to SoA does bring multiplicative gains. But those scenarios usually have more complex access patterns, more fields, and aren't this easy to vectorize. In my simple accumulation, the advantage gets eaten by modern CPU prefetch and vectorization. The conclusion is one sentence: **any "theoretically faster" optimization is only an assumption until you measure it on your own target scenario**. This discipline is exactly the correlation point made in the [last part of the microbenchmarks talk](../07-microbenchmarks-that-lie/05-correlation-and-discipline.md) — don't take a theoretical ranking as a code decision.
:::

So when is SoA actually worth switching to? When you meet these conditions: the access pattern touches only a small subset of the struct's fields, the data is large enough to blow the cache, the computation is dense enough that load is genuinely the bottleneck, or you've decided to hand-write SIMD vectorization (SoA is a natural fit for SIMD). Otherwise AoS is simpler and more readable — use it first, and only refactor when a profiler actually points at this block as a bottleneck.

## Rule four: mind the access order — sequential is far faster than random

This one the [second part's](02-memory-is-100x-slower.md) experiment proved directly: the same 8 MB array, sequential access 0.27 ns/elem, random access 4.55 ns/elem, a 17x gap. Translated into code, it means:

- **Iterate a 2D array by row, not by column.** `for (i) for (j) a[i][j]` is contiguous (row-major storage); the reverse `for (j) for (i) a[i][j]` jumps a whole row each step and cache misses fly.
- **In nested loops, the innermost access should hit contiguous memory.** If you're doing matrix multiply or image processing, put the contiguous-access dimension in the innermost loop.
- **Avoid "hopping" access over an array.** If an algorithm's index is something like `i = (i * 7) % n`, the prefetcher can't find a pattern, and it's effectively random access.

## Rule five: watch for false sharing under multithreading

Finally, a pitfall from the multithreaded world — this part won't benchmark it, only explain the mechanism. If two threads each write to a different variable, but those two variables **happen to land on the same cache line** (within 64 bytes), trouble follows: thread A dirties that whole cache line in its private L1; to maintain coherence, the hardware has to invalidate that line in the core running thread B, so the next time B reads its own variable it has to pull the line back over from A. Two threads are clearly writing different data, yet they keep kicking each other's cache lines back and forth, and performance collapses. This is **false sharing**.

The fix is **alignment padding**: pad a frequently-written shared variable with `alignas(64)` so it occupies a full cache line on its own, guaranteeing that different threads' hot variables don't land on the same line.

```cpp
struct alignas(64) PaddedCounter {
    std::atomic<std::uint64_t> value{0};
    // alignas(64) makes this struct fill a whole cache line
    // counters of different threads don't interfere
};
```

This is standard practice in high-performance concurrent code. The full story of multithreaded cache coherence (the MESI protocol and friends) I'll leave for when we cover concurrency; for now just remember: false sharing is a real performance killer, and when multiple threads write shared data, alignment padding is a default thing to do.

## A cache-friendly decision checklist

Folding the rules above into a mental checklist for a concrete scenario:

1. **Pick the contiguous data structure.** Use `std::vector` over a node-based container unless the node container's specific semantics are genuinely needed.
2. **When the struct is large, do hot/cold splitting.** Pull the high-frequency fields out and store them compactly.
3. **Pick the right data type.** As [part three](03-data-types-and-cache.md) covered, shrink storage-heavy fields (enums especially) without hesitation, but don't blindly shrink arithmetic-heavy fields — measure and decide.
4. **Iterate in memory-contiguous order.** The innermost loop should touch contiguous memory; don't hop around.
5. **When multiple threads write shared data, align-pad to prevent false sharing.**
6. **The most important rule: measure after you change.** Every optimization above is only "possibly effective." Whether it actually works, and by how much, is decided by the profiler and the benchmark.

This all sounds like common sense, but each rule only really sticks once you've been bitten by it. The biggest value of Jonathan's talk isn't teaching you any single trick — it's making it so that every time you write a line of memory-accessing code, a question flashes through your head subconsciously: "For this access pattern, what's the cache-line utilization?" Build that intuition, and you'll write faster code than most people.

[Back to the index](index.md) · [Sister piece: why 99% of microbenchmarks lie](../07-microbenchmarks-that-lie/index.md)
