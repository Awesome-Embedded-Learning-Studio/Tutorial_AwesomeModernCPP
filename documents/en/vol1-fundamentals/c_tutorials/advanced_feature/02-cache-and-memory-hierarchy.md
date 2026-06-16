---
chapter: 1
cpp_standard:
- 11
- 17
description: 从内存层次结构出发，拆解缓存行、映射策略、MESI 一致性协议的工作机制，落到缓存友好编程实践和 C++ 的缓存行对齐工具
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
  source_hash: ece49e10f9b57cc8977765c189bf1d20f45a91c0aaf6fdde4949215461df141c
  translated_at: '2026-06-16T03:37:46.702085+00:00'
  engine: anthropic
  token_count: 3041
---
# Cache Mechanisms and the Memory Hierarchy

If your program is running slowly, and you have already optimized the algorithmic time complexity to the limit, the bottleneck is likely not that the CPU cannot calculate fast enough, but that it is waiting for data to be transferred from memory. There is an orders-of-magnitude gap between the computing speed of modern CPUs and the access speed of main memory. Without building a few bridges across this chasm, even the most powerful arithmetic units are helpless. These "bridges" are the protagonists of our discussion today: the Cache.

To be honest, many application-level developers never touch the Cache directly. However, if you work in high-performance computing, game engines, embedded real-time systems, or database kernels, not understanding how the Cache works is like optimizing with your eyes closed. My first realization of the Cache's impact came during a performance test of matrix traversal—traversing a two-dimensional array row-by-row was nearly three times faster than column-by-column. I was completely baffled at the time. Later, I understood that this wasn't the compiler's fault, nor an algorithmic issue, but purely the Cache working behind the scenes.

Languages like Python and Java abstract memory management completely, giving programmers little chance to perceive the Cache's existence—the VM and interpreter handle that worry for you. C is different; it exposes the bare metal of memory to you. How you arrange data, traverse it, and align it is entirely up to you. C++ goes a step further than C by providing standardized tools (like `std::hardware_destructive_interference_size` and `alignas`) that allow us to work with the Cache in a portable way. In this article, we will dissect the Cache from the ground up: starting with the memory hierarchy, moving to cache lines, mapping policies, and coherence protocols, and finally landing on how to write code that makes the Cache "comfortable," and which tools in C++ help us do this.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Understand the design motivation and characteristics of the memory hierarchy.
> - [ ] Explain the working principles of Cache Lines, mapping policies, and replacement policies.
> - [ ] Understand the basic state transitions of the MESI coherence protocol.
> - [ ] Write cache-friendly C code and verify it.
> - [ ] Use `std::hardware_destructive_interference_size` and `alignas` in C++ for cache line alignment.

## Environment Notes

All code examples in this article can be compiled and run on a standard x86-64 platform. The timing results for the stride experiment and matrix traversal depend on the specific CPU model and cache configuration, but the trends are consistent.

```text
OS: Linux 6.1.0
Compiler: GCC 13.2.0
Flags: -O2 -std=c++17
CPU: Intel Core i7-12700H (L1: 32KB, L2: 1.28MB, L3: 24MB)
```

## Step 1 — Understanding Storage from the CPU's Perspective

Let's start by looking at the entire storage hierarchy from the CPU's perspective. Inside the CPU, there is a set of registers. Their speed matches the CPU frequency, and they can be accessed in a single clock cycle. However, registers are expensive; x86-64 has only 16 general-purpose registers, capable of storing extremely limited amounts of data.

Moving outward, we have the L1 Cache, usually split into Instruction Cache (L1I) and Data Cache (L1D), ranging from 32KB to 64KB, with an access latency of about 3-4 clock cycles. Next is the L2 Cache, typically 256KB to 1MB, with a latency of around 10-14 cycles. Further out is the L3 Cache, ranging from a few MBs to tens of MBs (or even over 100MB on servers), with a latency of 30-50 cycles. L3 is usually shared among all cores, while L1 and L2 are private to each core. Beyond that lies main memory (DRAM), with a latency of roughly 100-300 cycles. If data is on disk (SSD or HDD), latency jumps to microseconds or even milliseconds.

You can use a rough time scale to build intuition: if register access takes 1 second, then L1 is about 3 seconds, L2 is 10 seconds, L3 is 30 seconds, main memory is 3 minutes, SSD is about 2 days, and HDD is about half a year. The gap between levels is exponential—this is why even a 1% increase in Cache hit rate can bring significant performance gains.

