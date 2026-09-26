---
chapter: 1
cpp_standard:
- 11
- 17
description: Starting from the memory hierarchy, break down how cache lines, mapping
  policies, and the MESI coherence protocol work, and land on cache-friendly programming
  practices and C++ cache line alignment tools
difficulty: intermediate
order: 102
platform: host
prerequisites:
- 'Data Type Basics: Integers and Memory'
- Pointers, Arrays, const, and Null Pointers
- Structures and Memory Alignment
reading_time_minutes: 20
tags:
- host
- cpp-modern
- intermediate
- 优化
- 内存管理
title: Cache Mechanisms and Memory Hierarchy
translation:
  source: documents/vol1-fundamentals/c_tutorials/advanced_feature/02-cache-and-memory-hierarchy.md
  source_hash: ad0f5a1e5cc533ff793721011601acc886b004550280ea29645852f43efe333c
  translated_at: '2026-09-25T13:40:17+00:00'
  engine: anthropic
  token_count: 4800
---
# Cache Mechanisms and Memory Hierarchy

If your program is running slowly and you have already squeezed the time complexity as far as the algorithm allows, the bottleneck probably isn't that the CPU can't compute fast enough—it's that the CPU is sitting there idle, waiting for data to arrive from memory. Modern CPUs outpace main memory access by several orders of magnitude; unless we build a few bridges across that chasm, even the mightiest arithmetic units can only gaze across and sigh. Those "bridges" are the protagonists of today's discussion: the cache.

To be honest, plenty of application-layer developers can go their whole careers without ever touching the cache. But if you work on high-performance computing, game engines, embedded real-time systems, or database kernels, optimizing without understanding how the cache works is basically doing it blindfolded. My own first visceral encounter with the cache came during a matrix traversal benchmark: traversing the very same two-dimensional array row by row versus column by column differed by nearly three times in speed, and I was completely dumbfounded at the time. It took me a while to work out that this was neither the compiler's fault nor an algorithmic problem—it was purely the cache pulling strings behind the scenes.

Languages like Python and Java abstract memory management away entirely, so programmers barely get a chance to sense the cache's existence—the virtual machine and the interpreter take that worry off your hands. C is different: it hands you the bare metal of memory directly, and how you lay out data, how you traverse it, and how you align it are all up to you. Building on C, C++ adds a few standardized tools (such as `alignas` and `hardware_destructive_interference_size`) that let us cooperate with the cache in a portable way. In this article we'll take the cache apart from top to bottom: starting from the memory hierarchy, through cache lines, mapping policies, and coherence protocols, and finally landing on how to write code that keeps the cache "comfortable", and which tools in C++ can help us do it.

All code examples in this article compile and run on an ordinary x86-64 platform. The timing results of the stride experiment and the matrix traversal depend on the specific CPU model and cache configuration, but the trends are consistent.

```text
Platform: x86-64 Linux / macOS / Windows (MSVC/MinGW)
Compiler: GCC >= 9 or Clang >= 12
Standard: -std=c11 (C parts) / -std=c++17 (C++ comparison parts)
Compile flags: -O2 (avoids over-optimization eliminating the loops, while excluding the extra overhead of debug builds)
Dependencies: none
```

## Step 1 — See What Storage Looks Like from the CPU's Perspective

Let's first take a look at the whole storage system from the CPU's point of view. Inside the CPU sits a set of registers that run at the CPU's own frequency—one clock cycle is all it takes to access one. But registers are precious real estate: x86-64 has only 16 general-purpose registers, so the amount they can hold is extremely limited.

One layer out is the L1 cache, usually split into an instruction cache (L1I) and a data cache (L1D), between 32KB and 64KB in size, with an access latency of roughly 3-4 clock cycles. Further out is the L2 cache, typically 256KB to 1MB, with a latency of about 10-14 cycles. Beyond that lies the L3 cache, anywhere from a few MB to a few dozen MB (it can even exceed a hundred MB on servers), with a latency of 30-50 cycles. L3 is usually shared by all cores, while L1 and L2 are private to each core. Further out still is main memory (DRAM), with a latency of roughly 100-300 cycles. And if the data lives on disk (SSD or HDD), latency jumps to the microsecond or even millisecond range.

Here is a crude time scale for building intuition: if a register access took one second, then L1 would be about 3 seconds, L2 10 seconds, L3 30 seconds, main memory 3 minutes, an SSD about 2 days, and an HDD about half a year. The gaps between the levels are exponential—which is why even a 1% improvement in cache hit rate can bring a respectable performance gain.

