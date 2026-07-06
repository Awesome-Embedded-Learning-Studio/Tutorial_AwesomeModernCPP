---
chapter: 2
cpp_standard:
- 14
- 17
description: Between the CPU and main memory sits a 100x speed gap. The memory hierarchy uses fast-but-small-expensive layers to cache slow-but-big-cheap ones. With a memory mountain measured on the author's own machine and a pointer-chasing latency ladder, this article pins down the latency and bandwidth of L1/L2/L3/DRAM and shows why sequential access is two orders of magnitude faster than random.
difficulty: advanced
order: 1
platform: host
prerequisites:
- 'Performance Mindset: efficiency is not performance'
- 'Why microbenchmarks lie'
reading_time_minutes: 11
related:
- 'Cachelines and locality: the 64-byte minimum unit'
- 'Pipeline, ILP, and branch prediction'
- 'TLB, huge pages, and a microarchitecture cheat sheet across CPU families'
tags:
- host
- cpp-modern
- advanced
- 优化
- 内存管理
title: 'Memory hierarchy and the latency ladder: why sequential access is 100x faster'
translation:
  source: documents/vol6-performance/ch02-cpu-microarchitecture/02-01-memory-hierarchy.md
  source_hash: 1b8ce9865f2aa15f890735fe681aa5ffb0b04d3c69991dcbec62721b3508e158
  translated_at: '2026-07-06T00:00:00+00:00'
  engine: manual
  token_count: 3200
---
# Memory hierarchy and the latency ladder: why sequential access is 100x faster

## An uncomfortable fact: your CPU spends most of its time waiting on memory

Back in ch00-01 we made a claim: `std::vector` binary search (O(log n)) can beat `std::set` lookup (also O(log n)) at moderate sizes, identical complexity, performance off by an order of magnitude. The one-line reason we hand-waved back then was "vector is contiguous and cache-friendly, set nodes are scattered and cache-missy." This chapter pulls that "cache-friendly" apart. Behind it is a cold physical fact about modern CPUs: **the CPU computes blazingly fast, but the data can't get to it in time.**

In numbers: a modern CPU pipeline retires multiple instructions per cycle, while fetching one datum from main memory (DRAM) costs hundreds of cycles. In other words, **the moment your data isn't in cache, the CPU sits idle for hundreds of cycles** and all that compute goes to waste. Brendan Gregg calls this "the CPU is a jet engine and memory is a bicycle." No matter how powerful the engine, dragged by a bicycle, it goes nowhere fast.

The prerequisite to understanding this is a precise picture of the memory hierarchy: the closer data is to the CPU, the smaller, more expensive, and faster; the farther away, the bigger, cheaper, and slower. In this article we draw that "latency ladder" from a measurement on my own machine, then explain how it dictates every layout decision you make writing C++.

## The memory hierarchy: each layer fast-but-small, the next slow-but-big

Here's a "textbook plus on-machine measurement" table. Latency numbers are orders of magnitude (they move with architecture and frequency), but the **ratios** are stable, and that's what to remember:

| Layer | Typical latency (cycles) | Measured (ns) | Capacity (this machine, AMD Ryzen 7 5800H) |
|---|---|---|---|
| Registers | 0 cycles | — | a handful of general-purpose registers, compiler-allocated |
| L1 cache (data) | ~4 cycles | **~1.2** | 32 KB, private per core |
| L2 cache | ~14 cycles (Zen 3) | **~3–9** | 512 KB, private per core |
| L3 cache | ~47 cycles (Zen 3) | **~11–60** | 16 MB, shared across cores |
| Main memory DRAM | ~200–400 cycles | **~120** | tens of GB |

> Cycle counts come from Agner Fog's *The microarchitecture of Intel, AMD and VIA CPUs*, Chapter 23 *AMD Zen 3* (Table 23.1: L1=4, L2=14, L3=47 cycles); ns values are pointer-chasing results measured on my 5800H (see below).