The core design philosophy of this pyramid structure is called the **Principle of Locality**. Locality comes in two types: **Temporal Locality** means that if a piece of data was just accessed, it is likely to be accessed again soon; **Spatial Locality** means that if a piece of data was accessed, data at nearby addresses is likely to be accessed as well. All Cache design decisions—cache line size, prefetch strategies, replacement policies—revolve around these two types of locality. We can use a simple diagram to visualize this pyramid:

![Memory Hierarchy Pyramid Diagram](./02-memory-hierarchy.drawio)

You can check your machine's Cache configuration on Linux using the `lscpu` command. The `L1d cache`, `L1i cache`, and `L3 cache` lines in the output show your CPU's actual specs. Let's break this down layer by layer.

## Step 2 — Understanding the Cache Line as the Minimum Transfer Unit

Now we know that data is not exchanged between Cache and main memory byte-by-byte, but in units called **Cache Lines**. On x86, a cache line is typically 64 bytes; on ARM, it can be 32 bytes (though modern ARM64 has largely standardized on 64 bytes as well). This means that even if you only read one `int` (4 bytes), the Cache controller will pull the entire cache line (64 bytes) containing that `int` from main memory.

The motivation for this design is straightforward—since we have spatial locality, we might as well move a larger chunk at once; what if the next piece of data you need is adjacent? Most program access patterns indeed exhibit good spatial locality, so this strategy is statistically a win.

We can write a simple C program to intuitively feel the existence of cache lines. This program traverses the same array with different strides and observes the time cost:

```cpp
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define ARRAY_SIZE (64 * 1024 * 1024) // 256MB, larger than L3 cache

int main() {
    int *arr = malloc(ARRAY_SIZE * sizeof(int));
    // Initialize array to avoid page faults during timing
    for (int i = 0; i < ARRAY_SIZE; i++) arr[i] = 0;

    clock_t start, end;

    // Test different strides
    for (int stride = 1; stride <= 64; stride *= 2) {
        start = clock();
        long long sum = 0;
        // Access every 'stride' elements
        for (int i = 0; i < ARRAY_SIZE; i += stride) {
            sum += arr[i];
        }
        end = clock();
        double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
        printf("Stride %2d: %.4f seconds, Sum: %lld\n", stride, elapsed, sum);
    }

    free(arr);
    return 0;
}
```

After compiling and running, you will see an interesting phenomenon:

```text
Stride  1: 0.0450 seconds, Sum: 0
Stride  2: 0.0225 seconds, Sum: 0
Stride  4: 0.0113 seconds, Sum: 0
Stride  8: 0.0057 seconds, Sum: 0
Stride 16: 0.0029 seconds, Sum: 0
Stride 32: 0.0152 seconds, Sum: 0
Stride 64: 0.0158 seconds, Sum: 0
```

As the stride grows from 1 to 16 (16 ints = 64 bytes, exactly one cache line), the time cost barely changes—because whether you access them one by one or skip a few, once a cache line is pulled up, all the data inside it is already in the Cache. However, once the stride exceeds 16 (crossing the cache line boundary), every access triggers a new Cache Line load, and the time cost rises significantly. This small experiment perfectly demonstrates the effect of the cache line as the minimum transfer unit.

> **Warning**
> When doing the stride experiment, make sure to add the `-O2` compiler option. With `-O0`, the loop overhead itself will mask the differences caused by the Cache; while `-O3` might sometimes be aggressive enough to optimize the entire loop into a constant expression, meaning you won't measure anything. If you find the time cost is the same for all strides, it's likely the compiler "ate" your loop. You can try using `volatile` to modify the array or insert a compiler barrier (`asm volatile("" ::: "memory")`) inside the loop.

## Step 3 — Figuring Out Where a Cache Line is Placed

Now we know data is moved in cache lines, but where in the Cache is it placed after being moved? This involves mapping policies.

The most intuitive idea is **Direct Mapped**: every cache line in main memory can only be placed in one specific location in the Cache, determined by the address modulo. This is like seats in a classroom—every student ID corresponds to a fixed seat. The benefit is fast lookup, O(1) to determine presence; the downside is that if two frequently accessed cache lines happen to map to the same location, they will constantly kick each other out, causing "thrashing."

The other extreme is **Fully Associative**: any cache line can be placed in any location in the Cache. Lookup requires comparing the address tag against all Cache Lines simultaneously, which is hardware-expensive, so it's only used in very small Caches (like the TLB).