The core design idea behind this pyramid is called the **principle of locality**. Locality comes in two forms: **temporal locality** means that if a piece of data was just accessed, it is likely to be accessed again soon; **spatial locality** means that if a piece of data is accessed, the data at nearby addresses is likely to be accessed too. Every cache design decision—cache line size, prefetching policy, replacement policy—revolves around these two forms of locality. Here is a rough sketch to make the pyramid tangible:

![Schematic of the memory hierarchy pyramid](./02-memory-hierarchy.drawio)

On Linux, the `lscpu` command shows your machine's cache configuration—the `L1d cache`, `L2 cache`, and `L3 cache` lines report what your CPU actually has. Now let's peel it apart layer by layer.

## Step 2 — Understand the Cache Line, the Minimum Unit of Transfer

We now know that data is not exchanged between the cache and main memory byte by byte; it moves in units of **cache lines**. On x86 a cache line is usually 64 bytes; some ARM machines use 32-byte lines (though modern ARM64 has largely converged on 64 bytes as well). This means that even if you read only a single `int` (4 bytes), the cache controller pulls up the entire cache line containing that `int` (64 bytes) from main memory.

The motivation behind this design is straightforward—given that we have spatial locality, why not move a little more at a time? The next thing you touch may well be the neighboring data. Most programs' access patterns do exhibit quite good spatial locality, so this strategy pays off statistically.

We can write a short piece of C code to feel the cache line's presence directly. This program traverses the same array with different strides and observes how the timing changes:

```c
#define _POSIX_C_SOURCE 199309L  // Enable clock_gettime / CLOCK_MONOTONIC
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define kArraySize (64 * 1024 * 1024)  // 64M ints

int main(void)
{
    int* arr = (int*)malloc(kArraySize * sizeof(int));
    // Warm up first so the data is in the cache
    for (int i = 0; i < kArraySize; i++) {
        arr[i] = i;
    }

    // Traverse with different strides, reads only
    volatile int sink = 0;  // Prevent sum from being optimized away as dead code
    for (int stride = 1; stride <= 4096; stride *= 2) {
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);  // Wall-clock start
        int sum = 0;
        for (int i = 0; i < kArraySize; i += stride) {
            sum += arr[i];
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);  // Wall-clock end
        sink = sum;  // Force the compiler to actually compute sum

        double total_ms = (t1.tv_sec - t0.tv_sec) * 1000.0
                        + (t1.tv_nsec - t0.tv_nsec) / 1e6;
        long accesses = kArraySize / stride;  // Note: stride doubles, access count halves
        double ns_per_access = total_ms * 1e6 / accesses;
        printf("stride=%5d  accesses=%9ld  total=%7.3f ms  per_access=%6.2f ns\n",
               stride, accesses, total_ms, ns_per_access);
    }

    free(arr);
    return 0;
}
```

Compile and run it, and you'll see an interesting phenomenon:

```text
$ gcc -O2 -std=c11 stride_test.c -o stride_test && ./stride_test
stride=    1  accesses= 67108864  total= 20.518 ms  per_access=  0.31 ns
stride=    2  accesses= 33554432  total= 13.900 ms  per_access=  0.41 ns
stride=    4  accesses= 16777216  total= 12.080 ms  per_access=  0.72 ns
stride=    8  accesses=  8388608  total=  9.663 ms  per_access=  1.15 ns
stride=   16  accesses=  4194304  total= 10.263 ms  per_access=  2.45 ns
stride=   32  accesses=  2097152  total=  8.678 ms  per_access=  4.14 ns
stride=   64  accesses=  1048576  total=  4.679 ms  per_access=  4.46 ns
stride=  128  accesses=   524288  total=  2.733 ms  per_access=  5.21 ns
stride=  256  accesses=   262144  total=  1.409 ms  per_access=  5.38 ns
stride=  512  accesses=   131072  total=  0.866 ms  per_access=  6.61 ns
stride= 1024  accesses=    65536  total=  0.672 ms  per_access= 10.25 ns
stride= 2048  accesses=    32768  total=  0.304 ms  per_access=  9.27 ns
stride= 4096  accesses=    16384  total=  0.115 ms  per_access=  7.00 ns
```

