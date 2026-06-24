---
chapter: 1
cpp_standard:
- 11
- 17
description: Starting from the memory hierarchy, we break down the working mechanisms
  of cache lines, mapping policies, and the MESI coherence protocol, and arrive at
  cache-friendly programming practices and C++ cache line alignment tools.
difficulty: intermediate
order: 102
platform: host
prerequisites:
- 数据类型基础：整数与内存
- 指针与数组
- 结构体与内存布局
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
  source_hash: 090da146512d30536ab889f08679a2ea83f4a139a70eb8e727ac05e70de65891
  translated_at: '2026-06-24T00:29:37.333120+00:00'
  engine: anthropic
  token_count: 3582
---
# Cache Mechanisms and the Memory Hierarchy

If your program is running slowly, and you have already optimized the algorithm's time complexity to the limit, the bottleneck is likely not the CPU's inability to compute, but rather it waiting idly for data to be transferred from memory. There is a gap of several orders of magnitude between the computing speed of a modern CPU and the access speed of main memory. Without building a few bridges across this chasm, even the most powerful arithmetic units are helpless. These "bridges" are the protagonists of our discussion today: the Cache.

To be honest, many application-level developers may never touch the Cache directly. However, if you work in high-performance computing, game engines, embedded real-time systems, or database kernels, not understanding how the Cache works is essentially like optimizing blindly. I first gained a tangible sense of the Cache during a matrix traversal performance test—traversing the same two-dimensional array row-by-row versus column-by-row resulted in a speed difference of nearly three times. I was completely baffled at the time. Later, I realized it wasn't the compiler's fault, nor an algorithmic issue; it was purely the Cache working its magic behind the scenes.

Languages like Python and Java completely abstract away memory management, leaving programmers with little opportunity to perceive the existence of the Cache—the virtual machine and interpreter handle that worry for you. C is different; it exposes the bare metal of memory directly to you. How you layout data, how you traverse it, and how you align it are all up to you. Building on C, C++ provides a few additional standardized tools (such as `alignas` and `hardware_destructive_interference_size`), allowing us to work with the Cache in a portable way. In this article, we will dissect the Cache from the inside out: starting from the memory hierarchy, to cache lines, mapping policies, and coherence protocols, and finally landing on how to write code that makes the Cache "comfortable," and what tools in C++ can help us achieve this.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Understand the design motivation and characteristics of the memory hierarchy.
> - [ ] Explain the working principles of Cache Lines, mapping policies, and replacement policies.
> - [ ] Understand the basic state transitions of the MESI coherence protocol.
> - [ ] Write cache-friendly C code and verify it.
> - [ ] Use `alignas` and `hardware_destructive_interference_size` in C++ for cache line alignment.

## Environment Description

All code examples in this article can be compiled and run on a standard x86-64 platform. The timing results for the stride experiment and matrix traversal depend on the specific CPU model and cache configuration, but the trends remain consistent.

```text
平台：x86-64 Linux / macOS / Windows (MSVC/MinGW)
编译器：GCC >= 9 或 Clang >= 12
标准：-std=c11（C 部分）/ -std=c++17（C++ 对比部分）
编译选项：-O2（避免过度优化消除循环，同时排除 debug 模式的额外开销）
依赖：无
```

## Step 1 — Understanding Memory from the CPU's Perspective

Let's start by looking at the memory system from the CPU's point of view. Inside the CPU, we have a set of registers that run at the same frequency as the CPU and can be accessed in a single clock cycle. However, registers are expensive; x86-64 only has 16 general-purpose registers, so the amount of data they can hold is extremely limited.

Moving outward, we find the L1 Cache, usually split into instruction cache (L1I) and data cache (L1D), with sizes ranging from 32KB to 64KB and access latencies of roughly three to four clock cycles. Next is the L2 Cache, typically 256KB to 1MB, with a latency of about 10 to 14 cycles. Beyond that is the L3 Cache, ranging from a few MB to tens of MB (or even over a hundred MB on servers), with latencies of 30 to 50 cycles. L3 is usually shared among all cores, while L1 and L2 are private to each core. Further out lies main memory (DRAM), with a latency of roughly 100 to 300 cycles. If data resides on disk (SSD or HDD), latency jumps to the microsecond or even millisecond range.