In practice, a compromise is used—**Set Associative**. The Cache is divided into several sets, each containing N cache lines (N is the "way," or N-way set associative). A main memory cache line can only be placed in its corresponding set, but there are N positions to choose from within that set. Modern CPUs usually have L1 as 4-way or 8-way set associative, and L3 might be 12-way or even 16-way. Set associative achieves a good balance between hardware complexity and thrashing risk.

What happens when a set is full? This requires a **Replacement Policy**. The most common policy is LRU (Least Recently Used), kicking out the one that hasn't been accessed for the longest time. However, implementing precise LRU in hardware is too costly, so many CPUs use approximation algorithms like Pseudo-LRU. For us programmers, knowing that "recently used data stays in the Cache" is enough; we don't need to dive deep into the hardware's approximation details.

You can quickly confirm your CPU's cache line size on Linux using the `getconf` command:

```text
$ getconf LEVEL1_DCACHE_LINESIZE
64
```

If you see 64, that's the standard 64-byte cache line. If you see 128, your CPU might be using larger cache lines (some server chips do this), and alignment parameters will need to be adjusted accordingly.

> **Warning**
> If you find a loop traversing an array performs inexplicably poorly, and the array size is exactly a power of two, it's likely address conflict thrashing caused by direct mapping. A simple fix is to allocate some extra padding for the array to break that "exact modulo conflict" pattern. This type of problem is very subtle in high-performance code because, from the code perspective, everything looks fine.

## Step 4 — Understanding How Cores Keep Data Consistent

Things are still simple with a single core—data is either in the Cache or it isn't. But in multi-core systems, each core has its own L1 and L2. If core A modifies a cache line in its Cache, and core B still holds the old data of the same address in its Cache, chaos ensues.

This is the problem that **Cache Coherence Protocols** solve. The most widely used protocol on x86 is MESI (ARM uses a variant called MOESI). MESI is named after the four states of a cache line:

- **M (Modified)**: This data has been modified and differs from main memory. Only this core holds the latest copy.
- **E (Exclusive)**: This data matches main memory, and only this core holds a copy. If you want to modify it, you don't need to notify anyone.
- **S (Shared)**: This data matches main memory, but multiple cores might hold copies. It can only be read, not written directly.
- **I (Invalid)**: This cache line is invalid, effectively empty.

Let's walk through a specific example. Suppose core A and core B both read data from the same address. At this point, both cores' cache lines are in the S state. Now core A wants to write to this address—it needs to first issue an "Invalidate" broadcast, telling other cores: "If you hold data for this address, discard it." Core B receives the notification and changes its copy to I state, while core A's copy becomes M state. Core A can then safely modify the data. If core B later wants to read this address and finds itself in I state, it triggers a Cache Miss, fetches the latest data from core A (and writes it back to main memory), and the states on both sides transition to S or E depending on the situation.

This mechanism ensures all cores always see consistent data, but it has a side effect—**False Sharing**. If two cores are modifying different variables on the same cache line (e.g., two ints right next to each other in a struct), logically they don't interfere, but at the hardware level, they are contending for the same cache line. The MESI protocol will constantly trigger invalidations and synchronization, causing performance to plummet. This is a classic problem in multi-threaded programming, and later we will see how to use cache line alignment to avoid it.

> **Warning**
> False sharing is completely invisible in single-threaded tests; it only manifests as performance degradation under high multi-thread concurrency. The degradation is proportional to the number of threads—the more threads, the more frequent invalidate broadcasts on the bus. The standard way to investigate this is using the `perf` tool to observe cache miss events (`cache-misses`). If the multi-threaded version's cache misses spike abnormally, it's likely false sharing at work.

## Step 5 — Writing Code That Makes the Cache "Comfortable"

Enough theory; let's get practical. The core of cache-friendly programming can be summed up in one sentence: **Make data access patterns fit the way the Cache works**, which means maximizing spatial and temporal locality.

### Row-wise vs. Column-wise Traversal

The most classic example is traversing a two-dimensional array. In C, two-dimensional arrays are stored in **row-major** order, meaning `arr[0][0]`, `arr[0][1]`, `arr[0][2]`... are contiguous in memory. If we traverse row-wise, the access order matches the memory layout, maximizing spatial locality; if we traverse column-wise, each access skips an entire row, likely requiring a new cache line load every time.