Don't rush to look at the `total` column—it is "the total time to sweep the entire array from head to tail", and our loop goes `i += stride`, so every time the stride doubles, the access count halves: stride=1 makes 67 million accesses while stride=4096 makes only 16 thousand, a gap of more than four thousand times. So the `total` column is dominated by the "number of accesses" and keeps dropping (from 20ms down to 0.1ms); it simply cannot reveal the cache's presence—switch machines or grow or shrink the array, and the absolute values wobble along, with no comparability at all.

What you should really be watching is `per_access`—**the average nanoseconds each memory access is charged**. It divides out the "number of accesses" confounder, leaving the pure cost of a single memory access, and only there does the cache's shadow become visible. You'll notice the curve has three distinct segments:

- **stride 1 → 16**: `per_access` climbs slowly from 0.31ns to 2.45ns. Sixteen `int`s are exactly 64 bytes—one cache line—so throughout this segment consecutive accesses still nest inside the same cache line: once a line has been pulled up, everything in it is in the cache for free, and with hardware prefetching quietly moving data ahead behind your back, the per-access cost is pressed down to sub-nanosecond levels.
- **stride past 16**: accesses begin crossing cache line boundaries, and `per_access` rises markedly faster, already at 6.6ns by stride=512. At this point, every hop basically has to wait for a new cache line to arrive from L2/L3 or even main memory, and prefetching can't keep up with steps that large.
- **stride 1024 and up**: the stride is now ≥ 4KB—crossing page boundaries—and the accesses are sparse enough that the cache simply can't hold them. `per_access` climbs to 7-10ns, essentially a cold access every time, approaching the latency magnitude of a DRAM access.

That is the effect of the cache line as the minimum unit of transfer—**as long as your accesses stay within one 64-byte cache line, a single memory access is as cheap as sub-nanoseconds; the moment you step outside the line, every hop pays the price of hauling an entire cache line.**

This experiment has a few pitfalls that are particularly easy to stumble into; let's go through them one by one:

- **Always look at the average time per access; don't be fooled by the total time.** If you compare directly on "the total time to sweep the whole array", a larger stride means fewer accesses, so the total time of course keeps shrinking—but that has nothing to do with the cache; it is purely "less work done". That's why the code deliberately computes `per_access = total time ÷ access count`, dividing out the access-count confounder so you can see the change in cache hit rate. (This was a pitfall an earlier version of this tutorial stepped into—thanks to the reader who pointed it out in an issue.)
- **Don't let the compiler optimize the loop away.** `-O0` lets the loop's own overhead drown out the cache differences, while `-O3` can be aggressive enough to fold the entire loop into a constant expression. The `volatile int sink = sum;` in the code exists for exactly this purpose—once `sum` is computed, nobody uses it, so the compiler would deem it "dead code" and delete it outright; we use a `volatile` sink to force it to compute everything honestly.
- **Time with wall-clock time, not `clock()`.** `clock()` measures the CPU time consumed by the process, not the wall-clock time that actually elapsed; a memory benchmark should use `clock_gettime(CLOCK_MONOTONIC, ...)`. It requires `#define _POSIX_C_SOURCE 199309L` (or compiling with `-std=gnu11` directly), otherwise under strict `-std=c11` you'll get an "implicit declaration" error.

## Step 3 — Figure Out Where a Cache Line Gets Placed

We now know that data is moved in cache lines, but once a line is pulled up, where in the cache is it placed? That is where mapping policies come in.

The most straightforward idea is **direct mapped**: each cache line from main memory can sit in only one fixed position in the cache, determined by the address modulo the number of positions. It's like seats in a classroom—every student ID corresponds to one fixed seat. The upside is fast lookup: O(1) tells you whether it's a hit. The downside is that if two frequently accessed cache lines happen to map to the same position, they keep evicting each other, causing so-called "thrashing".

The opposite extreme is **fully associative**: any cache line can be placed anywhere in the cache. A lookup must compare against the tags of all cache lines simultaneously, which is very expensive in hardware, so it is used only in very small caches (the TLB, for instance).

What real hardware adopts is the middle ground—**set associative**. The cache is divided into a number of sets, each containing N cache lines (N is the "way count", as in N-way set associative). A cache line from main memory can only go into its corresponding set, but within the set there are N positions to choose from. On modern CPUs, L1 is typically 4-way or 8-way set associative, and L3 may be 12-way or even 16-way. Set associativity strikes a good balance between hardware complexity and thrashing risk.