We can use a rough time scale to build intuition: if a register access takes one second, then L1 is about three seconds, L2 is 10 seconds, L3 is 30 seconds, main memory is three minutes, an SSD is about two days, and an HDD is about half a year. The gap between levels is exponential—this is why even a one percent increase in cache hit rate can yield significant performance gains.

The core design principle of this pyramid structure is called the **Principle of Locality**. Locality comes in two forms: **Temporal Locality** means that if a piece of data was just accessed, it is likely to be accessed again soon; **Spatial Locality** means that if a piece of data is accessed, data at nearby addresses is likely to be accessed as well. All cache design decisions—cache line size, prefetching strategies, replacement policies—revolve around these two forms of locality. We can use a simple diagram to visualize this pyramid:

![Memory Hierarchy Pyramid Diagram](./02-memory-hierarchy.drawio)

You can check your machine's cache configuration on Linux using the `lscpu` command; the lines labeled `L1d cache`, `L2 cache`, and `L3 cache` reflect your CPU's actual specifications. Next, we will break down each layer.

## Step 2 — Understanding the Cache Line, the Minimum Unit of Transfer

Now we understand that data is not exchanged between cache and main memory byte-by-byte, but rather in chunks called **Cache Lines**. On x86, a cache line is typically 64 bytes, while on ARM it can be 32 bytes (though modern ARM64 has largely standardized on 64 bytes as well). This means that even if you only read a single `int` (4 bytes), the cache controller will pull the entire cache line (64 bytes) containing that `int` from main memory.

The motivation for this design is straightforward—since we have spatial locality, we might as well move a larger chunk at once; what if the next data you need is right next door? Most program access patterns indeed exhibit good spatial locality, so this strategy pays off statistically.

We can write a simple C program to intuitively feel the existence of cache lines. This program traverses the same array with different strides (steps) to observe the changes in execution time:

```c
#define _POSIX_C_SOURCE 199309L  // 启用 clock_gettime / CLOCK_MONOTONIC
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define kArraySize (64 * 1024 * 1024)  // 64M 个 int

int main(void)
{
    int* arr = (int*)malloc(kArraySize * sizeof(int));
    // 先预热，确保数据在 Cache 里
    for (int i = 0; i < kArraySize; i++) {
        arr[i] = i;
    }

    // 以不同步长遍历，只做读操作
    volatile int sink = 0;  // 防止 sum 被"死代码消除"优化掉
    for (int stride = 1; stride <= 4096; stride *= 2) {
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);  // 记录墙上时间起点
        int sum = 0;
        for (int i = 0; i < kArraySize; i += stride) {
            sum += arr[i];
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);  // 记录墙上时间终点
        sink = sum;  // 强制编译器真的去算 sum

        double total_ms = (t1.tv_sec - t0.tv_sec) * 1000.0
                        + (t1.tv_nsec - t0.tv_nsec) / 1e6;
        long accesses = kArraySize / stride;  // 注意：步长翻倍，访问次数减半
        double ns_per_access = total_ms * 1e6 / accesses;
        printf("stride=%5d  accesses=%9ld  total=%7.3f ms  per_access=%6.2f ns\n",
               stride, accesses, total_ms, ns_per_access);
    }

    free(arr);
    return 0;
}
```

After compiling and running, we observe an interesting phenomenon:

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

Don't rush to look at the `total` column yet—it represents the total time to scan the entire array from start to finish. Since our loop increments with `i += stride`, doubling the stride halves the number of accesses: stride=1 requires 67 million accesses, while stride=4096 only requires 16,000 accesses—a difference of over four thousand times. Therefore, the `total` column is dominated by the "number of accesses" and drops accordingly (from 20ms down to 0.1ms). It fails to reflect the existence of the Cache—changing machines or resizing the array will cause these absolute values to fluctuate, making them incomparable.

What we should really focus on is `per_access`—**the average nanoseconds spent per memory access**. This eliminates the confounding variable of "access count," leaving only the pure overhead of a single access. This is where the shadow of the Cache becomes visible. You will notice that this curve has three distinct segments:

- **stride 1 → 16**: `per_access` gradually climbs from 0.31ns to 2.45ns. 16 `int`s happen to be 64 bytes, exactly one cache line. Within this segment, adjacent accesses still land within the same cache line—once the line is fetched, the data inside is effectively free in the Cache. Combined with hardware prefetching secretly moving data in advance, the per-access cost is suppressed to the sub-nanosecond level.
- **stride exceeds 16**: We start crossing cache line boundaries, and `per_access` accelerates upward, reaching 6.6ns at stride=512. At this point, every step basically requires waiting for a new cache line to be transferred from L2/L3 or even main memory, and prefetching cannot keep up with such a large stride.
- **stride reaches 1024 and above**: The stride is now ≥ 4KB, crossing even page boundaries. Accesses are so sparse that the Cache can't hold them at all. `per_access` climbs to 7~10ns, basically representing a cold access every time, approaching the latency magnitude of a DRAM access.

This demonstrates the effect of the cache line as the minimum unit of transfer—**as long as accesses stay within a single 64-byte cache line, single memory accesses are dirt cheap at sub-nanosecond speeds; once you step out of that line, every step pays the price of transferring an entire cache line.**

> **Pitfall Warning**
>
> There are a few points where this experiment can easily go wrong, so let's go through them one by one:
>
> - **Make sure to look at the average time per access; don't be fooled by the total time.** If you compare the "total time to scan the whole array," a larger stride means fewer accesses, so the total time naturally gets shorter—but this has nothing to do with Cache, it's purely "doing less work." That's why the code specifically calculates `per_access = total time ÷ access count` to remove the confounding variable of access count, allowing us to see the change in Cache hit rate. (This was a pitfall in an earlier version of this tutorial; thanks to the readers who pointed it out in the issues.)
> - **Don't let the compiler optimize away the loop.** `-O0` makes the loop overhead overshadow the Cache differences, while `-O3` might be aggressive enough to fold the entire loop into a constant expression. The `volatile int sink = sum;` in the code serves this purpose—since `sum` is calculated but never used, the compiler will judge it as "dead code" and delete it. We use a `volatile` sink to force it to complete the calculation honestly.
> - **Use wall-clock time for timing, don't use `clock()`.** `clock()` measures the CPU time consumed by the process, not the actual elapsed wall-clock time; memory benchmarks should use `clock_gettime(CLOCK_MONOTONIC, ...)`. This requires `#define _POSIX_C_SOURCE 199309L` (or compiling directly with `-std=gnu11`), otherwise it will report an "implicit declaration" under strict `-std=c11`.

## Step 3 — Understand where a cache line is placed

Now we know that data is moved in cache lines, but where in the Cache is it placed after being fetched? This involves mapping policies.

The most intuitive idea is **Direct Mapped**: each cache line in main memory can only be placed in one fixed location in the Cache. The location is determined by the address modulo operation. This is like seats in a classroom—each student ID corresponds to a fixed seat. The benefit is fast lookup; we can determine presence in O(1). The downside is that if two frequently accessed cache lines happen to map to the same location, they will constantly kick each other out, causing so-called "thrashing."

The other extreme is **Fully Associative**: any cache line can be placed in any location in the Cache. Lookup requires comparing the address tag against all cache lines simultaneously, which is very expensive in hardware, so it is only used in very small caches (like the TLB).

In practice, a compromise is used—**Set Associative**. The Cache is divided into several sets, each containing N cache lines (N is the "number of ways," or N-way set associative). A main memory cache line can only be placed in its corresponding set, but there are N positions to choose from within that set. Modern CPUs usually have L1 caches that are 4-way or 8-way set associative, while L3 might be 12-way or even 16-way. Set associative achieves a good balance between hardware complexity and the risk of thrashing.

What happens when a set is full? This requires a **replacement policy**. The most common replacement policy is LRU (Least Recently Used), which kicks out the line that hasn't been accessed for the longest time. However, implementing precise LRU in hardware is too costly, so many CPUs use approximate algorithms like Pseudo-LRU. For us programmers, knowing that "recently used data stays in the Cache" is sufficient; we don't need to delve into the hardware approximation details.

You can use the `getconf` command on Linux to quickly confirm your CPU's cache line size:

```text
$ getconf LEVEL1_ICACHE_LINESIZE
64
$ getconf LEVEL1_DCACHE_LINESIZE
64
```

