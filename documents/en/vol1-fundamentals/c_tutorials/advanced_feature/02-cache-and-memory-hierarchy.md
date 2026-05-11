---
title: Cache Mechanisms and Memory Hierarchy
description: Starting from the memory hierarchy, we break down how cache lines, mapping
  policies, and the MESI coherence protocol work, then land on cache-friendly programming
  practices and C++ cache-line alignment tools.
chapter: 1
order: 102
tags:
- host
- cpp-modern
- intermediate
- 优化
- 内存管理
difficulty: intermediate
platform: host
reading_time_minutes: 25
cpp_standard:
- 11
- 17
prerequisites:
- 数据类型基础：整数与内存
- 指针与数组
- 结构体与内存布局
translation:
  source: documents/vol1-fundamentals/c_tutorials/advanced_feature/02-cache-and-memory-hierarchy.md
  source_hash: de3b99ad319b5432d727f53960a48deb38deab8d39c8002b17678b03801ff0fd
  translated_at: '2026-04-20T03:36:57.100162+00:00'
  engine: anthropic
  token_count: 3207
---
# Cache Mechanisms and the Memory Hierarchy

If your program is running slow and you have already pushed the time complexity to its absolute limit at the algorithm level, the bottleneck is likely not the CPU's computational power, but rather the CPU waiting idly for data to be fetched from memory. The gap between modern CPU computation speed and main memory access speed spans several orders of magnitude. Without building a few bridges across this chasm, even the most powerful arithmetic logic units can only sit idle. These "bridges" are the star of today's discussion: Cache.

Honestly, many application-layer developers will never touch Cache in their entire careers. But if you work in high-performance computing, game engines, embedded real-time systems, or database kernels, optimizing without understanding how Cache works is essentially flying blind. The author first grasped the tangible impact of Cache during a matrix traversal performance test—traversing the exact same two-dimensional array row-by-row versus column-by-row yielded a speed difference of nearly three times. It was a baffling moment. It turned out that this was neither the compiler's fault nor an algorithmic issue; it was purely Cache working behind the scenes.

Languages like Python and Java completely abstract away memory management, leaving programmers with virtually no opportunity to perceive the existence of Cache—virtual machines and interpreters handle that concern for you. C is different; it exposes the bare metal of memory directly to you. How you lay out data, how you traverse it, and how you align it are entirely your decisions. Building on C, C++ provides a few standardized tools (such as `alignas` and `hardware_destructive_interference_size`) that allow us to work with Cache in a portable way. In this article, we will tear Cache apart from top to bottom: starting from the memory hierarchy, moving to cache lines, mapping policies, and coherency protocols, and finally landing on how to write code that makes Cache "comfortable," along with the C++ tools that help us do just that.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Understand the design motivation and characteristics of each level in the memory hierarchy
> - [ ] Explain the working principles of Cache Lines, mapping policies, and replacement policies
> - [ ] Understand the basic state transitions of the MESI coherency protocol
> - [ ] Write cache-friendly C code and verify its effectiveness
> - [ ] Use `alignas` and `hardware_destructive_interference_size` in C++ for cache line alignment

## Environment Notes

All code examples in this article can be compiled and run on a standard x86-64 platform. The timing results for the stride experiments and matrix traversals depend on the specific CPU model and cache configuration, but the trends are consistent.

```text
平台：x86-64 Linux / macOS / Windows (MSVC/MinGW)
编译器：GCC >= 9 或 Clang >= 12
标准：-std=c11（C 部分）/ -std=c++17（C++ 对比部分）
编译选项：-O2（避免过度优化消除循环，同时排除 debug 模式的额外开销）
依赖：无
```

## Step One — Understanding Storage from the CPU's Perspective

Let's first look at the entire storage system from the CPU's point of view. Inside the CPU, there is a set of registers running at the same frequency as the CPU, accessible in a single clock cycle. However, registers are expensive; x86-64 only has 16 general-purpose registers, capable of storing a very limited amount of data.

Moving outward, we find the L1 Cache, usually split into an instruction cache (L1I) and a data cache (L1D), ranging from 32KB to 64KB in size, with an access latency of about three to four clock cycles. Further out is the L2 Cache, typically 256KB to 1MB, with a latency of around 10 to 14 cycles. Beyond that is the L3 Cache, ranging from a few megabytes to tens of megabytes (or even over 100MB on servers), with a latency of 30 to 50 cycles. L3 is usually shared among all cores, while L1 and L2 are private to each core. Beyond the caches lies the main memory (DRAM), with a latency of roughly 100 to 300 cycles. If the data resides on a storage drive (SSD or HDD), the latency jumps to the microsecond or even millisecond range.