Read this table vertically and you get the single most important ratio in this volume: **L1 hit ~1 ns, DRAM fetch ~120 ns, a full 100x apart, that is, two orders of magnitude.** That's the physical root of "why sequential traversal is two orders of magnitude faster than random (in latency)": sequential access pulls subsequent data into cache ahead of time, random access cold-misses every time and has to fetch from DRAM.

Before you take my word for it, let's measure this ladder by hand.

## Run it yourself: measuring the latency ladder on this machine

The cleanest way to measure latency is **pointer chasing**: lay down a circular chain in memory where "the next address is hidden inside the current datum," and let the CPU walk it. The key is that the next address depends on the result of the current load. That's a true dependency, the hardware prefetcher can't guess ahead (it doesn't know what's next), so what you measure is the **bare memory-access latency**.

The core loop is just a few lines (full program in this chapter's code, `memory_mountain.cpp`):

```cpp
// Build a shuffled single ring that visits every node: nxt[perm[i]] = perm[(i+1) % n]
std::vector<long> nxt(elems);
for (long i = 0; i < elems; ++i) nxt[perm[i]] = perm[(i + 1) % elems];

long idx = 0;
auto t0 = std::chrono::steady_clock::now();
for (long s = 0; s < total_steps; ++s) {
    idx = nxt[idx];            // next address depends on this load → true dep, prefetcher helpless
    sink = sink + g_data[idx]; // read data[idx], creating a dependency on the chain
}
auto t1 = std::chrono::steady_clock::now();
```

Sweeping the working set (array size) from 4 KB to 128 MB, crossing the L1/L2/L3/DRAM boundaries, and letting it "stomp" the entire working set each time, here's what I get on my machine (`taskset -c 0` to pin to core 0 and cut noise, WSL2 environment):

```text
===== B. Pointer-chasing random-read latency (ns/access) =====
      size      elems  ns/access level(inferred)
       4K        512       1.19          L1d
       8K       1024       1.18          L1d
      16K       2048       1.24          L1d
      32K       4096       2.50          L1d     ← working set = L1d capacity (32K), starts spilling
      64K       8192       3.33          L2
     128K      16384       4.04          L2
     256K      32768       6.02          L2
     512K      65536       8.88          L2      ← working set ≈ L2 capacity (512K), starts spilling
    1024K     131072      11.42          L3
    2048K     262144      12.40          L3
    4096K     524288      22.59          L3
    8192K    1048576      59.99          L3
   16384K    2097152      96.27          L3      ← working set = L3 capacity (16M), heavy thrashing
   32768K    4194304     135.92         DRAM
   65536K    8388608     118.59         DRAM
  131072K   16777216     122.06         DRAM
```

This table is worth pausing on. It tells a very clean story:

- **4K–16K: all in L1, ~1.2 ns.** This is what "hot cache" looks like, data sitting right next to the execution units. Note that 1.2 ns ÷ 4 cycles ≈ 3.3 GHz, exactly the 5800H's base-clock operating point, lining up precisely with Agner's L1 = 4 cycles.
- **32K: starts climbing (2.5 ns).** The working set exactly equals the L1d capacity (32 KB), it doesn't fit anymore, and some accesses spill to L2. This is the "overflow" tipping point of the cache.
- **64K–512K: into L2, 3–9 ns.** A clean L2 region, but latency creeps up as the working set grows, because the closer it gets to L2 capacity, the more conflict/capacity misses.
- **1M–16M: into L3, 11–60 ns.** Note the 16M row at 96 ns, the classic L3 thrashing regime (working set = L3 capacity, frantically evicting each other).
- **32M and up: DRAM, ~120 ns.** The ladder bottoms out here.

L1's 1.2 ns and DRAM's 120 ns are exactly **100x** apart. That's not a textbook metaphor, it's the physical reality of the machine in your hands.

> One caveat from me: this machine runs WSL2 (a VM), the host manages the CPU frequency, and I can't read the governor, so the absolute ns values jitter by a few percent. But **the ratios between levels** (L1≈1, L2≈a few, L3≈tens, DRAM≈hundreds of ns) are fixed by the hardware and very stable. What's actually trustworthy in a performance article is the ratio, not any single absolute number.

## The memory mountain: drawing locality as a mountain

The latency ladder answers only one dimension, "how does access latency change with the working set." The famous **memory mountain** from CSAPP Chapter 6 adds a second dimension, **stride**, so spatial and temporal locality get laid out together on one table. The information density is much higher.

The idea: fix a working-set size, then read the array in a circular sequential pattern at different strides and measure throughput (GB/s). Smaller stride means consecutive accesses are more likely to land in the same cacheline (spatial locality); smaller size means the data is more likely still in cache (temporal locality). Measured on this machine:

```text
===== A. Memory mountain: read throughput (GB/s) =====
size\stride(B)    8B    16B    32B    64B   128B   256B   512B
      1K        16.9   17.0   17.0   16.7   16.1   16.5   15.3
      8K        16.9   16.6   17.0   17.2   17.1   17.0   17.1
     32K        16.8   16.9   16.9   16.9   16.8   17.2   16.5   ← L1d edge
     64K        16.7   16.9   17.2   16.7   16.8   16.6    7.3
    256K        17.2   17.2   17.2   16.6   16.6   16.7    7.6   ← L2 region
    512K        16.7   16.8   16.6   14.3   11.3   11.6    5.3   ← L2 edge
   1024K        16.7   16.6   16.2   12.9    8.7   11.1    4.5
   4096K        16.5   16.9   16.3   12.9    8.1   10.1    4.4   ← L3 region
  16384K        13.0   10.5    7.7    3.8    3.1    3.1    2.9   ← L3 edge, dropped
  32768K        14.5   11.0    6.7    3.8    2.7    2.6    2.1   ← into DRAM
```

How do you read this mountain? Watch two directions:

**Down the "left column" (stride = 8B, fully sequential):** even with a 32 MB working set (way past L3), throughput is still 14.5 GB/s, almost the same as at 1 KB. That's the **hardware prefetcher** at work: it detects "you're scanning memory at a fixed stride" and pulls subsequent cachelines into cache ahead of time. In other words, **sequential access can approach L1 throughput even on DRAM**, because the prefetcher hides the latency. This is the real mechanism behind "sequential traversal is fast," and we'll unpack it below.

**The "bottom-right corner" (stride = 512B, working set 32 MB):** throughput collapses to 2.1 GB/s, 8x worse than the 17 GB/s in the top-left. A 512B stride means each access lands in a new cacheline, and the stride is too large for the prefetcher to keep up (it can generally only track a limited number of streams), so every access becomes a near-random DRAM fetch. **This is DRAM's true face: fast when sequential, brutally slow when random.**

Read this mountain together with the latency ladder above and the conclusion is the same set: how fast the memory hierarchy is depends *both* on "is the data in cache" (temporal locality) *and* on "is the access pattern contiguous" (spatial locality). The former is decided by working-set size, the latter by access pattern. Get both right and you run at L1 speed; get both wrong and you drop to DRAM's scrap.

## Back to C++: what these numbers are telling us

Hardware knowledge isn't for showing off. It's for guiding how you write code. From this ladder we can translate directly to a few C++ layout principles, one by one:

**1. Keep data contiguous; prefer contiguous containers.** This is the most direct beneficiary of spatial locality. `std::vector` / `std::array` pack elements tightly into one contiguous block of memory; when traversing, a single cacheline (64 bytes) holds several elements, and the prefetcher pulls ahead for you. That's the root cause of fast vector traversal. Conversely, nodes of `std::list` / `std::set` are each `new`'d and scattered across the heap, and pointer chasing over them is the slowest curve in the latency ladder above. **Same complexity, different layout, performance off by an order of magnitude.** ch00-01 measured this with vector binary search vs set lookup; here we fill in the physical basis.

> Boundary note: the **design-level** internals of vector — three pointers, the growth strategy (2x), the element-move cost of insert/erase — belong to vol3 (it answers "why is vector designed this way"). vol6 only answers "why is contiguous layout fast when running on hardware," which is exactly this latency ladder.

**2. Keep the hot working set inside L3.** The latency ladder shows that once the working set exceeds cache capacity, latency jumps from nanoseconds to tens or hundreds of nanoseconds. A very practical corollary: **data that gets scanned repeatedly needs to "fit in cache."** For example, a table queried at high frequency that's 20 MB on a 5800H (whose L3 is only 16 MB) thrashes on every query; if you can compress it to under 12 MB, performance might jump several times. This isn't mysticism, it's the gap between the 16M row (96 ns) and the 4M row (22 ns) in the table.

**3. For random-access workloads, switch to a structure that cuts the number of memory accesses.** Linked lists and balanced trees jump one node at a time, and every jump is a potential cache miss (nodes are scattered). A common engineering compromise is to **flatten the tree**: replace a binary balanced tree with a B-tree / B+ tree, where each node stores dozens or hundreds of keys (cramming one or two cachelines), the height drops sharply, and so do the misses. That's why database indexes and filesystems all use B+ trees instead of red-black trees, not to save pointers, but to save cache misses. `std::set` gets walloped by a B-tree on large-data, query-heavy workloads, and this is the root cause.

**4. `reserve` capacity up front, don't let growth shatter your layout.** This one's more of a C++ engineering practice: a `vector` repeatedly `push_back`'d triggers growth, which moves the whole block to new memory, invalidating the old cache, and the move itself is a big cost. Calling `reserve(estimated capacity)` ahead of time avoids that. The specifics of the growth mechanism (why 2x, the cost of one move) belong to vol3; here just remember the conclusion: **on a hot-path vector, reserve up front.**

## Threads left for the next article

In this article we drew the **latency ladder** on this machine (L1 ~1 ns, L2 ~a few ns, L3 ~tens of ns, DRAM ~120 ns, a 100x gap at each step) using pointer chasing, and used the memory mountain to see **how spatial locality (stride) and temporal locality (size) together determine throughput**. The C++ principles translated from it — use contiguous containers, control the hot working set, swap random access for a B-tree, reserve — are all direct corollaries of these numbers.

But two details we deliberately skipped: how exactly that recurring "**64 bytes**" is derived (why is 32K the edge in the L1 table? why is stride 64B a cliff?), and the counterintuitive costs that come with the cacheline as the minimum unit of cache (for example, two unrelated variables crammed into the same line kicking each other out). That's exactly the topic of the next article.

There's also one bigger thing left for later: this chapter only covered "where data is slow." It didn't cover "why computation stalls." Pipelining, instruction-level parallelism, branch prediction — those are the subject of ch02-03, and together with the memory hierarchy they form the hardware foundation underpinning every "optimize by bottleneck site" chapter later in vol6.

## References

- Agner Fog, *The microarchitecture of Intel, AMD and VIA CPUs*, §23 *AMD Zen 3*: Zen 3 cache-latency table (L1=4, L2=14, L3=47 cycles), pipeline widths, branch throughput. Local copy: `.claude/drafts/books/optimazation_in_cpp/microarchitecture.md`
- Bakhvalov, D., *Performance Analysis and Tuning on Modern CPUs*, Chapter 3 *CPU Microarchitecture*: an engineering view of the memory hierarchy and latency numbers
- Bryant & O'Hallaron, *Computer Systems: A Programmer's Perspective* (CSAPP), Chapter 6 *The Memory Hierarchy*: the source of the memory-mountain experiment, and the formal definitions of "spatial/temporal locality"
- Source for this article's measurements: `code/volumn_codes/vol6-performance/ch02/memory_mountain.cpp`
