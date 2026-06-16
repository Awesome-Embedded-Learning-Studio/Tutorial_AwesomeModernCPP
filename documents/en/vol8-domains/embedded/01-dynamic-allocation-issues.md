---
chapter: 5
cpp_standard:
- 11
- 14
- 17
- 20
description: Analyzing Embedded Dynamic Memory Issues
difficulty: intermediate
order: 1
platform: stm32f1
prerequisites:
- 'Chapter 3: 内存与对象管理'
reading_time_minutes: 5
tags:
- cpp-modern
- intermediate
- stm32f1
title: Dynamic Allocation Issues
translation:
  source: documents/vol8-domains/embedded/01-dynamic-allocation-issues.md
  source_hash: ec7bbcad3c84325e7d7c98ca54fac346dd3497f1aebc24e116714dcaf54a45b3
  translated_at: '2026-06-16T04:08:58.299801+00:00'
  engine: anthropic
  token_count: 770
---
# The Cost of Dynamic Memory: Fragmentation and Uncertainty (Memory Layout, Fragmentation, and Alignment)

## Introduction

In embedded systems, dynamic memory seems convenient, but its costs are often underestimated—fragmentation, timing uncertainty, and alignment and structure padding issues can silently consume resources and reliability.

We all know that in embedded systems, resources are extremely limited. Tiny decisions in memory allocation can affect stability, real-time performance, and power consumption. Understanding the cost of dynamic memory allows us to avoid catastrophic errors during design—or minimize risks when dynamic memory is unavoidable.

------

## A Quick Review of Memory Layout: Static, Heap, and Stack

Before we start, let's review the concepts:

- **Static Area (.data/.bss/.rodata)**: Size is determined at compile time or link time. Includes global variables, constants, and read-only data. Lifetime matches the program, fragmentation risk is almost zero, but flexibility is low.
- **Stack**: Local variables and automatic objects for function calls. Allocation/deallocation is very fast (usually just pointer increments), highly regular, and lifetime is controlled by scope. Drawbacks include limited capacity, inability to share across tasks, and unsuitability for large objects or objects with variable lifetimes.
- **Heap**: Runtime dynamic allocation (`malloc` / `new` / `std::make_unique` etc.). Flexible but with obvious costs: allocation and deallocation time is non-deterministic, generates fragmentation, and memory layout is non-linear.

In embedded development, the general preference order is: Stack (if size allows) → Static (pre-allocatable) → Heap (use cautiously, preferably controlled).

------

## Fragmentation: What, Why, and How It Affects the System

### Internal fragmentation

When an allocator allocates a larger block than the actual request to satisfy alignment or minimum allocation unit constraints, this unused space is **internal fragmentation**. Examples:

- The allocator allocates in 16-byte granularity. A 20-byte object will occupy 32 bytes (16×2), and the extra 12 bytes is internal fragmentation.
- Frequent allocation of small objects with large allocation units leads to decreased memory utilization.

### External fragmentation

There are many free blocks in the heap, but they are scattered and discontinuous, unable to merge into a large enough contiguous space to satisfy a larger allocation request. The result can be a situation where total memory is sufficient but allocation fails ("available memory fragmentation"). The symptoms we observe are:

- As runtime increases, available large blocks decrease, causing occasional `malloc`/`new` failures.
- The system exhibits intermittent crashes, memory leak-like symptoms, and degraded stability after long runs.
- Real-time tasks experience long-tail latency (occasional long allocation/deallocation operations).

------

## Run Online

Run the struct alignment example online to observe how member arrangement affects `sizeof`:

<OnlineCompilerDemo
  title="Struct Alignment and Padding: BadLayout vs GoodLayout"
  source-path="code/examples/compiler_explorer/struct_alignment_host.cpp"
  description="Run online and compare the sizeof differences between two struct layouts to understand the mechanism of alignment padding."
  allow-run
  allow-x86-asm
/>

## Alignment and Padding

### Why Alignment is Needed

CPUs typically expect certain data to be aligned on their natural boundaries (e.g., 4-byte alignment, 8-byte alignment); otherwise, access is slower or causes hardware exceptions on some architectures. Alignment also affects DMA, peripheral access, and cache coherency.

### Struct Padding Example

```cpp
struct BadLayout {
    char a;        // 1 byte
    // 3 bytes of padding here
    int b;         // 4 bytes
};
```

`char` occupies 1 byte, `int` requires 4-byte alignment, so the compiler inserts 3 bytes of padding after `a`. The total struct size is aligned to a multiple of 4 (8 in this case).

Placing members with larger alignment requirements first can reduce padding:

```cpp
struct GoodLayout {
    int b;         // 4 bytes
    char a;        // 1 byte
    // 1 byte of padding here to align struct size to 4 bytes
};
```

Or use `#pragma pack(1)` or `[[no_unique_address]]` to forcibly remove padding, but note:

- Accessing unaligned members after removing padding can significantly degrade performance or cause hardware exceptions on some architectures.
- Use only when the consequences are clear and saving space is absolutely necessary.

#### Relationship with DMA / Cache Line

- DMA requires buffers to be aligned to peripheral requirements (e.g., 32 bytes). Misalignment can lead to hardware refusal or severe performance degradation.
- Aligning to cache lines (usually 32/64 bytes) helps avoid false sharing and cache thrashing, which is especially important in multi-core systems or when accessing concurrently with DMA.

------

## Uncertainty of Dynamic Memory: Time and Reproducibility Issues

- **Non-deterministic allocation/deallocation time**: General heap implementations involve complex data structures (free lists, trees, bitmaps), causing the execution time of `malloc`/`free` to be unpredictable, potentially leading to long-tail latency.
- **Concurrency and lock contention**: In multi-threaded environments, the heap usually requires locks or thread-local caching (TLC); lock contention affects real-time performance.
- **Unrecoverable fragmentation**: For standard C/C++ heaps, once fragmentation forms, it is difficult to recover in linear time. It must be resolved by restarting or using specialized compaction strategies (usually unrealistic).

Embedded systems are particularly sensitive: long-tail latency can lead to dropped frames, control timeouts, or security issues.

------

## Common Embedded Alternatives and Hybrid Strategies

So, what can we do? Here are several common strategies:

#### Memory Pool (Pool / Slab)

- Divide memory into fixed-size blocks (e.g., 32B, 64B, 256B). Allocation returns a block index or pointer, and deallocation returns the block to the free list.
- Pros: Allocation/deallocation is constant time (O(1)), no external fragmentation (as long as all objects match a pool size).
- Cons: Requires multiple pools for different object sizes, memory utilization depends on allocation granularity, and internal fragmentation can occur.

#### Bump / Arena Allocator (Linear Allocator)

- Allocates linearly from a contiguous buffer; deallocation is usually all-at-once (entire arena reset).
- Very fast and fragmentation-free; suitable for objects with consistent lifetimes (e.g., temporary objects during a single task or initialization phase).
- Not suitable for objects requiring arbitrary deallocation.

#### Slab Allocation (Linux style)

- Suitable for caching objects of the same type (kernel objects), allowing reuse of initialized objects upon deallocation to reduce construction/destruction overhead.

#### Object Pool + RAII (C++ style)

- Use `std::unique_ptr` or custom smart pointers combined with memory pools to guarantee exception safety and automatic release.

------

## Code Examples
