---
chapter: 2
cpp_standard:
- 14
- 17
description: Between the CPU and main memory lies a 100x speed gulf, and the memory hierarchy is how fast-but-small-expensive layers cache slow-but-big-cheap ones. With a memory mountain and a pointer-chasing latency ladder measured on a real machine, we see the latency and bandwidth of L1/L2/L3/DRAM each, and understand why sequential access beats random access by two orders of magnitude.
difficulty: advanced
order: 1
platform: host
prerequisites:
- 'Performance Mindset: efficiency is not performance'
- Why microbenchmarks lie
reading_time_minutes: 11
related:
- 'Cachelines and locality: the 64-byte minimum unit of transfer'
- 'Pipeline, ILP, and branch prediction: same data, several-fold differences depending on how you execute it'
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
  translated_at: '2026-09-26T05:54:40+00:00'
  engine: anthropic
  token_count: 7200
---
# Memory hierarchy and the latency ladder: why sequential access is 100x faster

## An uncomfortable fact: your CPU spends most of its time waiting for memory

Back in ch00-01 we laid down that proposition: binary search over a `std::vector` (O(log n)) can beat lookup in a `std::set` (also O(log n)) at moderate scale — same complexity, an order of magnitude apart in performance. The one-line reason we handed out at the time was "vector is contiguous and cache-friendly; set's nodes are scattered and cache-miss". This chapter takes that "cache-friendly" completely apart. Behind it stands a cold physical fact of modern CPUs: **the CPU computes blazingly fast, but the data cannot be moved to it fast enough.**

In concrete numbers, a contemporary CPU pipeline can retire multiple instructions per cycle, while one trip to main memory (DRAM) to fetch a piece of data costs upwards of a hundred cycles. In other words, **as soon as your data is not in the cache, the CPU just sits there idling for hundreds of cycles**, and all that compute power goes to waste. Brendan Gregg describes this as "the CPU is a jet engine, memory is a bicycle": however powerful the engine, being towed by a bicycle, it still can't go fast.

The prerequisite for understanding all this is a precise picture of the memory hierarchy: the closer data sits to the CPU, the smaller, pricier, and faster the storage; the farther away, the bigger, cheaper, and slower. In this article we draw that "latency ladder" with measurements from our own machine, and then work out how it dictates every layout decision you make when writing C++.

## The memory hierarchy: each layer fast but small, the next layer slow but big

First, a "textbook + local measurement" table. The latency figures give magnitudes (they shift with architecture and frequency), but the **ratios between them** are stable — that is the part worth memorizing:

| Layer | Typical latency (cycles) | Measured locally (ns) | Capacity (local AMD Ryzen 7 5800H) |
|---|---|---|---|
| Registers | 0 cycles | — | A handful of general-purpose registers, allocated by the compiler |
| L1 cache (data) | ~4 cycles | **~1.2** | 32 KB, private per core |
| L2 cache | ~14 cycles (Zen 3) | **~3–9** | 512 KB, private per core |
| L3 cache | ~47 cycles (Zen 3) | **~11–60** | 16 MB, shared by all cores |
| Main memory DRAM | ~200–400 cycles | **~120** | Tens of GB |

> Cycle counts are taken from Agner Fog's *The microarchitecture of Intel, AMD and VIA CPUs*, Chapter 23 *AMD Zen 3* (Table 23.1: L1=4, L2=14, L3=47 cycles); the ns figures are pointer-chasing results measured on my local 5800H (see below).

Read this table top to bottom and you get the single most important set of ratios in this entire volume: **an L1 hit costs about 1 nanosecond, a DRAM fetch about 120 nanoseconds — a full 100x apart, two orders of magnitude.** That is the physical foundation of "why sequential traversal beats random by two orders of magnitude (in latency)": sequential access pulls upcoming data into the cache ahead of time, while random access cold-misses every single time and has to fetch from DRAM on the spot.

