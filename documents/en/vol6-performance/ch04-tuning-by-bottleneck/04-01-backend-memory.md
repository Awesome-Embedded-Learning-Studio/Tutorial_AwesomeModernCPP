---
chapter: 4
cpp_standard:
- 17
description: Backend Memory Bound is the single biggest lever in single-threaded performance. Using a measured particle system, this article works through three things — why contiguous + sequential is fast (a cache-friendly refresher), how AoS to SoA is nearly 10x faster in the "only some fields updated" scenario, and how struct alignment and padding affect cacheline utilization — plus when software prefetch helps
difficulty: advanced
order: 1
platform: host
prerequisites:
- Memory hierarchy and the latency ladder — why sequential access is 100x faster
- Cachelines and locality — the 64-byte minimum unit of transfer
- Attribution in practice — from a slow program to the bottleneck
reading_time_minutes: 7
related:
- Loop and compute optimization — code motion, unrolling, and multiple accumulators
- SIMD and vectorization — auto-vectorization conditions, intrinsics, and CPU dispatch
tags:
- host
- cpp-modern
- advanced
- 优化
- 内存管理
title: "Backend memory bottlenecks: cache-friendly, AoS/SoA, and prefetch"
translation:
  source: documents/vol6-performance/ch04-tuning-by-bottleneck/04-01-backend-memory.md
  source_hash: 606e5c8189143c72cadc8413b47018a834c38ac97ed7933a1d755581f2eca86b
  translated_at: '2026-07-06T00:00:00+00:00'
  engine: manual
  token_count: 2900
---
# Backend memory bottlenecks: cache-friendly, AoS/SoA, and prefetch

## The single biggest lever in single-threaded performance

ch03's attribution methods tell us that the overwhelming majority of C++ programs land their bottleneck in the **Backend Memory Bound** bucket — the execution units are waiting on data. That's actually good news, because "waiting on data" is the **highest-leverage** category in single-threaded optimization: a change in data layout often buys you a few-fold, even ten-fold improvement, whereas changing algorithms or adding SIMD usually only gets you tens of percent.

