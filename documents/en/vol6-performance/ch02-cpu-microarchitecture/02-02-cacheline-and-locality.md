---
chapter: 2
cpp_standard:
- 17
description: Cache doesn't move bytes one at a time, nor in whatever size you happen to access. It moves data in 64-byte cachelines, so even a 1-byte read pulls an entire 64-byte line into cache. With a stride scan we pin down this 64-byte cliff precisely, then use a 6x gap between row-major and column-major traversal to show the power of spatial locality.
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'Memory hierarchy and the latency ladder: why sequential access is 100x faster'
reading_time_minutes: 12
related:
- 'Pipeline, ILP, and branch prediction'
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
  translated_at: '2026-07-06T00:00:00+00:00'
  engine: manual
  token_count: 3400
---
# Cachelines and locality: the 64-byte minimum unit of transfer

## Starting from the "cliff" in the previous mountain

The memory mountain from the last article hid a detail we waved past at the time. Pull out the row of that mountain where the working set is 1024K (which lands in L3):

```text
1024K   16.7  16.6  16.2  12.9   8.7  11.1   4.5
        8B    16B   32B   64B   128B  256B   512B   ← stride
```

Throughput barely moves from stride 8B up to 32B (around 16 GB/s); the moment it crosses **64B**, the numbers drop noticeably. That's not noise, the position of this "cliff" is stable. It corresponds to a hard physical parameter of the cache: the size of a **cacheline**. In this article we crack open that 64 bytes; it's the key to understanding every "why does layout affect performance" question.

## The cacheline: the minimum unit of cache

A lot of people hold an intuitive but wrong model of cache: I access an `int` (4 bytes), the hardware goes to memory and pulls 4 bytes in. **Not even close.** The reality is that transfers between cache and main memory happen in fixed-sized blocks, and this block is called a **cacheline** (also cache line / cache block). On contemporary x86 and ARM, this size is almost universally **64 bytes**.