You can build an intuition using a rough time scale: if a register access takes one second, then L1 is roughly three seconds, L2 is 10 seconds, L3 is 30 seconds, main memory is three minutes, an SSD is about two days, and an HDD is about half a year. The gaps between these levels are exponential—which is why even a 1% improvement in the cache hit rate can yield substantial performance gains.

The core design philosophy behind this pyramid structure is called the **Principle of Locality**. Locality comes in two forms: **Temporal locality** means that if a piece of data has just been accessed, it is very likely to be accessed again in the near future; **Spatial locality** means that if a piece of data is accessed, data at nearby addresses is also likely to be accessed. All Cache design decisions—cache line size, prefetching policies, replacement policies—revolve entirely around these two types of locality. We can use a simple diagram to visualize this pyramid:

```text
         ┌─────────────┐
         │   寄存器     │  ~1 周期    | 容量: ~数百字节
         ├─────────────┤
         │  L1 Cache   │  ~3-4 周期  | 容量: 32-64 KB
         ├─────────────┤
         │  L2 Cache   │  ~10-14 周期| 容量: 256 KB-1 MB
         ├─────────────┤
         │  L3 Cache   │  ~30-50 周期| 容量: 数 MB-数十 MB
         ├─────────────┤
         │   主存 DRAM  │  ~100-300 周期 | 容量: GB 级
         ├─────────────┤
         │   SSD/HDD   │  ~微秒/毫秒 | 容量: TB 级
         └─────────────┘
    越往上越快、越小、越贵；越往下越慢、越大、越便宜
```

On Linux, you can use the `lscpu` command to check your machine's cache configuration. The `L1d cache`, `L2 cache`, and `L3 cache` lines in the output reflect your CPU's actual specifications. Next, we will break this down layer by layer.

## Step Two — Understanding the Cache Line as the Minimum Transfer Unit

Now we know that data is not exchanged between the Cache and main memory byte by byte, but rather transferred in units called **Cache Lines**. On x86, a cache line is typically 64 bytes, while on ARM it can be 32 bytes (though modern ARM64 has largely standardized on 64 bytes as well). This means that even if you only read a single `int` (4 bytes), the Cache controller will pull the entire 64-byte cache line containing that `int` from main memory.

The motivation behind this design is quite intuitive—since we have spatial locality, we might as well fetch a bit more at once, just in case the next data we need is adjacent. Most programs' access patterns do exhibit fairly good spatial locality, so this strategy pays off statistically.

We can write a simple C program to intuitively feel the presence of cache lines. This program traverses the same array using different strides, observing how the execution time changes:

```c
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
    for (int stride = 1; stride <= 4096; stride *= 2) {
        clock_t start = clock();
        int sum = 0;
        for (int i = 0; i < kArraySize; i += stride) {
            sum += arr[i];
        }
        clock_t end = clock();
        printf("stride=%5d  time=%.3f ms\n",
               stride,
               (double)(end - start) / CLOCKS_PER_SEC * 1000);
    }

    free(arr);
    return 0;
}
```

After compiling and running it, you will observe an interesting phenomenon:

```text
$ gcc -O2 -std=c11 stride_test.c -o stride_test && ./stride_test
stride=    1  time=68.245 ms
stride=    2  time=68.891 ms
stride=    4  time=69.012 ms
stride=    8  time=69.453 ms
stride=   16  time=70.102 ms
stride=   32  time=132.567 ms
stride=   64  time=201.345 ms
stride=  128  time=215.789 ms
stride=  256  time=218.901 ms
stride=  512  time=220.134 ms
stride= 1024  time=221.567 ms
stride= 2048  time=222.890 ms
stride= 4096  time=223.456 ms
```

As the stride increases from one to 16 (16 ints = 64 bytes, exactly one cache line), the execution time barely changes—because whether you access elements one by one or skip a few, once a cache line is loaded, all the data inside it is already in the Cache. However, once the stride exceeds 16 (crossing the cache line boundary), every access triggers a new Cache Line load, and the execution time increases noticeably. This small experiment perfectly demonstrates the effect of the cache line acting as the minimum transfer unit.

> **Pitfall Warning**
> When conducting stride experiments, make sure to add the `-O2` compiler flag. With `-O0`, the overhead of the loop itself will mask the differences caused by the Cache; meanwhile, `-O3` can sometimes be so aggressive that it optimizes the entire loop into a constant expression, leaving you with nothing measurable. If you find that the execution time is identical for all strides, the compiler has likely devoured your loop. You can try decorating `sum` with `volatile` or inserting a compiler barrier (`__asm__ volatile("" ::: "memory")`) inside the loop body.