Now, before you take my word for any of this, let's go measure this ladder with our own hands.

## Hands on: measuring the latency ladder on your own machine

The cleanest way to measure latency is **pointer chasing**: lay out a circular chain in memory where "the next address is hidden inside the current data", and make the CPU walk the whole thing. The key is that the next address depends on the result of the current load — a true dependency. The hardware prefetcher has no way to guess ahead (it doesn't know which address comes next), so what you end up measuring is **bare memory-access latency**.

The core loop is just these few lines (full program in this chapter's code, `memory_mountain.cpp`):

```cpp
// Build a shuffled single ring through all nodes: nxt[perm[i]] = perm[(i+1) % n]
std::vector<long> nxt(elems);
for (long i = 0; i < elems; ++i) nxt[perm[i]] = perm[(i + 1) % elems];

long idx = 0;
auto t0 = std::chrono::steady_clock::now();
for (long s = 0; s < total_steps; ++s) {
    idx = nxt[idx];            // the next address depends on this load → a true dependency, the prefetcher is helpless
    sink = sink + g_data[idx]; // read data[idx], creating a dependency along the chain
}
auto t1 = std::chrono::steady_clock::now();
```

Sweeping the working set (the array size) from 4 KB up to 128 MB, crossing the L1/L2/L3/DRAM boundaries, each time letting the walk "stomp" over the entire working set, the results on my machine (`taskset -c 0` pinned to core 0 to cut noise, WSL2 environment) were:

```text
===== B. 指针追逐随机读延迟 (ns/访问) =====
      size      elems  ns/access level(推断)
       4K        512       1.19          L1d
       8K       1024       1.18          L1d
      16K       2048       1.24          L1d
      32K       4096       2.50          L1d     ← working set = L1d capacity (32K), starts to overflow
      64K       8192       3.33          L2
     128K      16384       4.04          L2
     256K      32768       6.02          L2
     512K      65536       8.88          L2      ← working set ≈ L2 capacity (512K), starts to overflow
    1024K     131072      11.42          L3
    2048K     262144      12.40          L3
    4096K     524288      22.59          L3
    8192K    1048576      59.99          L3
   16384K    2097152      96.27          L3      ← working set = L3 capacity (16M), thrashing hard
   32768K    4194304     135.92         DRAM
   65536K    8388608     118.59         DRAM
  131072K   16777216     122.06         DRAM
```

This table deserves a pause. It tells a very clean story:

- **4K–16K: entirely in L1, ~1.2 ns.** This is what a "hot cache" looks like — the data sits right next to the compute units. Note that 1.2 ns ÷ 4 cycles ≈ 3.3 GHz, exactly the 5800H's base-clock operating point, slotting perfectly onto Agner's L1 = 4 cycles.
- **32K: starts to climb (2.5 ns).** The working set exactly equals the L1d capacity (32 KB) — it no longer fits, and part of the accesses gets pushed out to L2. This is the critical point where the cache "overflows".
- **64K–512K: into L2, 3–9 ns.** A clean L2 region, but latency creeps upward as the working set grows, because the closer the working set gets to the L2 capacity, the more conflict/capacity misses occur.
- **1M–16M: into L3, 11–60 ns.** Note the 16M row at 96 ns — the classic look of the L3 thrash zone (working set = L3 capacity, lines evicting each other like crazy).
- **32M and above: DRAM, ~120 ns.** The ladder has now fallen all the way to the bottom.

L1's 1.2 ns and DRAM's 120 ns are exactly **100x** apart. This is not a textbook metaphor; it is the physical fact of the machine in your hands.

> A reminder from me here: I ran this on WSL2 (a VM), where the CPU frequency is managed by the host and the governor isn't readable, so the absolute ns figures wobble by a few percent. But the **ratios between levels** (L1≈1, L2≈a few, L3≈tens, DRAM≈a hundred-plus ns) are decided by the hardware and are rock solid. What is actually trustworthy in a performance article is the ratios, never any single absolute number.

## The memory mountain: drawing locality as a mountain

The latency ladder answers only one dimension: "how access latency varies with the working set". The famous **memory mountain** from Chapter 6 of CSAPP adds a second dimension, the **stride**, so spatial locality and temporal locality get laid out on the same table at once — much higher information density.

The procedure: fix a working-set size, walk the array sequentially in a ring at different strides, and measure throughput (GB/s). The smaller the stride, the more likely two consecutive accesses land in the same cacheline (spatial locality); the smaller the size, the more likely the data is still sitting in the cache (temporal locality). Measured locally:

```text
===== A. memory mountain: 读吞吐 (GB/s) =====
size\stride(B)    8B    16B    32B    64B   128B   256B   512B
      1K        16.9   17.0   17.0   16.7   16.1   16.5   15.3
      8K        16.9   16.6   17.0   17.2   17.1   17.0   17.1
     32K        16.8   16.9   16.9   16.9   16.8   17.2   16.5   ← L1d boundary
     64K        16.7   16.9   17.2   16.7   16.8   16.6    7.3
    256K        17.2   17.2   17.2   16.6   16.6   16.7    7.6   ← L2 region
    512K        16.7   16.8   16.6   14.3   11.3   11.6    5.3   ← L2 boundary
   1024K        16.7   16.6   16.2   12.9    8.7   11.1    4.5
   4096K        16.5   16.9   16.3   12.9    8.1   10.1    4.4   ← L3 region
  16384K        13.0   10.5    7.7    3.8    3.1    3.1    2.9   ← L3 boundary, throughput drops
  32768K        14.5   11.0    6.7    3.8    2.7    2.6    2.1   ← into DRAM
```

How do you read this mountain? Fix your eyes on two directions:

**Down the left column (stride = 8B, sequential contiguous access)**: even with a 32 MB working set (far beyond L3), throughput is still 14.5 GB/s, essentially the same as at 1 KB. The credit goes to the **hardware prefetcher**: it detects "you are scanning memory sequentially at a fixed stride" and pulls later cachelines into the cache ahead of time. Put differently, **sequential access can approach L1-level throughput even when the data lives in DRAM**, because the prefetcher hides the latency. This is the real mechanism behind "sequential traversal is fast" — we will expand on it below.

**The bottom-right corner (stride = 512B, 32 MB working set)**: throughput collapses to 2.1 GB/s, 8x down from the 17 GB/s in the top-left corner. A 512B stride means every access steps on a brand-new cacheline, and the stride is too large for the prefetcher to keep up (it can generally only prefetch a limited number of streams), so every access turns into a nearly random DRAM fetch. **This is DRAM's true face: sequential, it is fast; random, it is brutally slow.**

Put the mountain together with the earlier latency ladder, and the conclusion is one and the same: how fast the memory hierarchy runs depends **simultaneously** on "is the data in the cache" (temporal locality) and "is the access contiguous" (spatial locality). The former is decided by the working-set size, the latter by the access pattern. Get both right, and you run at L1 speed; get both wrong, and you sink to a fraction of what DRAM could even deliver.

## Back to C++: what these numbers are telling us

Hardware knowledge is not for showing off; it exists to guide how we write code. Several C++ layout principles translate directly out of this ladder — let's match them up one by one:

**1. Store data contiguously; prefer contiguous containers.** This is the most direct beneficiary of spatial locality. `std::vector` / `std::array` pack elements tightly into one contiguous block of memory; during traversal a single cacheline (64 bytes) holds several elements, and the prefetcher pulls ahead for you on top of that — this is the root cause of vector's fast iteration. The other side of the coin: the nodes of `std::list` / `std::set` are each `new`'d separately and scattered all over the heap, and chasing their pointers is the slowest curve in the latency ladder above. **Same complexity, different layout, an order of magnitude of performance difference.** ch00-01 already demonstrated this with vector binary search vs set lookup; here we have filled in its physical justification.

> Boundary note: vector's internal design-level content — "three pointers, the growth strategy (2x), the element-moving cost of insert/erase" — belongs to vol3 (it answers "why is vector designed this way"). vol6 only answers "when running on hardware, why is contiguous layout fast" — which is precisely this latency ladder of ours.

**2. Keep the hot data set within L3.** The latency ladder shows that once the working set exceeds the cache capacity, latency jumps from nanoseconds to tens or hundreds of nanoseconds. A very practical corollary: **data that gets scanned over and over must "fit inside the cache"**. For instance, take a heavily-queried table: at 20 MB, with the 5800H's L3 at only 16 MB, every query thrashes; compress it to 12 MB or less, and performance can jump several-fold outright. That is not mysticism — it is the gap between the 16M row (96 ns) and the 4M row (22 ns) in the table above.

**3. For random-access workloads, switch to structures that cut the number of memory accesses.** Every hop of a linked list / balanced tree is a potential cache miss (the nodes are scattered). The common engineering compromise is **to flatten the tree**: replace the binary balanced tree with a B-tree / B+ tree, store tens to a hundred-plus keys per node (filling one or two cachelines), and the tree height plummets — and the miss count plummets with it. This is why database indexes and filesystems all use B+ trees instead of red-black trees: not to save pointers, but to save cache misses. It is also the root reason `std::set` gets roundly crushed by B-trees once the data is large and the queries are dense.

**4. reserve your capacity up front; don't let reallocation scatter your layout.** This one leans toward C++ engineering practice: repeated `push_back` on a `vector` triggers growth, which hauls the entire block of data to new memory and invalidates every old cache entry — and the haul itself is a hefty one-time cost. Calling `reserve(estimated capacity)` ahead of time avoids all of this. The specifics of the growth mechanism (why 2x, what one relocation costs) belong to vol3; here, just remember the conclusion: **for vectors on hot paths, reserve up front**.

## Loose threads for the next article

In this article we drew this machine's **latency ladder** with pointer chasing (L1 ~1 ns, L2 ~a few ns, L3 ~tens of ns, DRAM ~120 ns — a 100x gulf level by level), and used the memory mountain to see exactly **how spatial locality (stride) and temporal locality (size) jointly determine throughput**. The C++ principles translated out of these numbers — use contiguous containers, keep the hot data set small, switch to B-trees for random access, reserve up front — are all direct corollaries of this set of figures.

But two details were deliberately left aside: where exactly does that recurring "**64 bytes**" come from (why is 32K the boundary in the L1 table? why is stride 64B a threshold?), and the counterintuitive costs that come with the cacheline being the cache's minimum unit of transfer (for example, two unrelated variables squeezed into the same line kicking each other out). Those are exactly the next article's topic.

And one big thing is left for even later: this chapter covered only "where data goes slow"; it has not covered "why computation stalls". Pipelines, instruction-level parallelism, branch prediction — those are ch02-03's content. Together with the memory hierarchy, they form the hardware foundation for everything later in vol6 about "optimizing by bottleneck location".

## References

- Agner Fog, *The microarchitecture of Intel, AMD and VIA CPUs*, §23 *AMD Zen 3*: Zen 3's cache latency table (L1=4, L2=14, L3=47 cycles), pipeline width, branch throughput. Local copy: `.claude/drafts/books/optimazation_in_cpp/microarchitecture.md`
- Bakhvalov, D., *Performance Analysis and Tuning on Modern CPUs*, Chapter 3 *CPU Microarchitecture*: an engineering perspective on the memory hierarchy and latency figures
- Bryant & O'Hallaron, *Computer Systems: A Programmer's Perspective* (CSAPP), Chapter 6 *The Memory Hierarchy*: the origin of the memory mountain experiment, plus the formal definitions of "spatial/temporal locality"
- This article's measurement code: `code/volumn_codes/vol6-performance/ch02/memory_mountain.cpp`
