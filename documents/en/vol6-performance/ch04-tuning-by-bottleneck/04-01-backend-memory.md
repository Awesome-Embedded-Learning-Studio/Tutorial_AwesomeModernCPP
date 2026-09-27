---
chapter: 4
cpp_standard:
- 17
description: Backend Memory Bound is the biggest lever in single-threaded performance.
  Using a measured particle system, this article works through three things — why
  contiguous plus sequential access is fast (a cache-friendly refresher), how AoS
  to SoA is nearly 10x faster when only some fields get updated, and how struct
  alignment and padding affect cacheline utilization — plus when software prefetch
  is useful
difficulty: advanced
order: 1
platform: host
prerequisites:
- 'Memory hierarchy and the latency ladder: why sequential access is 100x faster'
- 'Cachelines and locality: the 64-byte minimum unit of transfer'
- 'Attribution in practice: from a slow program to a pinpointed bottleneck'
reading_time_minutes: 7
related:
- 'Loop and compute optimization: code motion, eliminating memory references, and multiple accumulators'
- 'SIMD and vectorization: auto-vectorization conditions, intrinsics, and CPU dispatch'
tags:
- host
- cpp-modern
- advanced
- 优化
- 内存管理
title: 'Backend memory bottlenecks: cache-friendly, AoS/SoA, and prefetch'
translation:
  source: documents/vol6-performance/ch04-tuning-by-bottleneck/04-01-backend-memory.md
  source_hash: 606e5c8189143c72cadc8413b47018a834c38ac97ed7933a1d755581f2eca86b
  translated_at: '2026-09-26T06:21:25+00:00'
  engine: anthropic
  token_count: 2900
---
# Backend memory bottlenecks: cache-friendly, AoS/SoA, and prefetch

## The biggest lever in single-threaded performance

The attribution methods from ch03 tell us: the overwhelming majority of C++ programs land their bottleneck in the **Backend Memory Bound** bucket — the execution units are waiting on data. That's actually good news, because "waiting on data" is the **highest-leverage** category of single-threaded optimization: rework the data layout a little and you often net a several-fold, even tenfold-plus speedup, while changing the algorithm or adding SIMD usually only buys tens of percent.

This article treats Backend Memory specifically. We take the memory-hierarchy and cacheline knowledge from ch02 and drop it onto three concrete C++ rewrites: cache-friendly (a review, until it becomes muscle memory), AoS → SoA (the core rewrite of data-oriented design), and struct alignment and padding (stop wasting cachelines). At the end we cover when software prefetch is actually useful.

## cache-friendly: three iron rules, reviewed

ch02 already covered this exhaustively; here we compress it into three iron rules you can recite, as the starting point for every rewrite that follows:

1. **Contiguous data**: `vector`/`array`/raw arrays beat `list`/`set`/chained buckets. Only when data is contiguous can one cacheline serve many accesses.
2. **Access order**: make traversal walk in the direction of memory contiguity (row-major), and the prefetcher pulls upcoming data into cache for you ahead of time.
3. **Small hot dataset**: the data you sweep over and over has to fit in cache (especially L3), or it thrashes at the capacity boundary (recall ch02-01: when the working set equals the L3 size, latency jumps from ~12 ns to ~96 ns).

These three are "free": without touching the algorithm, just adjusting layout and access order fills the cache. But when you hit a scenario where "the object has many fields, yet each loop only uses a few of them," contiguity alone is not enough — that's when you bring in AoS → SoA.

## AoS → SoA: the core rewrite of data-oriented design

This is the most valuable move in all of ch04. Look at a typical "object-oriented" particle struct:

```cpp
// AoS: Array of Structures — every field of each particle sits together
struct Particle { float x, y, z;        // position
                  float vx, vy, vz; };  // velocity
Particle ps[N];   // 6 floats = 24 bytes/particle
```

Looks perfectly natural — each particle is one object, fields packed tight. **The problem shows up when you only update the position**:

```cpp
for (int i = 0; i < N; ++i)
    ps[i].x += ps[i].vx * 0.016f;   // only touches x, uses vx
```

Each iteration only touches `x` and `vx`, but because it's AoS you drag the whole `ps[i]` (along with the unused `y`, `z`, `vy`, `vz`) into cache. Out of a 24-byte particle you use 8 bytes (`x`+`vx`) — **cacheline utilization is just 1/3**, and 2/3 of the bandwidth is wasted for nothing.

**SoA (Structure of Arrays)** lays all the values of the same field out contiguously:

```cpp
struct Particles {
    std::vector<float> x, y, z, vx, vy, vz;   // 6 separate arrays
};
Particles ps;
for (int i = 0; i < N; ++i)
    ps.x[i] += ps.vx[i] * 0.016f;   // only touches the x array and the vx array
```

Now you only pull the `x` array and the `vx` array into cache, and cacheline utilization approaches 100%. Our measurement (N = 1 million particles, updating only `x`):

```text
===== A. AoS vs SoA(更新 1048576 粒子的 x,20 次平均)=====
  AoS:  1.74 ms/次(每行把不更新的 y/z/vy/vz 也拉进 cache,带宽浪费)
  SoA:  0.18 ms/次(只碰 x、vx 数组,cacheline 利用率近 100%)
  SoA 快 9.79x
```