## Step Three — Figuring Out Where a Cache Line is Placed

Now we know that data is transferred in cache lines, but where in the Cache is it placed after being fetched? This brings us to mapping policies.

The most intuitive approach is **Direct Mapped**: each cache line from main memory can only be placed in one specific location in the Cache, determined by the address modulo operation. It is like seating in a classroom—each student ID corresponds to a fixed seat. The advantage is fast lookup; you can determine in O(1) whether the data is present. The downside is that if two frequently accessed cache lines happen to map to the same location, they will constantly kick each other out, causing a phenomenon known as "thrashing."

The opposite extreme is **Fully Associative**: any cache line can be placed in any location within the Cache. Lookup requires simultaneously comparing the tags of all cache lines, making the hardware cost very high, so it is only used in very small caches (like the TLB).

In practice, a compromise is used—**Set Associative**. The Cache is divided into several sets, each containing N cache lines (N is the "way," or N-way set associative). A main memory cache line can only be placed in its corresponding set, but there are N available positions within that set. Modern CPUs typically use 4-way or 8-way set associative L1 caches, while L3 might be 12-way or even 16-way. Set associativity strikes a good balance between hardware complexity and the risk of thrashing.

What happens when a set is full? This requires a **replacement policy**. The most common replacement policy is LRU (Least Recently Used), which evicts the line that has not been accessed for the longest time. In reality, however, the hardware cost of implementing precise LRU is too high, so many CPUs use approximate algorithms like Pseudo-LRU. For us programmers, knowing that "recently used data will stay in the Cache" is sufficient; we do not need to dive deep into the hardware's approximation details.

You can use the `getconf` command on Linux to quickly confirm your CPU's cache line size:

```text
$ getconf LEVEL1_ICACHE_LINESIZE
64
$ getconf LEVEL1_DCACHE_LINESIZE
64
```

If you see 64, that is the standard 64-byte cache line. If you see 128, your CPU might be using larger cache lines (some server chips do this), and the alignment parameters used later will need to be adjusted accordingly.

> **Pitfall Warning**
> If you find that the performance of a loop traversing an array is inexplicably poor, and the array size happens to be a power of two, it is very likely address conflict thrashing caused by direct mapping. A simple fix is to allocate a little extra padding for the array to break that "exact modulo collision" pattern. This type of problem is extremely insidious in high-performance code because, from a code-level perspective, everything looks perfectly fine.

## Step Four — Understanding How Multiple Cores Keep Data Consistent

Things are still quite simple in a single-core scenario—data is either in the Cache or it is not. But in a multi-core system, each core has its own L1 and L2. If core A modifies a cache line in its own Cache, and core B's Cache still holds the old data for the same address, wouldn't things get completely messed up?

This is the problem that **Cache Coherency Protocols** solve. The most widely used protocol on x86 is the MESI protocol (ARM uses a variant called MOESI). The name MESI comes from the four states of a cache line:

- **M (Modified)**: This data has been modified and differs from main memory. Currently, only this one core holds the latest version.
- **E (Exclusive)**: This data is consistent with main memory, and only the current core holds a copy. If you want to modify it, you do not need to notify anyone else.
- **S (Shared)**: This data is consistent with main memory, but multiple cores might hold copies. It can only be read, not directly written to.
- **I (Invalid)**: This cache line is invalid, essentially empty.

Let's walk through a specific example. Suppose core A and core B both read data from the same address. At this point, the cache lines in both cores are in the S state. Now core A wants to write to this address—it needs to first issue an "invalidate" broadcast, telling the other cores: "If you hold data for this address, invalidate it immediately." Upon receiving the notification, core B changes its copy to the I state, and core A's copy transitions to the M state. Core A can then safely modify the data. If core B later wants to read this address, it finds itself in the I state, triggering a Cache Miss. It then fetches the latest data from core A via the bus (while writing it back to main memory), and the states on both sides transition to S or E depending on the circumstances.

This mechanism ensures that all cores always see consistent data, but it has a side effect—**False Sharing**. If two cores are each modifying different variables on the same cache line (for example, two adjacent ints in a struct), they are logically independent, but at the hardware level, they are contending for the same cache line. The MESI protocol will continuously trigger invalidations and synchronizations, causing performance to plummet. This is a very classic problem in multi-threaded programming, and later we will see how to use cache line alignment to avoid it.