If you see 64, that indicates a standard 64-byte cache line. If it is 128, your CPU likely uses larger cache lines (some server chips do this), and you will need to adjust the alignment parameters accordingly.

> **Warning**
> If you find that a loop iterating over an array performs inexplicably poorly, and the array size happens to be a power of two, it is likely due to address conflict thrashing caused by direct mapping. A simple fix is to allocate some extra padding for the array to disrupt that "perfect modulo conflict" pattern. These issues are very subtle in high-performance code because, from the code's perspective, everything looks perfectly fine.

## Step 4 — Understanding Data Consistency Between Cores

Things are still fairly simple on a single core — data is either in the cache or it isn't. But in a multi-core system, each core has its own L1 and L2. If core A modifies a cache line in its own cache while core B still holds the old data for that same address, chaos would ensue.

This is the problem that **Cache Coherency Protocols** solve. The most widely used protocol on x86 is MESI (ARM uses a variant called MOESI). The name MESI comes from the four states of a cache line:

- **M (Modified)**: This data has been modified and differs from main memory. Only this core currently holds the latest version.
- **E (Exclusive)**: This data matches main memory, and only the current core holds a copy. If you want to modify it, you don't need to notify anyone else.
- **S (Shared)**: This data matches main memory, but multiple cores might hold copies. It can be read, but not written to directly.
- **I (Invalid)**: This cache line is invalid, effectively empty.

Let's walk through a specific example. Suppose core A and core B both read data from the same address. At this point, the cache lines on both cores are in the **S** state. Now core A wants to write to this address — it needs to first issue an "invalidate" broadcast, telling the other cores: "If you are holding data for this address, invalidate it immediately." Upon receiving this notification, core B changes its copy to the **I** state, while core A's copy transitions to the **M** state. After that, core A can safely modify the data. If core B then wants to read this address, it sees it is in the **I** state, triggering a Cache Miss. It then fetches the latest data from core A via the bus (and writes it back to main memory), and the states on both sides become **S** or **E** depending on the situation.

This mechanism ensures that all cores always see consistent data, but it has a side effect — **False Sharing**. If two cores are modifying different variables that reside on the same cache line (for example, two `int`s packed tightly together in a struct), they are logically independent. However, at the hardware level, they are contending for the same cache line. The MESI protocol will constantly trigger invalidations and synchronizations, causing performance to plummet. This is a classic problem in multi-threaded programming, and we will see later how to use cache line alignment to avoid it.

> **Warning**
> False sharing is completely invisible in single-threaded tests; it only manifests as performance degradation under high multi-threaded concurrency. Furthermore, the degradation is proportional to the number of threads — the more threads, the more frequent invalidate broadcasts on the bus. The standard way to investigate this is using the `perf` tool to observe cache miss events (`perf stat -e cache-misses,cache-references`). If the cache miss rate spikes abnormally in the multi-threaded version, false sharing is likely the culprit.

## Step 5 — Writing Cache-Friendly Code

Enough theory; let's get practical. The core of cache-friendly programming can be summed up in one sentence: **Make data access patterns align with how the Cache works**, which means maximizing spatial locality and temporal locality.

### Row-Major vs. Column-Major Traversal

The most classic example is traversing a two-dimensional array. In C, two-dimensional arrays are stored in **row-major** order, meaning `matrix[0][0]`, `matrix[0][1]`, `matrix[0][2]`, etc., are contiguous in memory. If we traverse by row, the access order matches the memory layout, maximizing spatial locality. If we traverse by column, we skip an entire row with each access, which means we likely need to reload the cache line every time.

```c
#define kRows 1024
#define kCols 1024

static int matrix[kRows][kCols];

// 缓存友好：按行遍历
void sum_by_rows(int* total)
{
    int sum = 0;
    for (int i = 0; i < kRows; i++) {
        for (int j = 0; j < kCols; j++) {
            sum += matrix[i][j];  // 连续访问，Cache 命中率高
        }
    }
    *total = sum;
}

// 缓存不友好：按列遍历
void sum_by_cols(int* total)
{
    int sum = 0;
    for (int j = 0; j < kCols; j++) {
        for (int i = 0; i < kRows; i++) {
            sum += matrix[i][j];  // 每次跳跃 sizeof(int)*kCols 字节
        }
    }
    *total = sum;
}
```