What this means: whether you access a 1-byte `char`, an 8-byte `double`, or just read an `int` once, **if this access misses the cache, the hardware pulls in the entire 64-byte cacheline containing that address, as a block.** You only wanted to pay for 4 bytes; you actually paid the time to move 64 (though the rest of those 64 bytes are free if you access them next — that's exactly the mechanism of spatial locality).

The 64-byte number isn't a guess, the OS tells you directly. On this machine:

```bash
$ cat /sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size
64
$ getconf LEVEL1_DCACHE_LINESIZE
64
```

`index0` is the L1 data cache, and `coherency_line_size` is the cacheline size. L1/L2/L3 are all 64 bytes (you can check `index1/2/3` one by one; on this machine all 64). This value is fixed for a given CPU generation, so performance articles often hard-code "64B," but **the first time you tune on an unfamiliar machine, confirm with `getconf` first.** Some low-power ARM cores or older Intel parts use 32-byte lines. This reminder sounds naggy until you've actually stepped in the 32B-line pit.

With the model clear, let's measure this 64 by behavior.

## Run it yourself: a stride scan to locate the 64-byte cliff

The idea is direct: pin the working set to a size "larger than L1, landing in L3" (so every miss has real cost), then vary only the stride and watch where throughput falls off a cliff. If the cliff lands exactly at stride = 64B, that's reverse-proof that "cache moves in units of 64 bytes."

The core is still sequential circular traversal, but now we sweep the stride finely:

```cpp
double stride_throughput(long elems, long stride_elem) {
    const long ACCESSES = 64'000'000;
    long mask = elems - 1;            // elems is a power of 2
    int sink = 0; long idx = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (long i = 0; i < ACCESSES; ++i) {
        sink += g_data[idx];
        idx = (idx + stride_elem) & mask;
    }
    do_not_optimize(sink);            // defeat DCE, same semantics as Google Benchmark DoNotOptimize
    auto t1 = std::chrono::steady_clock::now();
    double secs = std::chrono::duration<double>(t1 - t0).count();
    return (double)ACCESSES / secs / 1e6; // M accesses/sec
}
```

Working set 2 MB (`int` array, 512K elements), `taskset -c 0` pinned, sweep the stride:

```text
===== A. Stride scan (working set 2MB, in L3) =====
stride(B)     M accesses/sec   note
       4B       2017.5   < cacheline: multiple accesses to the same line get amortized
       8B       1956.3   < cacheline: multiple accesses to the same line get amortized
      16B       1986.7   < cacheline: multiple accesses to the same line get amortized
      32B       1962.3   < cacheline: multiple accesses to the same line get amortized
      48B       1820.1   < cacheline: multiple accesses to the same line get amortized
      56B       1543.5   < cacheline: multiple accesses to the same line get amortized
      64B       1422.1   = cacheline: each access exactly changes line   ← cliff
      72B       1181.8   > cacheline: every access hits a new line
      96B       1034.8   > cacheline: every access hits a new line
     128B        995.8   > cacheline: every access hits a new line
     256B       1324.2   > cacheline: every access hits a new line   ← anomalous bump, see below (suspected prefetcher)
     512B        528.7   > cacheline: every access hits a new line
```

How to read this table: watch "**stride = 64B is the dividing line**."

- **stride < 64B (4 through 32)**: several consecutive accesses land in the same 64-byte cacheline. The first access misses and pulls the whole line in; the following accesses all hit the line that's already been brought in, almost free. So throughput is high and flat (~2000 M/sec). The parts of this 64-byte line you didn't touch got a "free ride."
- **stride ≥ 64B (64, 72, 96…)**: every access steps into a **new** cacheline, the "free ride" is gone, and each one pays the cost of moving a line. Throughput drops accordingly to ~1000–1400 M/sec.

This drop from ~2000 to ~1000, landing exactly at stride = 64B, is our behavioral cross-check on "a cacheline is 64 bytes."

> I want to flag the 256B row specifically: it's actually higher than 128B (1324 vs 996), which looks "unscientific." **The most likely suspect is the hardware prefetcher**: a modern CPU's prefetcher can recognize a "fixed-stride" access stream (quite large strides are still within its learning window), pull subsequent lines in ahead of time, and partially hide the latency. **But this is mechanism-based speculation, not a measured verdict**: on this WSL2 machine I have no way to turn the prefetcher off for a control (disabling it requires writing to an MSR, which WSL2 can't reach), and there's no public documentation for the exact maximum trackable stride / stream count of the Zen prefetcher. The 256B row also catches a geometric dividend from "the same-array traversal period getting shorter, so warmup is more complete" (see `mask = elems-1` for the circular traversal in the code). At 512B the number keeps dropping, and the dominant factor there is more likely cacheline utilization (each access consumes only a small slice of a 64-byte line; see the bandwidth drop at large strides in 02-01). The takeaway: **when measuring cache behavior, the prefetcher is a recurring troublemaker variable**, and an "inexplicable bump" should make you suspect it first. But also admit that until you've run the control experiment, "suspicion" is not "proof" (this is exactly the "sounds-like-an-explanation" false-causality trap that vol6 ch00-01 keeps warning about).

## Spatial locality: contiguous layout is a "double win"

Translate the previous section's conclusion into a C++ design principle, and you get the one everybody has heard but maybe not thought through: **keep data contiguous.** Contiguous layout cashes in on a double dividend:

1. **Amortizing the cacheline load**: one 64-byte line comes in, and contiguous access uses every element in those 64 bytes, nothing wasted. For the same one miss, accessing 16 `int`s (64B) versus 1 `int` costs the same to move.
2. **Feeding the prefetcher**: the prefetcher loves "sequential, fixed-stride" streams. When you traverse a `vector`, it detects the stride = 4B stream and pulls the next few cachelines into cache ahead of time, so by the time you need them they already hit, **it even saves you the misses**.

That's why `std::vector` / `std::array` / native arrays fly when traversed, while `std::list` / `std::set` / `std::unordered_map` (chained buckets) crawl over their nodes: the latter's nodes are scattered across the heap, you can neither amortize (each node occupies a cacheline but you only read a small slice) nor can the prefetcher keep up (the next node's address is unpredictable).

This principle has one classic pitfall that every C++ programmer should step in once with their own feet: the traversal order of a 2D array.

## Row-major vs column-major: where does the 6x gap come from

2D arrays in C and C++ (whether native `a[N][N]` or simulated as 1D `a[i*N+j]`) are stored in memory in **row-major** order: store the first row, then the second. So `a[i][j]` and `a[i][j+1]` are adjacent in memory (4 bytes apart), but `a[i][j]` and `a[i+1][j]` are a whole row apart (`N*4` bytes).

What this means: when double-looping over a matrix, **the inner loop walking along a "row" is sequential access, walking along a "column" is a big-stride jump.** We measure with a 2048×2048 `int` matrix (16 MB, exactly this machine's L3 size, to amplify the gap):

```cpp
void walk_2d(int* a, int N, bool row_major) {
    volatile int sink = 0; int s = 0;
    auto t0 = std::chrono::steady_clock::now();
    if (row_major)
        for (int i = 0; i < N; ++i)
            for (int j = 0; j < N; ++j) s += a[i * N + j]; // along a row: sequential, stride=4B
    else
        for (int i = 0; i < N; ++i)
            for (int j = 0; j < N; ++j) s += a[j * N + i]; // along a column: jumping, stride=N*4B=8KB
    sink = s;
    auto t1 = std::chrono::steady_clock::now();
    /* print elapsed time and throughput */
}
```

```text
===== B. 2D traversal (N=2048, matrix 16 MB = L3 size) =====
   row-major:          1.0 ms,  16.7 GB/s
   column-major:       6.3 ms,   2.7 GB/s
```

**Same matrix, same amount of work, same L3 residency, column-major is more than 6x slower than row-major.**

Breaking it down: in row-major the inner `j` advances 4 bytes each step, fully sequential, one cacheline (64B) serves 16 `int` accesses, and the prefetcher pulls data ahead, throughput hits 16.7 GB/s. In column-major, the inner `j` makes the address jump `N*4 = 8192` bytes each step, every access steps into a brand-new cacheline, and an 8 KB stride is so large the prefetcher can't keep up (recall the 512B row's misery a section ago), so it degenerates to L3's random-access throughput, leaving only 2.7 GB/s.

That's the entire reason behind "the inner loop has to walk along the memory-contiguous direction." Anyone writing matrix multiply, image convolution, or grid simulations gets this direction backwards and gives away several times the performance for free, and the compiler **will not** flip it for you (it can't prove swapping loop order doesn't change the result; in most cases it actually doesn't, but the compiler doesn't dare).

> As an aside: this "swap loop order" trick is called **loop interchange**, one of the compiler optimizations ch04-02 will cover. Here just hold on to its physical motivation: make the innermost loop walk along the memory-contiguous direction.

## Struct field ordering: pack hot data into the same line

Cachelines also directly drive one C++-specific layout decision: **how to order the fields of a struct.** Look at these two:

```cpp
struct Bad {
    int    id;          // hot: queried every frame
    char   debug_tag;   // cold: only read when logging
    double values[6];   // hot: computed every frame
    void*  parent;      // cold: only used when walking the tree
};

struct Good {
    int    id;          // hot
    double values[6];   // hot    ← hot fields clustered
    char   debug_tag;   // cold
    void*  parent;      // cold    ← cold fields separated
};
```

`Bad` interleaves hot and cold fields; every time you touch a hot field during traversal, you incidentally drag the cold fields in the same cacheline into cache too, wasting precious cache space that could have held more hot objects. `Good` concentrates the hot fields so a single cacheline holds as much "data that will actually be used" as possible. This is called **hot/cold splitting**, and at heart it's about marshaling cachelines so every slot lands on the cutting edge.

There's a more aggressive version of this called **AoS → SoA** (Array of Structs to Struct of Arrays): when you have a ton of objects but only process one of their fields at a time (say, only updating positions in a physics sim), laying out "the same field contiguously" beats "the same object's fields adjacent" by a lot. This goes deeper and has more nuance than field ordering, so we save it for ch04-01 "backend memory bottlenecks." Here, just plant the idea.

> Boundary note: the **alignment, padding, `#pragma pack`** machinery behind "why `sizeof` ends up bigger than you'd think" (for example, the compiler inserts padding after a `char` to align a `double` to 8 bytes) belongs to ABI / layout rules, and vol4 will touch it when covering class layout. vol6 here only cares about the performance-side meaning of "hot-field clustering is cache-friendly."

## A thread left dangling: the cost of sharing a line (false sharing)

There's one more face of the cacheline we've deliberately left unfolded. Since "adjacent addresses share the same cacheline," what happens when two **unrelated** variables happen to be crammed into the same 64 bytes? They kick each other out of cache, and worse, under multithreading they trigger **false sharing**: thread A writes variable x, thread B writes variable y; even though x and y are logically unrelated, they're in the same cacheline, and the hardware coherence protocol repeatedly invalidates that line back and forth between the two cores. Performance collapses.

This doesn't show up on a single core. It's a multicore-only pitfall, and a deep one. So the full false-sharing measurement and the `alignas(64)` fix we save for ch05's multicore chapter. Here just bury the foreshadowing: the "sharing" of cachelines is a dividend on one core, and possibly a tax on many.

## Threads left for later

By the end of this article we've confirmed that the minimum unit of cache transfer is the **64-byte cacheline**, and seen its consequences through two measurements: the **stride scan** finds a throughput cliff exactly at stride = 64B (a behavioral proof), and **row-major vs column-major traversal differs by 6x** (the inner-loop direction decides whether your access is sequential or a big-stride jump), plus **hot-field clustering** making precious cachelines hold only data that gets used (AoS→SoA is saved for ch04-01), and the multicore dark side, **false sharing** (saved for ch05).

But program performance isn't determined only by data movement. The way the CPU executes instructions itself (pipelining, instruction-level parallelism, branch prediction) can make the same data run several times faster or slower. The next article steps into the CPU's execution core.

## References

- Agner Fog, *The microarchitecture of Intel, AMD and VIA CPUs*, §22.16 *Cache and memory access*: Zen-family cache parameters (64 B lines, associativity/sets at each level), hardware-prefetch behavior. Local copy: `.claude/drafts/books/optimazation_in_cpp/microarchitecture.md`
- Bryant & O'Hallaron, *CSAPP*, Chapter 6 *The Memory Hierarchy*: formal definitions of cachelines, spatial/temporal locality, and the memory mountain
- Drepper, U., *What Every Programmer Should Know About Memory*: engineering details of cachelines, alignment, and prefetching (classic long-form)
- Source for this article's measurements: `code/volumn_codes/vol6-performance/ch02/cacheline_locality.cpp`
