---
chapter: 2
cpp_standard:
- 17
description: The cache does not move data byte-by-byte, nor in units of whatever size you happen to touch — its minimum unit of transfer is the 64-byte cacheline. Even a 1-byte read pulls the entire 64 bytes into cache. We pinpoint this 64-byte cliff with a stride scan, then let a 6x gap between row-major and column-major traversal show the power of spatial locality
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'Memory hierarchy and the latency ladder: why sequential access is 100x faster'
reading_time_minutes: 12
related:
- 'Pipeline, ILP, and branch prediction: same data, several-fold differences depending on how you execute it'
- 'Backend memory bottlenecks: cache-friendly, AoS/SoA, and prefetch'
tags:
- host
- cpp-modern
- intermediate
- 优化
- 内存管理
title: 'Cachelines and locality: the 64-byte minimum unit of transfer'
translation:
  source: documents/vol6-performance/ch02-cpu-microarchitecture/02-02-cacheline-and-locality.md
  source_hash: 6a6e7203adac373ba9ae435ddbceb1e218234d7285757ec965f432b9c8968917
  translated_at: '2026-09-26T05:56:33+00:00'
  engine: anthropic
  token_count: 3200
---
# Cachelines and locality: the 64-byte minimum unit of transfer

## Starting from the ledge in the previous memory mountain

There was a detail hiding in the previous article's memory mountain that we brushed past at the time. Pull out the working set = 1024K (which lands in L3) row of that mountain on its own:

```text
1024K   16.7  16.6  16.2  12.9   8.7  11.1   4.5
        8B    16B   32B   64B   128B  256B   512B   ← stride
```

As stride grows from 8B to 32B, throughput barely moves (around 16 GB/s); but the moment it crosses **64B**, the numbers visibly sag. This is not noise — the position of that ledge is stable, and it corresponds to a rock-solid physical parameter of the cache: **the size of a cacheline**. This article takes those 64 bytes apart, because they are the key to understanding every question of the form "why does layout affect performance".

## The cacheline: the cache's minimum unit

Many people carry an intuitive but wrong model of the cache: I access an `int` (4 bytes), so the hardware goes to memory and moves 4 bytes in. **Not even close.** The reality: transfers between cache and main memory happen in fixed-size blocks, and that block is called a **cacheline** (also cache line / cache block). On contemporary x86 and ARM, that size is almost uniformly **64 bytes**.

What this means: whether you touch a 1-byte `char`, an 8-byte `double`, or read a single `int`, **if that access misses the cache, the hardware pulls in the entire 64-byte cacheline containing that address.** You only wanted to pay for 4 bytes, but you paid the transfer time of 64 bytes (of course, the rest of those 64 bytes is free if you access it next — that is exactly the mechanism of spatial locality).

```bash
$ cat /sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size
64
$ getconf LEVEL1_DCACHE_LINESIZE
64
```

`index0` is the L1 data cache, and `coherency_line_size` is the cacheline size. L1/L2/L3 are all 64 bytes here (you can check `index1/2/3` one by one; mine all report 64). This value is fixed within a CPU generation, which is why performance articles often hardcode "64B" — but **the first time you tune on an unfamiliar machine, confirm it with `getconf` first**: some low-power ARM cores and older Intel chips use 32-byte lines. This reminder sounds fussy, but once you have actually fallen into a 32B-line pit you never forget it again.

With the model clear, let's now measure that 64 by hand with a behavioral experiment.

## Hands on: a stride scan pinpoints the 64-byte cliff

The idea is direct: pin the working set at a size that is "larger than L1, landing in L3" (so every miss carries real cost), then vary only the stride and watch where throughput falls off a cliff. If the cliff lands exactly at stride = 64B, that is the behavioral cross-check that "the cache operates in 64-byte units".

The core is still the sequential circular traversal, but this time we sweep the stride finely:

```cpp
double stride_throughput(long elems, long stride_elem) {
    const long ACCESSES = 64'000'000;
    long mask = elems - 1;            // elems is a power of two
    int sink = 0; long idx = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (long i = 0; i < ACCESSES; ++i) {
        sink += g_data[idx];
        idx = (idx + stride_elem) & mask;
    }
    do_not_optimize(sink);            // prevent DCE, same semantics as Google Benchmark DoNotOptimize
    auto t1 = std::chrono::steady_clock::now();
    double secs = std::chrono::duration<double>(t1 - t0).count();
    return (double)ACCESSES / secs / 1e6; // M accesses/sec
}
```

Working set 2 MB (an `int` array, 512K elements), pinned with `taskset -c 0`, sweep the stride once:

```text
===== A. Stride scan (working set 2MB, lands in L3) =====
stride(B)     M accesses/sec   notes
       4B       2017.5   < cacheline: several accesses amortize one line
       8B       1956.3   < cacheline: several accesses amortize one line
      16B       1986.7   < cacheline: several accesses amortize one line
      32B       1962.3   < cacheline: several accesses amortize one line
      48B       1820.1   < cacheline: several accesses amortize one line
      56B       1543.5   < cacheline: several accesses amortize one line
      64B       1422.1   = cacheline: each access lands exactly on a new line   ← cliff
      72B       1181.8   > cacheline: every access hits a new line
      96B       1034.8   > cacheline: every access hits a new line
     128B        995.8   > cacheline: every access hits a new line
     256B       1324.2   > cacheline: every access hits a new line   ← anomalous bump, see below (suspected prefetcher)
     512B        528.7   > cacheline: every access hits a new line
```

How to read this table: keep your eyes on "**stride = 64B is the watershed**".

- **stride < 64B (4 through 32)**: several consecutive accesses land inside the same 64-byte cacheline. The first access misses and pulls the whole line in; the following accesses all hit the line that has already been moved over, nearly free. Hence throughput is high and flat (~2000 M accesses/sec) — the parts of those 64 bytes you didn't touch came along for the ride, free of charge.
- **stride ≥ 64B (64, 72, 96...)**: every access steps into a **new** cacheline, the free-ride perk disappears, and each access pays the cost of moving a line. Throughput duly drops to ~1000–1400 M accesses/sec.

This dip from ~2000 down to ~1000, landing precisely at stride = 64B, is our behavioral cross-validation of "one cacheline = 64 bytes".

> I want to single out the 256B row: it is actually higher than 128B (1324 vs 996), which looks "unscientific". **The prime suspect is the hardware prefetcher** — prefetchers on contemporary CPUs can recognize access streams with a fixed, constant stride (surprisingly large strides still fit inside their learning window) and pull later lines in ahead of time, hiding part of the latency. **But this is a mechanism-based conjecture, not a measured conclusion**: my WSL2 machine offers no way to turn the prefetcher off for a controlled comparison (disabling prefetching requires writing an MSR, which WSL2 cannot reach), and the exact trackable-stride ceiling and stream count of the Zen prefetcher are not publicly documented; the 256B row also enjoys a geometric dividend from "the traversal period over the same array gets shorter, so warm-up is more thorough" (see the circular traversal via `mask = elems-1` in the code). At 512B the number keeps falling, and the dominant factor is more likely cacheline utilization (each access consumes only a small slice of a 64B line; see the large-stride bandwidth drop in 02-01). The lesson here: **when measuring cache behavior, the prefetcher is a variable that loves to meddle** — when you see an unreasonable bump, suspect it first, but also admit that until the controlled experiment has been run, "suspicion" is not "proof" (exactly the "sounds like an explanation" pseudo-causality trap that vol6 ch00-01 warns about over and over).

## Spatial locality: contiguous layout is a double win

Translate the previous section's conclusion into a C++ design principle, and you get the one everyone has heard but few have thought through: **keep your data contiguous**. Contiguous layout collects a double dividend:

1. **Amortized cacheline loads**: once a 64-byte line is moved in, contiguous accesses put every element of those 64 bytes to use — nothing is wasted. For the same one miss, accessing 16 `int`s (64B) costs exactly the same transfer as accessing 1 `int`.
2. **Feeding the prefetcher**: the prefetcher's favorite meal is a constant-stride sequential stream. Walk a `vector` sequentially and it detects a stride = 4B stream, pulls the next few cachelines into cache ahead of time, and by the time you get there they already hit — **it even saves you the misses**.

This is why iterating a `std::vector` / `std::array` / native array is blazing fast, while walking the nodes of a `std::list` / `std::set` / `std::unordered_map` (chained buckets) is slow: those nodes are scattered all over the heap — no amortization (each node occupies an entire cacheline but you read only a small part of it), and the prefetcher cannot keep up (the next node's address is unpredictable).

This principle has one especially classic pit that every C++ programmer should step in with their own feet once: the traversal order of a two-dimensional array.

## Row-major vs column-major: where the 6x gap comes from

Two-dimensional arrays in C and C++ (whether native `a[N][N]` or a 1D simulation `a[i*N+j]`) are stored **row-major** in memory: the first row is laid down, then the second. In other words, `a[i][j]` and `a[i][j+1]` sit next to each other in memory (4 bytes apart), while `a[i][j]` and `a[i+1][j]` are separated by an entire row (`N*4` bytes).

This means: when a double loop traverses the matrix, **the inner loop walking along the row is sequential access, while walking along the column is a large-stride hop**. We measured with a 2048×2048 `int` matrix (16 MB, exactly the L3 size on this machine, to amplify the difference):

```cpp
void walk_2d(int* a, int N, bool row_major) {
    volatile int sink = 0; int s = 0;
    auto t0 = std::chrono::steady_clock::now();
    if (row_major)
        for (int i = 0; i < N; ++i)
            for (int j = 0; j < N; ++j) s += a[i * N + j]; // along the row: sequential, stride=4B
    else
        for (int i = 0; i < N; ++i)
            for (int j = 0; j < N; ++j) s += a[j * N + i]; // along the column: hopping, stride=N*4B=8KB
    sink = s;
    auto t1 = std::chrono::steady_clock::now();
    /* print elapsed time and throughput */
}
```

```text
===== B. 2D traversal (N=2048, matrix 16 MB = L3 size) =====
   row-major:     1.0 ms,  16.7 GB/s
   col-major:     6.3 ms,   2.7 GB/s
```

**Same matrix, same amount of computation, same hits in L3 — column-major is more than 6x slower than row-major.**

Take it apart: row-major, the inner `j` advances 4 bytes each step, fully sequential, so one cacheline (64B) serves 16 `int` accesses and the prefetcher pulls data in ahead of time — throughput saturates at 16.7 GB/s. Column-major, the inner `j` makes the address jump `N*4 = 8192` bytes each step, every access steps into a brand-new cacheline, and the 8 KB stride is too large for the prefetcher to chase (recall the misery of the 512B row in the previous section), so it degenerates into L3 random-access throughput, leaving only 2.7 GB/s.

That is the entire justification for "the inner loop must walk along the memory-contiguous direction". If you write matrix multiplication, image convolution, or grid-style simulations and get this direction backwards, you are giving away several times the performance for nothing — and the compiler will **not** flip it for you automatically (it cannot prove that swapping the loop order does not change the result; in most cases it actually doesn't, but the compiler doesn't dare).

> By the way: this "swapping of loop order" is called **loop interchange**, one of the compiler optimizations covered in depth later in ch04-02 on loop optimization. For now just remember its physical motivation: make the innermost loop walk along the memory-contiguous direction.

## Struct field ordering: squeezing hot data into the same line

The cacheline also directly decides one layout question that is special to C++: **how to order the fields of a struct**. Look at these two structs:

```cpp
struct Bad {
    int    id;          // hot: looked up every frame
    char   debug_tag;   // cold: only read when logging
    double values[6];   // hot: computed every frame
    void*  parent;      // cold: only used when walking the tree
};

struct Good {
    int    id;          // hot
    double values[6];   // hot   ← hot fields grouped together
    char   debug_tag;   // cold
    void*  parent;      // cold  ← cold fields set apart
};
```

In `Bad`, hot and cold fields interleave: every time traversal touches a hot field, the cold fields sharing its cacheline get dragged into cache too, squatting on precious cache space that could have held a few more hot objects. `Good` concentrates the hot fields, so one cacheline carries as much "will actually be used" data as possible. This is called **hot/cold field splitting**, and in essence it is deploying your cachelines like troops, making every slot of them count where it matters.

This rule has a more aggressive version called **AoS → SoA** (Array of Structs becomes Struct of Arrays): when you have a huge pile of objects but each pass processes only one of their fields (say, a physics simulation updating only positions), laying out "the same field contiguously" is far faster than "one object's fields adjacent". This goes deeper than field ordering and carries more subtlety, so we leave it for the dedicated chapter ch04-01 "backend memory bottlenecks"; here we only plant the idea.

> A boundary note: struct **alignment, padding, `#pragma pack`** — the machinery behind "why `sizeof` is bigger than you thought" (for example, the compiler inserting padding after a `char` so the `double` aligns to 8 bytes) — belongs to ABI / layout rules, which vol4 touches when it covers class layout. Here in vol6 we only care about the performance meaning of this layer: clustering hot fields is cache-friendly.

## A foreshadowing: the cost of sharing a line (false sharing)

There is one more face of the cacheline we have deliberately not unfolded. Since "neighboring addresses share the same cacheline", what happens when two **unrelated** variables happen to be squeezed into the same 64 bytes? They evict each other from cache — and worse, under multithreading this triggers **false sharing**: thread A writes variable x, thread B writes variable y; x and y are logically unrelated, but they sit on the same cacheline, so the hardware coherence protocol keeps invalidating that line back and forth between the two cores, and performance collapses.

You cannot see this on a single core; it is a multicore-only pit, and an unusually deep one. So the full false-sharing measurements and the `alignas(64)` fix are saved for the dedicated dissection in the ch05 multicore-performance chapter; here we just plant the foreshadowing: a cacheline's "sharing" is a dividend on a single core, and can turn into a tax on many cores.

## Threads left for later

As of this article, we have confirmed that the cache's minimum unit of transfer is the **64-byte cacheline**, and two measurements have shown its consequences: the **stride scan** sees the throughput cliff land exactly at stride = 64B (behavioral-level proof), and **row-major vs column-major traversal differs by 6x** (the inner loop's direction decides whether your accesses are sequential or large-stride hops). Add **hot-field clustering**, which keeps precious cachelines carrying only data that will be used (AoS→SoA waits for ch04-01), plus the multicore dark side, **false sharing** (saved for ch05).

But a program's performance does not depend only on data movement: the way the CPU executes instructions itself (pipelining, instruction-level parallelism, branch prediction) can also produce several-fold gaps on the same data. In the next article we step into the CPU's execution core.

## References

- Agner Fog, *The microarchitecture of Intel, AMD and VIA CPUs*, §22.16 *Cache and memory access*: Zen-family cache parameters (64 B lines, ways/sets at each level), hardware prefetch behavior. Local copy: `.claude/drafts/books/optimazation_in_cpp/microarchitecture.md`
- Bryant & O'Hallaron, *CSAPP*, Chapter 6 *The Memory Hierarchy*: formal definitions of cachelines and spatial/temporal locality, plus the memory mountain
- Drepper, U., *What Every Programmer Should Know About Memory*: engineering details of cachelines, alignment, and prefetching (the classic long read)
- This article's measurement code: `code/volumn_codes/vol6-performance/ch02/cacheline_locality.cpp`