This article treats Backend Memory specifically. We drop the memory-hierarchy and cacheline knowledge from ch02 onto three concrete C++ rewrites: cache-friendly (a review, to make it muscle memory), AoS to SoA (the core rewrite of data-oriented design), and struct alignment and padding (don't waste cachelines). At the end we cover when software prefetch is useful.

## cache-friendly: three iron rules, reviewed

ch02 covered this exhaustively; here we compress it into three memorizable iron rules that anchor every later rewrite:

1. **Contiguous data**: `vector`/`array`/raw arrays beat `list`/`set`/chained buckets. Only contiguity lets one cacheline serve multiple accesses.
2. **Access order**: walk traversal along the direction of memory contiguity (row-major), and the prefetcher pulls upcoming data into cache ahead of time for you.
3. **Small hot dataset**: data you repeatedly sweep has to fit in cache (especially L3), or it will thrash at the capacity boundary (look back at ch02-01: when the working set equals the L3 size, latency jumps from ~12 ns to ~96 ns).

These three are "free" — without changing the algorithm, just adjusting layout and access order, you fill the cache. But when you hit a scenario like "the object has many fields but the loop only touches a few of them," contiguity alone isn't enough, and that's where AoS to SoA comes in.

## AoS to SoA: the core rewrite of data-oriented design

This is the most valuable move in ch04. Look at a typical "object-oriented" particle struct:

```cpp
// AoS: Array of Structures — every field of each particle sits together
struct Particle { float x, y, z;        // position
                  float vx, vy, vz; };  // velocity
Particle ps[N];   // 6 floats = 24 bytes/particle
```

Looks natural enough — each particle is an object, fields packed tight. **The trouble shows up when you only update the position**:

```cpp
for (int i = 0; i < N; ++i)
    ps[i].x += ps[i].vx * 0.016f;   // only touches x, uses vx
```

Each iteration only touches `x` and `vx`, but because it's AoS you pull the entire `ps[i]` (along with the unused `y`, `z`, `vy`, `vz`) into cache. A 24-byte particle where you only use 8 bytes (`x`+`vx`) means **cacheline utilization is 1/3**, with 2/3 of the bandwidth wasted.

**SoA (Structure of Arrays)** lays all the values of a single field contiguously:

```cpp
struct Particles {
    std::vector<float> x, y, z, vx, vy, vz;   // 6 separate arrays
};
Particles ps;
for (int i = 0; i < N; ++i)
    ps.x[i] += ps.vx[i] * 0.016f;   // only touches the x array and the vx array
```

Now you only pull the `x` array and the `vx` array into cache, and cacheline utilization approaches 100%. Measured (N = 1 million particles, only updating `x`):

```text
===== A. AoS vs SoA (updating 1048576 particles' x, average of 20 runs) =====
  AoS:  1.74 ms/run (each row drags the not-updated y/z/vy/vz into cache; wasted bandwidth)
  SoA:  0.18 ms/run (only touches the x and vx arrays; cacheline utilization near 100%)
  SoA is 9.79x faster
```

**Close to 10x.** That's one of the most dramatic rewrites in single-threaded optimization, with no algorithm change and no SIMD — purely from rearranging the data. That's the core idea of **data-oriented design (DOD)**: **organize data by how it's accessed, not by what it "is in the real world."** Fabian's *Data-Oriented Design* and Mike Acton's talks are the source of this line of thought (the talk is saved for vol10; here we only give the conclusion).

SoA has one more dividend: it's **naturally SIMD-friendly** (the `x` array is contiguous, so SIMD can load 8 floats at once); ch04-05 expands on that. The cost is that the code is no longer "object-oriented" — that's an engineering tradeoff, worth it on a performance hotspot, unnecessary off hotspot.

> Boundary note: SoA is a **layout transformation**; explaining "why it's fast" is vol6's job (this article). "Why `vector` is contiguous inside" is vol3's job. Here we only care about the effect of layout on cache.

## Struct alignment and padding: don't waste cachelines

AoS has one more hidden cost — **alignment and padding**. Look at these two structs:

```cpp
struct ParticleAoS  { float x, y, z, vx, vy, vz; };          // 6 floats = 24 B
struct ParticleAoS8 { float x, y, z, vx, vy, vz, pad1, pad2; }; // 8 floats = 32 B
```

`sizeof`, measured: the first is 24 bytes, the second 32 bytes (deliberately padded to a multiple of 8). A 64-byte cacheline holds **two** ParticleAoS (2×24=48B, leaving 16B that can't fit a third — waste), or **two** ParticleAoS8 (2×32=64B exactly, no waste).

```text
sizeof(ParticleAoS)  = 24 B
sizeof(ParticleAoS8) = 32 B (padded to 32)
64B cacheline holds: AoS=2, AoS8=2
```

Here AoS, because its size isn't a power of two, wastes the tail of the cacheline; padding to 32B actually packs it cleanly. The rule behind this is the compiler's **alignment padding**: inside a `struct` the compiler inserts padding according to member alignment (for example, a `char` followed by a `double` gets 7 bytes of padding so the `double` aligns to 8). `sizeof` being bigger than "the sum of the fields" you thought you had is exactly this.

The practical corollary: **on a hot path, sort struct fields by size, descending** (big doubles/pointers first, small ints/chars after) to cut padding; or use `alignas(64)` to give a critical struct its own cacheline (this move is the protagonist in ch05-01 when treating false sharing). `#pragma pack` forces tight packing but breaks alignment and can trigger unaligned-access penalties, **so don't spray it around on performance-sensitive paths**. The mechanism of alignment/padding (why sizeof computes this way) belongs to vol4 class layout; vol6 only covers its effect on cachelines.

## Software prefetch: usually don't

The last move is **software prefetch**, `__builtin_prefetch`. The idea: you know you'll soon access `data[i+stride]`, so you tell the CPU in advance "pull this from memory into cache," and by the time you actually access it, it's already hit, hiding latency.

```cpp
for (int i = 0; i < N; ++i) {
    __builtin_prefetch(&data[i + PREFETCH_DIST]);  // prefetch PREFETCH_DIST steps ahead
    process(data[i]);
}
```

Sounds great, but **modern CPUs come with hardware prefetchers that are extremely good at regular access (sequential, fixed stride)** (look back at ch02-01: sequential traversal on DRAM can hit L1 throughput, courtesy of the hardware prefetcher). So regular access has no use for software prefetch; the hardware already did it.

Software prefetch is genuinely useful for **irregular but predictable access**: linked-list traversal (you know the next node's address ahead of time), B-tree lookup, graph traversal. The hardware prefetcher can't learn these patterns, so hand prefetching can pay off. But even in these scenarios prefetch easily **makes things worse** (prefetching data you don't use, polluting cache, wasting bandwidth). **Always benchmark against a control** — that's a concrete instance of the "don't blindly hand-optimize" discipline that ch04-06 keeps stressing.

Looking back at the cards this article dealt: Backend Memory Bound is the single biggest lever in single-threaded work, where data-layout changes buy a few-fold to ten-fold, far cheaper than adding compute; the three cache-friendly iron rules (contiguous, sequential, small hot dataset) are free; AoS to SoA is nearly 10x faster (measured 9.79x) in the "only update some fields" scenario and is naturally SIMD-friendly — the core DOD rewrite; on alignment and padding, sort hot-path struct fields by size descending to cut padding, and `alignas(64)` treats false sharing (ch05); the mechanism belongs to vol4; software prefetch doesn't help for regular access (the hardware already does it), only helps with irregular-but-predictable access, and must be benchmarked.

The next article moves the battlefield from "data layout" to "the computation itself" — how to write loops so that ILP and the compiler can both help.

## References

- Fabian, R., *Data-Oriented Design* — the source of DOD thinking; local copy in `.claude/drafts/books/`.
- Agner Fog, *Optimizing software in C++*, §7 *Making containers/objects efficient* — the engineering treatment of AoS/SoA, alignment, and padding; local copy.
- Bakhvalov, *Performance Analysis and Tuning on Modern CPUs*, Chapter 9 *Memory Optimizations*.
- Measured code for this article: `code/volumn_codes/vol6-performance/ch04/backend_memory.cpp`.