What if a set is full? That's where the **replacement policy** comes in. The most common replacement policy is LRU (Least Recently Used)—evict the line that has gone the longest without being accessed. In practice, implementing exact LRU in hardware is too costly, so many CPUs use approximation algorithms such as pseudo-LRU. For us programmers, knowing that "recently used data stays in the cache" is enough; there's no need to dig into the hardware's approximation details.

You can quickly confirm your CPU's cache line size with the `getconf` command on Linux:

```text
$ getconf LEVEL1_ICACHE_LINESIZE
64
$ getconf LEVEL1_DCACHE_LINESIZE
64
```

If you see 64, you have the standard 64-byte cache line. If it's 128, your CPU may use a larger cache line (some server chips do this), and the alignment parameters later on need to be adjusted accordingly.

If you ever find a loop over an array inexplicably slow in performance, and the array size happens to be a power of two, it is very likely address-conflict thrashing caused by direct mapping. A simple fix is to allocate a little extra padding for the array, breaking the "addresses collide exactly on the modulo" pattern. This class of problem is extremely sneaky in high-performance code, because nothing looks wrong at the source level.

## Step 4 — Understand How Multiple Cores Keep Data Coherent

Things are simple enough on a single core—data is either in the cache or it isn't. But in a multicore system, every core has its own L1 and L2. If core A modifies a copy of a cache line in its own cache while core B's cache still holds stale data for the same address, wouldn't everything fall into chaos?

That is the problem **cache coherence protocols** exist to solve. The most widely used on x86 is the MESI protocol (ARM uses its variant, MOESI). MESI takes its name from the four states a cache line can be in:

- **M (Modified)**: The data has been modified and differs from what's in main memory. Only this one core currently holds the latest version.
- **E (Exclusive)**: The data matches main memory, and only the current core holds this copy. If you want to modify it, nobody needs to be notified.
- **S (Shared)**: The data matches main memory, but multiple cores may hold copies. It can only be read, not written directly.
- **I (Invalid)**: This cache line is invalid—effectively empty.

Let's walk through a concrete example. Suppose cores A and B have both read the data at the same address; at this point both cores' cache lines are in the S state. Now core A wants to write to this address—it first has to issue an "invalidate" broadcast, telling the other cores: "If you hold data for this address, consider it void." Core B receives the notice and flips its copy to the I state; core A's copy becomes M. After that, core A can modify the data with confidence. If core B then wants to read this address again, it finds itself in the I state, which triggers a cache miss; it then goes through the bus to fetch the latest data from core A (writing it back to main memory along the way), and the two sides' states then become S or E depending on the situation.

This machinery guarantees that all cores always see consistent data, but it has one side effect—**false sharing**. If two cores each modify different variables sitting on the same cache line (say, two adjacent `int`s in a struct), they don't interfere logically, but at the hardware level they are contending for the same cache line, and the MESI protocol keeps triggering invalidations and synchronizations—performance falls off a cliff. This problem is a classic in multithreaded programming; later we'll see how cache line alignment sidesteps it.

False sharing never shows up in single-threaded tests; it manifests only as performance degradation under multithreaded, high-concurrency load. And the degradation is proportional to the thread count—the more threads, the more frequent the invalidation broadcasts on the bus. The standard tool for tracking down this class of problem is `perf`, watching cache-miss events (`perf stat -e cache-misses,cache-references`). If the multithreaded version's cache misses shoot up abnormally, false sharing is most likely at work.

## Step 5 — Write Code That Keeps the Cache Comfortable

Enough theory—let's get practical. The core of cache-friendly programming boils down to one sentence: **make your data access patterns fit the way the cache works**, which means maximizing spatial locality and temporal locality.

### Traversal by Rows vs by Columns

The most classic example is traversing a two-dimensional array. In C, a 2D array is stored in **row-major** order, which means `matrix[0][0]`, `matrix[0][1]`, `matrix[0][2]`, ... are contiguous in memory. If we traverse by rows, the access order matches the memory layout and the cache's spatial locality is maxed out; if we traverse by columns, every access skips an entire row, and most likely a cache line has to be reloaded every time.

```c
#define kRows 1024
#define kCols 1024

static int matrix[kRows][kCols];

// Cache-friendly: traverse by rows
void sum_by_rows(int* total)
{
    int sum = 0;
    for (int i = 0; i < kRows; i++) {
        for (int j = 0; j < kCols; j++) {
            sum += matrix[i][j];  // Sequential access, high cache hit rate
        }
    }
    *total = sum;
}

// Cache-unfriendly: traverse by columns
void sum_by_cols(int* total)
{
    int sum = 0;
    for (int j = 0; j < kCols; j++) {
        for (int i = 0; i < kRows; i++) {
            sum += matrix[i][j];  // Each hop jumps sizeof(int)*kCols bytes
        }
    }
    *total = sum;
}
```