Here are the test results obtained by the author (i7-12700H, L3 24MB):

```text
$ gcc -O2 -std=c11 matrix_sum.c -o matrix_sum && ./matrix_sum
sum_by_rows: 1048576, time=1.234 ms
sum_by_cols: 1048576, time=5.678 ms
按行遍历比按列遍历快约 4.6 倍
```

`sum_by_rows` is typically three to six times faster than `sum_by_cols` (depending on the matrix size and cache capacity). The principle is simple: when traversing by row, after loading a single cache line, we can continuously process 16 integers (64 bytes / 4 bytes); when traversing by column, only 4 bytes of each cache line are used before it is evicted.

### Struct Layout—Put Hot Data First

Another common optimization point is the arrangement of struct fields. If a struct has dozens of fields, but only three or four are used on the hot path, these fields should be placed contiguously so they can share the same cache line:

```c
typedef struct {
    // 热路径字段——频繁访问，放一起
    int x;
    int y;
    int z;
    // 冷字段——不常访问
    char name[64];
    int id;
    double metadata[8];
} Particle;

// 反面教材：冷热数据混排
typedef struct {
    int x;
    char name[64];  // 冷数据插在热数据中间
    int y;
    int id;          // 冷数据
    int z;
    double metadata[8];
} ParticleBadLayout;
```

We can use `sizeof` to verify the difference in layout. The three fields `x`, `y`, and `z` in `Particle` are tightly packed, totaling 12 bytes, and are contiguous within a cache line. In `ParticleBadLayout`, however, `y` and `z` are separated by `name` and `id`. If we iterate through an array of particles and read only the coordinates, after loading `x`, we have to skip the 64-byte `name` field to reach `y`, which likely requires loading a new cache line—this is the cost of mixing hot and cold data.

If `x`, `y`, and `z` reside in the same cache line (they occupy only 12 bytes total, easily fitting into a single 64-byte cache line), a single cache load fetches all of them. If they are scattered throughout the structure, accessing `z` might require loading a new cache line every time. This concept of separating hot and cold data is very common in high-performance code. The ECS (Entity Component System) architecture in game engines essentially does this—storing frequently accessed position and velocity data contiguously, while moving less frequently used data like names and model IDs to separate arrays.

### Data-Oriented Design — SoA vs AoS

Extending this logic further, if we have a collection of objects of the same type, there are two ways to organize them: AoS (Array of Structures) and SoA (Structure of Arrays).

AoS is the most common approach—an array of structures where each element is a complete structure:

```c
typedef struct {
    float x, y, z;
    float r, g, b;
} Vertex;

Vertex vertices[10000];
```

SoA splits the data into multiple independent arrays:

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

Let's compare the differences in their memory layouts:

![AoS Memory Layout](./02-aos-layout.drawio)

![SoA Memory Layout](./02-soa-layout.drawio)

If our hot path only processes the coordinates `x`, `y`, and `z`, without touching the colors `r`, `g`, and `b`, the advantage of SoA becomes very obvious. As we iterate sequentially through `x[0]`, `x[1]`, `x[2]`, and so on, the data is completely contiguous in memory, resulting in a Cache hit rate approaching 100%. In the AoS scenario, accessing every `x` inadvertently pulls `y`, `z`, `r`, `g`, and `b` from the same structure into the Cache (because they reside on the same cache line). Since we don't need the color data at that moment, this cache space is wasted.

Of course, SoA isn't a silver bullet. If your access pattern requires all fields simultaneously, AoS offers better spatial locality. The choice depends entirely on your access pattern—there is no silver bullet, only trade-offs.

## C++ Connections — From C Understanding to C++ Tools

Everything we discussed earlier—cache lines, locality, false sharing—exists at the hardware level and is language-agnostic. However, C++ provides us with standard-level tools to better cooperate with the Cache, which C does not offer.

### `std::hardware_destructive_interference_size` (C++17)

C++17 introduced a compile-time constant, `std::hardware_destructive_interference_size`. Its value equals the minimum offset between two concurrently accessed cache lines on the target platform—on x86, this is 64. While the name is quite a mouthful, its purpose is straightforward: using this value for `alignas` ensures that two variables are not placed on the same cache line, thereby avoiding false sharing:

```cpp
#include <new>  // hardware_destructive_interference_size

struct alignas(std::hardware_destructive_interference_size) PaddedCounter {
    int value;
};

// 两个计数器各自独占一条缓存行
PaddedCounter counter_a;
PaddedCounter counter_b;
```

After doing this, `counter_a` and `counter_b` will not share a cache line, even if they are adjacent in memory. Thread A modifying `counter_a` will not cause Thread B's cache line to invalidate — this is the standard solution to the false sharing problem we discussed earlier in the MESI section.

In C, we are forced to hardcode `__attribute__((aligned(64)))` (GCC/Clang) or `__declspec(align(64))` (MSVC), as there is no portable means to obtain this value. This constant in C++17 provides portability in theory — although in practice, mainstream compilers return 64 on all supported platforms.

### `alignas` and Cache Line Alignment

C++11 introduced the `alignas` keyword, allowing us to specify alignment requirements for variables or types. Combined with the cache line size, we can manually ensure that certain critical data structures do not span cache lines:

```cpp
// C++ 风格的缓存行对齐
struct alignas(64) CacheLineAligned {
    int hot_data[4];    // 16 字节
    // 剩余 48 字节是 padding，编译器自动填充
};

static_assert(sizeof(CacheLineAligned) == 64,
              "Should be exactly one cache line");
```

This `static_assert` is quite useful—if someone adds too many fields to the struct and exceeds 64 bytes, the compiler will immediately report an error. Compile-time checks are far superior to discovering performance degradation at runtime.

### Impact of Data Structure Layout on Cache

C++ standard library containers are also designed with caching factors in mind. `std::vector` stores data contiguously, making traversal extremely cache-friendly. `std::list` allocates each node independently, potentially scattering them throughout memory, making traversal a nightmare for the cache. This is why `std::vector` is the default container in many modern C++ coding standards, while `std::list` is rarely recommended—not because list's time complexity is poor (insertion and deletion are indeed O(1)), but because its cache hit rate is abysmal, resulting in a ridiculously large constant factor. `std::deque` represents a compromise—it stores data in fixed-size blocks, making it significantly better than list, but still falling short of vector. If you are working in a performance-sensitive scenario, the primary consideration for container selection is often not time complexity, but the impact of memory layout on the cache.

## Exercises

1. **Stride Experiment Verification**: Modify the stride test code from this article to shrink the array to 4 MB (which should fit into most CPUs' L3 cache, avoiding interference from main memory latency), and focus on the `per_access` column. Observe the change in single-access latency as the stride increases from one to 32. Think about it: why does `per_access` only start to rise significantly after the stride exceeds 16 (a cache line boundary)? Can the byte count corresponding to this inflection point be used to deduce the cache line size of your machine?

2. **Reproduce False Sharing**: Write a multi-threaded program (using pthreads or C++ `<thread>`) that creates two threads, each incrementing a different field in a shared struct one hundred million times. First, run it without alignment, then use `alignas(64)` to align the two fields to different cache lines and run it again. Compare the execution times.

3. **Matrix Transpose Optimization**: Implement a square matrix transpose function. First, write a naive double-loop version, then try blocking—split the matrix into 32x32 small blocks and perform the transpose within each block. Compare the performance difference of the two versions on a large matrix (2048x2048).

4. **AoS vs SoA Benchmark**: Define a particle struct containing `float x, y, z, r, g, b`, and create one hundred thousand particles. Implement "normalize all particle coordinates to the unit sphere" using both AoS and SoA layouts, and compare the execution times.

5. **Cache-Friendly Linked List**: Reference the design philosophy of the Linux kernel's `list_head` to implement an intrusive doubly linked list. Store the node data domain and the linked list pointer domain separately so that traversing the linked list pointers does not require loading the entire node data, thereby improving cache hit rates.

## References

- [cppreference: `std::hardware_destructive_interference_size`](https://en.cppreference.com/w/cpp/thread/hardware_destructive_interference_size)
- [cppreference: `alignas` specifier](https://en.cppreference.com/w/cpp/language/alignas)
- [Ulrich Drepper: What Every Programmer Should Know About Memory](https://people.freebsd.org/~lstewart/articles/cpumemory.pdf)
- [Gustavo Duarte: Cache: a place for concealment](https://manybutfinite.com/post/intel-cpu-caches/)