**Close to 10x.** This is one of the most dramatic rewrites in single-threaded optimization — no algorithm change, no SIMD, purely rearranging where the data sits. And that is the core idea of **data-oriented design (DOD)**: **organize data by how it gets accessed, not by what it "is" in the real world.** Fabian's *Data-Oriented Design* and Mike Acton's talks are the wellspring of this line of thinking (the talks are saved for vol10; here we only give the conclusion).

SoA pays one more dividend: it is **naturally suited to SIMD vectorization** (the `x` array is contiguous, so one SIMD load grabs 8 floats) — ch04-05 expands on that. The cost is that the code is no longer "object-oriented"; that's an engineering tradeoff — worth it on a performance hotspot, unnecessary off it.

> Boundary note: SoA is a **layout transformation** — explaining "why is it fast" is vol6's job (this very article), and "why is `vector` contiguous inside" is vol3's job. Here we only care about how layout affects cache.

## Struct alignment and padding: don't waste cachelines

AoS carries one more hidden cost: **alignment and padding**. Look at these two structs:

```cpp
struct ParticleAoS  { float x, y, z, vx, vy, vz; };          // 6 floats = 24 B
struct ParticleAoS8 { float x, y, z, vx, vy, vz, pad1, pad2; }; // 8 floats = 32 B
```

`sizeof`, measured: the first is 24 bytes, the second 32 bytes (deliberately padded to a multiple of 8). A 64-byte cacheline holds **two** ParticleAoS (2×24=48B, with 16B left over that can't fit a third — waste), or **two** ParticleAoS8 (2×32=64B exactly, no waste).

```text
sizeof(ParticleAoS)  = 24 B
sizeof(ParticleAoS8) = 32 B(pad 到 32)
64B cacheline 能装:AoS=2 个,AoS8=2 个
```

Here AoS, because its size is not a power of two, wastes the tail of the cacheline; padding it to 32B instead packs an integral number of structs per line. The rule underneath is the compiler's **alignment padding**: inside a `struct`, the compiler inserts filler according to each member's alignment requirement (for example, a `char` followed by a `double` gets 7 bytes inserted in between so the `double` aligns to 8). When `sizeof` comes out bigger than the "sum of the fields" you had in mind, this is why.

The practical corollary: **on a hot path, order struct fields by size, descending** (big doubles/pointers first, small ints/chars after) to cut down padding; or use `alignas(64)` to give a critical struct a cacheline of its own (that move takes the starring role in ch05-01 when we treat false sharing). `#pragma pack` forces tight packing, but it breaks alignment and can trigger unaligned-access penalties — **don't spray it around on performance-sensitive paths**. The mechanism of alignment/padding (why `sizeof` comes out the way it does) belongs to vol4's class layout; vol6 only covers its effect on cachelines.

## Software prefetch: most of the time, don't

The last move is **software prefetch**, `__builtin_prefetch`. The idea: you know that a little later you'll access `data[i+stride]`, so you tell the CPU in advance "pull this from memory into cache for me" — by the time you actually touch it, it's already a hit, and the latency was hidden.

```cpp
for (int i = 0; i < N; ++i) {
    __builtin_prefetch(&data[i + PREFETCH_DIST]);  // prefetch PREFETCH_DIST steps ahead
    process(data[i]);
}
```

Sounds lovely, but **modern CPUs ship with hardware prefetchers that are extremely good at regular access patterns (sequential, fixed stride)** (recall ch02-01: sequential traversal reaches L1-grade throughput even on DRAM — that's the hardware prefetcher at work). So for regular access there's no need for software prefetch at all; the hardware already did it.

Where software prefetch genuinely helps is **irregular but predictable access**: linked-list traversal (you know the next node's address ahead of time), B-tree lookups, graph traversal. The hardware prefetcher can't learn these patterns, so prefetching by hand can pay off. But even in these scenarios prefetch easily backfires (you prefetch data you never use, polluting cache and wasting bandwidth). **Always benchmark against a control** — this is one concrete instance of the "don't hand-optimize blindly" discipline that ch04-06 keeps hammering on.

Looking back at the cards this article played: Backend Memory Bound is the biggest single-threaded lever — data-layout changes buy a several-fold to tenfold-plus speedup, far cheaper than adding compute; the three cache-friendly iron rules (contiguous, sequential, small hot dataset) are free; AoS → SoA is nearly 10x faster (measured 9.79x) in the "only update some fields" scenario and is naturally SIMD-friendly — the core DOD rewrite; on alignment and padding, sorting hot-path struct fields by descending size cuts padding and `alignas(64)` treats false sharing (ch05), with the mechanism belonging to vol4; software prefetch does nothing for regular access (the hardware already does it), serves only irregular-but-predictable access, and must always be benchmarked.

In the next article we move the battlefield from "data layout" to "the computation itself" — how to write loops so that both ILP and the compiler can pitch in.

## References

- Fabian, R., *Data-Oriented Design* — the wellspring of DOD thinking; local copy under `.claude/drafts/books/`
- Agner Fog, *Optimizing software in C++*, §7 *Making containers/objects efficient* — the engineering treatment of AoS/SoA, alignment, and padding; local copy.
- Bakhvalov, *Performance Analysis and Tuning on Modern CPUs*, Chapter 9, *Memory Optimizations*.
- Measured code for this article: `code/volumn_codes/vol6-performance/ch04/backend_memory.cpp`