My test results are as follows (i7-12700H, L3 24MB):

```text
$ gcc -O2 -std=c11 matrix_sum.c -o matrix_sum && ./matrix_sum
sum_by_rows: 1048576, time=1.234 ms
sum_by_cols: 1048576, time=5.678 ms
按行遍历比按列遍历快约 4.6 倍
```

`sum_by_rows` is typically 3 to 6 times faster than `sum_by_cols` (depending on the matrix size and cache capacity). The principle is simple: traversing by rows, after loading one cache line you can process 16 consecutive ints (64 bytes / 4 bytes); traversing by columns, each cache line gets only 4 bytes used before being evicted.

### Structure Layout — Hot Data First

Another common optimization point is the arrangement of struct fields. If a struct has dozens of fields but the hot path uses only three or four of them, those fields should be placed right next to each other so they can share the same cache line:

```c
typedef struct {
    // Hot-path fields — accessed frequently, keep them together
    int x;
    int y;
    int z;
    // Cold fields — rarely accessed
    char name[64];
    int id;
    double metadata[8];
} Particle;

// Counter-example: hot and cold data interleaved
typedef struct {
    int x;
    char name[64];  // Cold data stuck between hot data
    int y;
    int id;          // Cold data
    int z;
    double metadata[8];
} ParticleBadLayout;
```

We can use `sizeof` to verify the difference in layout. In `Particle`, the three fields `x`, `y`, and `z` sit right next to each other—12 bytes in total, contiguous within a cache line. In `ParticleBadLayout`, `y` and `z` are separated by `name` and `id`; if you traverse an array of particles reading only the coordinates, then after loading `x` you must skip the 64-byte `name` to reach `y`, and most likely a new cache line has to be loaded—that is the price of interleaving hot and cold data.

If `x`, `y`, and `z` are in the same cache line (together they occupy only 12 bytes, sliding easily into a 64-byte cache line), then one cache load brings them all in. If they are scattered to every corner of the struct, every access to `z` may have to load another new cache line. This idea of separating hot from cold is very common in high-performance code; a game engine's ECS architecture is essentially doing exactly this—pulling the frequently accessed position and velocity data out on their own into contiguous storage, and tossing names, model IDs, and other rarely used things into another array.

### Data-Oriented Design — SoA vs AoS

Carrying this line of thinking one step further: given a set of objects of the same type, there are two ways to organize them—AoS (Array of Structures) and SoA (Structure of Arrays).

AoS is the way we usually write—an array of structs, where every element is a complete struct:

```c
typedef struct {
    float x, y, z;
    float r, g, b;
} Vertex;

Vertex vertices[10000];
```

SoA instead splits it into multiple independent arrays:

```c
typedef struct {
    float x[10000];
    float y[10000];
    float z[10000];
    float r[10000];
    float g[10000];
    float b[10000];
} VertexSoA;
```

Compare the difference between the two layouts in memory:

![AoS memory layout](./02-aos-layout.drawio)

![SoA memory layout](./02-soa-layout.drawio)

If your hot path processes only the coordinates `x`, `y`, `z` and never touches the colors `r`, `g`, `b`, then SoA's advantage is very clear—you traverse `x[0]`, `x[1]`, `x[2]`, ... consecutively, the data is fully contiguous in memory, and the cache hit rate approaches 100%. With AoS, every access to an `x` drags the same struct's `y`, `z`, `r`, `g`, `b` into the cache along with it (because they sit on the same cache line), but we don't need the color data for now, so that space is wasted.

Of course SoA is not a cure-all: if your access pattern needs all the fields at once, then AoS actually has better spatial locality. Which one to choose depends on your access pattern—there is no silver bullet, only trade-offs.

## Bridging to C++ — From C Insight to C++ Tools

Everything we've discussed so far—cache lines, locality, false sharing—is entirely a hardware matter, independent of language. But C++ gives us some tools at the standard level to cooperate with the cache better, which C does not have.

### `std::hardware_destructive_interference_size` (C++17)

C++17 introduced a compile-time constant, `std::hardware_destructive_interference_size`, whose value equals the minimum spacing between two concurrently accessed cache lines on the target platform—on x86, that's 64. The name is admittedly long, but its use is very direct: align with `alignas` using this value, and you can ensure two variables won't be placed on the same cache line, thereby avoiding false sharing:

```cpp
#include <new>  // hardware_destructive_interference_size

struct alignas(std::hardware_destructive_interference_size) PaddedCounter {
    int value;
};

// The two counters each own a cache line exclusively
PaddedCounter counter_a;
PaddedCounter counter_b;
```

With this done, `counter_a` and `counter_b` no longer share a cache line, even if they sit right next to each other in memory. Thread A modifying `counter_a` will not invalidate thread B's cache line—this is the standard solution to the false-sharing problem we discussed in the MESI section earlier.

In C, we can only hardcode `__attribute__((aligned(64)))` (GCC/Clang) or `__declspec(align(64))` (MSVC); there is no portable way to obtain this value. This C++17 constant at least provides portability in theory—although in practice mainstream compilers return 64 on all supported platforms.

### `alignas` and Cache Line Alignment

C++11 introduced the `alignas` keyword, letting us specify alignment requirements for variables or types. Combined with the cache line size, we can manually guarantee that certain critical data structures don't straddle cache lines:

```cpp
// C++-style cache line alignment
struct alignas(64) CacheLineAligned {
    int hot_data[4];    // 16 bytes
    // The remaining 48 bytes are padding, filled in automatically by the compiler
};

static_assert(sizeof(CacheLineAligned) == 64,
              "Should be exactly one cache line");
```

This `static_assert` is quite useful—if one day someone adds too many fields to the struct and pushes it past 64 bytes, the compile fails right there. Compared with discovering the performance degradation only at runtime, a compile-time check is a far better deal.

### How Data Structure Layout Affects the Cache

The containers in the C++ standard library were designed with cache factors in mind as well. `std::vector`'s data is stored contiguously, so traversal is extremely cache-friendly; every node of `std::list` is allocated independently and may be scattered all over memory, so traversing it is a cache nightmare. This is why in many modern C++ coding standards `std::vector` is the default container and `std::list` is hardly ever recommended—not because list's time complexity is bad (insertion and deletion really are O(1)), but because its cache hit rate is so poor that the constant factors are absurd. `std::deque` is a compromise—it stores data in chunks of fixed size, quite a bit better than list, but still a stretch behind vector. If you're working on performance-sensitive scenarios, the first consideration in choosing a container is often not time complexity but the impact of memory layout on the cache.

## Exercises

1. **Verify the stride experiment** (basic): modify this article's stride test code to shrink the array to 4MB (which basically fits into most CPUs' L3, avoiding interference from main-memory latency), and keep a close eye on the `per_access` column. Watch how the per-access time changes as the stride grows from 1 to 32—think about it: why does `per_access` start climbing noticeably only after the stride breaks past 16 (one cache line boundary)? Can you work backward from the byte count at this inflection point to deduce your machine's cache line size?

2. **Reproduce false sharing** (intermediate): write a multithreaded program (using pthreads or C++ `<thread>`) that creates two threads, each accumulating a different field of a shared struct up to one hundred million times. Run it once without alignment first, then run it again with the two fields aligned to different cache lines via `alignas(64)`, and compare the timings.

3. **Matrix transpose optimization** (intermediate): implement a square-matrix transpose function—first write the naive double-loop version, then try blocking—splitting the matrix into 32x32 tiles and transposing within each tile. Compare the performance difference between the two versions on a large matrix (2048x2048).

4. **AoS vs SoA benchmark** (basic): define a particle struct containing `float x, y, z, r, g, b`, and create a hundred thousand particles. Implement "normalizing all particles' coordinates into the unit sphere" with both the AoS and SoA layouts, and compare the timings.

(An earlier version of this article also had an exercise, "a cache-friendly intrusive linked list"; since it depends on prerequisite knowledge of intrusive containers and is only loosely coupled to the cache topic, it has been moved out—it fits better in the linked-list article of Advanced Topics 06.)

## References

- [cppreference: `std::hardware_destructive_interference_size`](https://en.cppreference.com/w/cpp/thread/hardware_destructive_interference_size)
- [cppreference: the `alignas` specifier](https://en.cppreference.com/w/cpp/language/alignas)
- [Ulrich Drepper: What Every Programmer Should Know About Memory](https://people.freebsd.org/~lstewart/articles/cpumemory.pdf)
- [Gustavo Duarte: Cache: a place for concealment](https://manybutfinite.com/post/intel-cpu-caches/)