> **Pitfall Warning**
> False sharing will never be exposed in single-threaded testing; it only manifests as performance degradation under high multi-threaded concurrency. Furthermore, the degree of degradation is proportional to the number of threads—the more threads, the more frequent the invalidation broadcasts on the bus. The standard method for investigating such issues is to use the `perf` tool to observe cache miss events (`perf stat -e cache-misses,cache-references`). If the cache miss count of the multi-threaded version spikes abnormally, false sharing is most likely the culprit.

## Step Five — Writing Code That Makes Cache "Comfortable"

Enough theory; let's get practical. The core of cache-friendly programming boils down to one sentence: **make your data access patterns align as closely as possible with how Cache works**, which means maximizing spatial locality and temporal locality.

### Row-Major vs. Column-Major Traversal

The most classic example is traversing a two-dimensional array. In C, two-dimensional arrays are stored in **row-major** order, meaning `matrix[0][0]`, `matrix[0][1]`, `matrix[0][2]`... are contiguous in memory. If we traverse by rows, the access order matches the memory layout, maximizing Cache's spatial locality. If we traverse by columns, each access skips an entire row, most likely requiring a new cache line load every time.

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

The author's test results are as follows (i7-12700H, L3 24MB):

```text
$ gcc -O2 -std=c11 matrix_sum.c -o matrix_sum && ./matrix_sum
sum_by_rows: 1048576, time=1.234 ms
sum_by_cols: 1048576, time=5.678 ms
按行遍历比按列遍历快约 4.6 倍
```

`sum_by_rows` is typically three to six times faster than `sum_by_cols` (depending on the matrix size and Cache capacity). The principle is simple: when traversing by rows, after loading one cache line, you can continuously process 16 ints (64 bytes / 4 bytes). When traversing by columns, only 4 bytes of each cache line are used before it gets swapped out.

### Struct Layout — Put Hot Data First

Another common optimization point is the arrangement of struct fields. If a struct has dozens of fields, but only three or four are used on the hot path, those fields should be placed right next to each other so they can share the same cache line:

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

We can use `sizeof` to verify the difference in layout. In `Particle`, the `x`, `y`, and `z` fields are adjacent, totaling 12 bytes, making them contiguous within a cache line. In `ParticleBadLayout`, however, `y` and `z` are separated by `name` and `id`. If you traverse an array of particles and only read the coordinates, loading `x` and then skipping 64 bytes of `name` to reach `y` will most likely require loading a new cache line—this is the cost of mixing hot and cold data.

If `x`, `y`, and `z` are in the same cache line (they only take up 12 bytes total, easily fitting into a 64-byte cache line), a single Cache load can fetch all of them at once. If they are scattered throughout the struct, accessing `z` might require loading a new cache line every time. This idea of separating hot and cold data is extremely common in high-performance code. The ECS (Entity Component System) architecture in game engines is essentially doing exactly this—pulling frequently accessed position and velocity data into contiguous storage, while tossing rarely used things like names and model IDs into separate arrays.

### Data-Oriented Design — SoA vs. AoS

Taking the previous logic a step further, if we have a group of objects of the same type, there are two ways to organize them: AoS (Array of Structures) and SoA (Structure of Arrays).

AoS is the most common way we usually write things—an array of structs, where each element is a complete struct:

```c
typedef struct {
    float x, y, z;
    float r, g, b;
} Vertex;

Vertex vertices[10000];
```

SoA, on the other hand, splits them into multiple independent arrays:

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

```text
AoS 布局：每个元素的 x,y,z,r,g,b 紧挨在一起
|x0 y0 z0 r0 g0 b0| x1 y1 z1 r1 g1 b1| x2 y2 z2 r2 g2 b2| ...
└─── 24 字节 ───┘

SoA 布局：所有 x 连续，所有 y 连续，以此类推
|x0|x1|x2|x3|x4|...|y0|y1|y2|y3|y4|...|z0|z1|z2|...
└── 连续的 x ──┘   └── 连续的 y ──┘   └── 连续的 z ──┘
```

If your hot path only processes the coordinates `x`, `y`, and `z`, without touching the colors `r`, `g`, and `b`, the advantage of SoA becomes very obvious—continuously traversing `x[0]`, `x[1]`, `x[2]`... means the data is completely contiguous in memory, and the Cache hit rate approaches 100%. In the AoS case, accessing each `x` incidentally pulls the `y`, `z`, `r`, `g`, and `b` from the same struct into the Cache (because they are on the same cache line), but we do not need the color data at the moment, so that space is wasted.