```cpp
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define N 4096 // 16MB, fits in L3 but not L2

int main() {
    // Allocate contiguous memory for the 2D array
    int (*arr)[N] = malloc(sizeof(int[N][N]));

    clock_t start, end;

    // Row-wise traversal
    start = clock();
    long long sum1 = 0;
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            sum1 += arr[i][j];
        }
    }
    end = clock();
    printf("Row-wise: %.4f seconds\n", (double)(end - start) / CLOCKS_PER_SEC);

    // Column-wise traversal
    start = clock();
    long long sum2 = 0;
    for (int j = 0; j < N; j++) {
        for (int i = 0; i < N; i++) {
            sum2 += arr[i][j];
        }
    }
    end = clock();
    printf("Column-wise: %.4f seconds\n", (double)(end - start) / CLOCKS_PER_SEC);

    free(arr);
    return 0;
}
```

My test results are as follows (i7-12700H, L3 24MB):

```text
Row-wise: 0.0080 seconds
Column-wise: 0.0412 seconds
```

Row-wise traversal is usually 3 to 6 times faster than column-wise (depending on matrix size and Cache capacity). The principle is simple: when traversing row-wise, after loading one cache line, you can process 16 ints (64 bytes / 4 bytes) continuously; when traversing column-wise, each cache line is used for only 4 bytes before being swapped out.

### Struct Layout — Hot Data First

Another common optimization point is the arrangement of struct fields. If a struct has dozens of fields, but only three or four are used on the hot path, these fields should be placed close together so they can share the same cache line:

```cpp
struct ParticleBad {
    double x, y, z;        // 24 bytes
    char name[64];         // 64 bytes (Cold data)
    double vx, vy, vz;     // 24 bytes
    int id;                // 4 bytes
    // ... many other fields
};

struct ParticleGood {
    double x, y, z;        // 24 bytes
    double vx, vy, vz;     // 24 bytes
    int id;                // 4 bytes
    char name[64];         // 64 bytes (Cold data moved to end)
    // ... other fields
};
```

We can use `sizeof` to verify the layout difference. In `ParticleGood`, `x`, `y`, `z` are adjacent, totaling 12 bytes, and are contiguous within a cache line. In `ParticleBad`, `x` and `y` are separated by `name` and `vx`. If you traverse an array of particles and only read coordinates, after loading `x`, you skip 64 bytes of `name` to get to `y`, likely requiring a new cache line load—this is the cost of mixing hot and cold data.

If `x`, `y`, `z` are in the same cache line (they only take 12 bytes, easily fitting into a 64-byte line), one Cache load grabs them all. If they are scattered in different corners of the struct, accessing `y` might require loading a new cache line every time. This idea of separating hot and cold data is very common in high-performance code; the ECS (Entity Component System) architecture of game engines essentially does this—separating frequently accessed position and velocity data into continuous storage, and tossing rarely used things like names and model IDs into another array.

### Data-Oriented Design — SoA vs AoS

Extending the previous thought, if we have a group of objects of the same type, there are two ways to organize them: AoS (Array of Structures) and SoA (Structure of Arrays).

AoS is the most common way we write things—an array of structs, where each element is a complete struct:

```cpp
struct Particle { float x, y, z, r, g, b; };
Particle particles[1000];
```

SoA splits them into multiple independent arrays:

```cpp
struct ParticleSystem {
    float x[1000];
    float y[1000];
    float z[1000];
    float r[1000];
    float g[1000];
    float b[1000];
};
```

Let's compare their memory layouts:

![AoS Memory Layout](./02-aos-layout.drawio)

![SoA Memory Layout](./02-soa-layout.drawio)

If your hot path only processes coordinates `x`, `y`, `z` and doesn't touch colors `r`, `g`, `b`, SoA's advantage is obvious—traversing `x[0]`, `x[1]`, `x[2]`... is completely contiguous in memory, Cache hit rate is near 100%. In AoS, accessing every `x[i]` pulls `y`, `z`, `r`, `g`, `b` from the same struct into the Cache (because they are on the same cache line), but we don't need the color data right now, so that space is wasted.

Of course, SoA isn't a silver bullet. If your access pattern requires all fields simultaneously, AoS's spatial locality is actually better. The choice depends on your access pattern—there is no silver bullet, only trade-offs.

## C++ Connection — From C Understanding to C++ Tools

Everything we discussed earlier—cache lines, locality, false sharing—is all at the hardware level and language-agnostic. However, C++ provides us with some tools at the standard level to better cooperate with the Cache, which C lacks.