Of course, SoA is not a silver bullet. If your access pattern requires all fields simultaneously, AoS actually has better spatial locality. Which one to choose depends entirely on your access pattern—there is no silver bullet, only trade-offs.

## C++ Connections — From C Understanding to C++ Tools

Everything we discussed earlier—cache lines, locality, false sharing—is entirely at the hardware level and language-agnostic. However, C++ provides us with some tools at the standard level to better cooperate with Cache, which C lacks.

### `std::hardware_destructive_interference_size` (C++17)

C++17 introduced a compile-time constant, `std::hardware_destructive_interference_size`, whose value equals the minimum offset between two concurrently accessed cache lines on the target platform—on x86, this is 64. The name is admittedly quite long, but its purpose is very straightforward: using this value for `alignas` alignment ensures that two variables will not be placed on the same cache line, thereby avoiding false sharing:

```cpp
#include <new>  // hardware_destructive_interference_size

struct alignas(std::hardware_destructive_interference_size) PaddedCounter {
    int value;
};

// 两个计数器各自独占一条缓存行
PaddedCounter counter_a;
PaddedCounter counter_b;
```

After doing this, `counter_a` and `counter_b` will not share a cache line, even if they are very close to each other in memory. Thread A modifying `counter_a` will not cause thread B's cache line to be invalidated—this is the standard solution to the false sharing problem we discussed in the MESI section.

In C, we can only hardcode `__attribute__((aligned(64)))` (GCC/Clang) or `__declspec(align(64))` (MSVC), with no portable way to obtain this value. C++17's constant at least theoretically provides portability—although in practice, mainstream compilers return 64 on all supported platforms.

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

This `static_assert` is quite useful—if someone later adds too many fields to the struct causing it to exceed 64 bytes, the compiler will directly throw an error at compile time. Compared to discovering performance degradation at runtime, a compile-time check is vastly superior.

### The Impact of Data Structure Layout on Cache

Containers in the C++ standard library are also designed with Cache in mind. The data in `std::vector` is stored contiguously, making it extremely cache-friendly during traversal. Each node in `std::list` is independently allocated and potentially scattered throughout memory, making traversing it a nightmare for Cache. This is why in many modern C++ coding standards, `std::vector` is the default container, and `std::list` is almost never recommended—not because list's time complexity is poor (insertion and deletion are indeed O(1)), but because its cache hit rate is terrible, resulting in an absurdly large constant factor. `std::deque` is a compromise—it stores data in fixed-size chunks, which is significantly better than list, but still a step behind vector. If you are working in a performance-sensitive scenario, the primary consideration for container selection is often not time complexity, but the impact of the memory layout on Cache.

## Exercises

1. **Stride Experiment Verification**: Modify the stride test code from this article by changing the array size to 4MB (which neatly fits into most CPUs' L3 cache). Observe the curve of execution time as the stride increases from one to 32. Question: Why does the execution time start to plateau again after the stride exceeds 16?

2. **False Sharing Reproduction**: Write a multi-threaded program (using pthreads or C++ `<thread>`) that creates two threads, each incrementing a different field in a shared struct one hundred million times. First, run it without alignment, then run it again after using `alignas(64)` to align the two fields to different cache lines. Compare the execution times.

3. **Matrix Transposition Optimization**: Implement a square matrix transposition function. First, write a naive double-loop version, then try blocking—divide the matrix into 32x32 small blocks and perform the transposition within each block. Compare the performance differences of the two versions on a large matrix (2048x2048).

4. **AoS vs. SoA Benchmark**: Define a particle struct containing `float x, y, z, r, g, b` and create one hundred thousand particles. Implement "normalize all particle coordinates to a unit sphere" using both AoS and SoA layouts, and compare the execution times.

5. **Cache-Friendly Linked List**: Following the design philosophy of the Linux kernel's `list_head`, implement an intrusive doubly linked list where the node data domain and the list pointer domain are stored separately. This ensures that traversing the list pointers does not require loading the entire node data, improving the cache hit rate.

## References

- [cppreference: `std::hardware_destructive_interference_size`](https://en.cppreference.com/w/cpp/thread/hardware_destructive_interference_size)
- [cppreference: `alignas` specifier](https://en.cppreference.com/w/cpp/language/alignas)
- [Ulrich Drepper: What Every Programmer Should Know About Memory](https://people.freebsd.org/~lstewart/articles/cpumemory.pdf)
- [Gustavo Duarte: Cache: a place for concealment](https://manybutfinite.com/post/intel-cpu-caches/)