### `std::hardware_destructive_interference_size` (C++17)

C++17 introduced a compile-time constant `std::hardware_destructive_interference_size`. Its value equals the minimum spacing between two concurrently accessed cache lines on the target platform—on x86, this is 64. The name is indeed long, but its purpose is direct: using this value for alignment ensures two variables won't be placed on the same cache line, thereby avoiding false sharing:

```cpp
#include <new>
#include <cstddef>
#include <iostream>

struct AvoidFalseSharing {
    int a;
    // Padding to prevent a and b from sharing a cache line
    alignas(std::hardware_destructive_interference_size) char padding[64];
    int b;
};

int main() {
    std::cout << "Destructive interference size: "
              << std::hardware_destructive_interference_size << std::endl;
    return 0;
}
```

After doing this, `a` and `b` will not share a cache line, even if they are close in memory. Thread A modifying `a` won't cause Thread B's cache line to invalidate—this is the standard solution to the false sharing problem we discussed in the MESI section.

In C, we can only hardcode `__attribute__((aligned(64)))` (GCC/Clang) or `__declspec(align(64))` (MSVC). There is no portable way to get this value. C++17's constant theoretically provides portability—though in practice, mainstream compilers return 64 on all supported platforms.

### `alignas` and Cache Line Alignment

C++11 introduced the `alignas` keyword, allowing us to specify alignment requirements for variables or types. Combined with cache line size, we can manually ensure certain critical data structures don't span cache lines:

```cpp
struct alignas(64) AlignedStruct {
    int data[16]; // Exactly 64 bytes
};

static_assert(sizeof(AlignedStruct) == 64, "Must fit in one cache line");
```

This `static_assert` is very useful—if someone adds too many fields to the struct later causing it to exceed 64 bytes, the compiler will error out directly at compile time. This is much better than discovering performance degradation at runtime.

### Impact of Data Structure Layout on Cache

Containers in the C++ standard library are also designed with Cache factors in mind. `std::vector` stores data contiguously, so traversal is extremely cache-friendly. `std::list` allocates each node independently, likely scattered all over memory, making traversing it a nightmare for the Cache. This is why in many modern C++ coding standards, `std::vector` is the default container, and `std::list` is rarely recommended—not because list's time complexity is bad (insertion/deletion is indeed O(1)), but because its cache hit rate is too poor, and the constant factor is ridiculously large. `std::deque` is a compromise—it stores in chunks, fixed chunk size, better than list, but still worse than vector. If you are working on performance-sensitive scenarios, the primary consideration for container choice is often not time complexity, but the impact of memory layout on the Cache.

## Exercises

1. **Stride Experiment Verification**: Modify the stride test code in this article to change the array size to 4MB (just enough to fill most CPUs' L3). Observe the time cost curve as the stride changes from 1 to 32. Think about it: why does the time cost start to flatten out again after the stride exceeds 16?

2. **Reproduce False Sharing**: Write a multi-threaded program (using pthread or C++ `std::thread`). Create two threads that each increment different fields in a shared struct a hundred million times. Run it once without alignment, then run it again by aligning the two fields to different cache lines using `alignas`. Compare the time costs.

3. **Matrix Transpose Optimization**: Implement a square matrix transpose function. First, write a naive double-loop version. Then try blocking—split the matrix into 32x32 small blocks and perform the transpose within the block. Compare the performance of the two versions on a large matrix (2048x2048).

4. **AoS vs SoA Benchmark**: Define a particle struct containing `x, y, z, r, g, b`. Create 100,000 particles. Implement "normalize all particle coordinates to the unit sphere" using both AoS and SoA layouts. Compare the time costs.

5. **Cache-Friendly Linked List**: Refer to the Linux kernel's `list_head` design idea. Implement an intrusive doubly linked list where node data and list pointers are stored separately, so traversing the pointers doesn't require loading the entire node data, improving cache hit rate.

## References

- [cppreference: `std::hardware_destructive_interference_size`](https://en.cppreference.com/w/cpp/thread/hardware_destructive_interference_size)
- [cppreference: `alignas` specifier](https://en.cppreference.com/w/cpp/language/alignas)
- [Ulrich Drepper: What Every Programmer Should Know About Memory](https://people.freebsd.org/~lstewart/articles/cpumemory.pdf)
- [Gustavo Duarte: Cache: a place for concealment](https://manybutfinite.com/post/intel-cpu-caches/)
