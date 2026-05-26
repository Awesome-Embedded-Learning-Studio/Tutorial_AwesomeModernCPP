---
chapter: 5
cpp_standard:
- 11
- 14
- 17
- 20
description: Analyzing dynamic memory issues in embedded systems
difficulty: intermediate
order: 1
platform: stm32f1
prerequisites:
- 'Chapter 3: 内存与对象管理'
reading_time_minutes: 6
tags:
- cpp-modern
- intermediate
- stm32f1
title: Dynamic Allocation Issues
translation:
  source: documents/vol8-domains/embedded/01-dynamic-allocation-issues.md
  source_hash: 2576910a33d419a77e06cb69a15f53c465165bceaf93afa51db63827e973309b
  translated_at: '2026-05-26T11:59:52.455732+00:00'
  engine: anthropic
  token_count: 770
---
# The Cost of Dynamic Memory: Fragmentation and Non-Determinism (Memory Layout, Fragmentation, and Alignment)

## Introduction

In embedded systems, dynamic memory seems convenient, but its costs are often underestimated—fragmentation, timing non-determinism, and alignment or structure padding issues quietly consume resources and reliability.

We all know that embedded environments have extremely limited resources, and even minor memory allocation decisions can affect stability, real-time performance, and power consumption. Understanding the cost of dynamic memory helps us avoid catastrophic design errors—or at least minimize risk when dynamic memory is unavoidable.

------

## A Quick Review of Memory Layout: Static, Heap, and Stack

Before we dive in, let's review the basics:

- **Static region (.data/.bss/.rodata)**: Sizes are determined at compile or link time for global variables, constants, and read-only data. Their lifetime matches the program's, fragmentation risk is nearly zero, but flexibility is low.
- **Stack (stack)**: Stores local variables and automatic objects for function calls. Allocation and deallocation are extremely fast (typically just pointer increments and decrements), highly regular, and lifetimes are controlled by scope. The downside is limited capacity, inability to share across tasks, and unsuitability for large objects or objects with variable lifetimes.
- **Heap (heap)**: Dynamically allocated at runtime (`malloc` / `new` / `operator new`, etc.). Flexible but with obvious costs: allocation and deallocation times are non-deterministic, it generates fragmentation, and the memory layout is non-linear.

In embedded systems, the general preference order is: stack (if size permits) → static (pre-allocated) → heap (use cautiously, preferably controlled).

------

## Fragmentation: What, Why, and How It Affects the System

### Internal Fragmentation

When an allocator assigns a larger block than actually requested to satisfy alignment or minimum allocation unit requirements, this unused space becomes **internal fragmentation**. For example:

- If an allocator uses a 16-byte granularity, a 20-byte object will occupy 32 bytes (16×2). The extra 12 bytes are internal fragmentation.
- Frequent allocation of small objects with a large allocation unit leads to decreased memory utilization.

### External Fragmentation

The heap contains many free blocks, but these blocks are scattered and non-contiguous, making it impossible to merge them into a large enough contiguous space to satisfy a larger allocation request. The result can be a situation where the total memory is sufficient but allocation still fails ("available memory fragmentation"). What we observe is—

- As runtime increases, available large blocks of memory decrease, leading to occasional `new`/`malloc` failures.
- The system exhibits intermittent crashes, memory leak-like symptoms, and degraded stability after long-term operation.
- Real-time tasks experience long-tail latency (sporadic long allocation/deallocation operations).

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

### Why Alignment Is Needed

CPUs typically expect certain data to be aligned to its natural boundary (for example, 4-byte or 8-byte alignment); otherwise, access becomes slower or triggers hardware exceptions on some architectures. Alignment also affects DMA, peripheral access, and cache coherency.

### Struct Padding Example

```cpp
// 假设：sizeof(char)=1, sizeof(int32_t)=4
struct A {
    char c;      // offset 0
    int32_t x;   // 如果按照 4 字节对齐，x 的 offset 通常是 4
};              // sizeof(A) 通常是 8（包括 3 字节填充）

```

`char` occupies 1 byte, `int32_t` requires 4-byte alignment, so the compiler inserts 3 bytes of padding after `c`. The total struct size is aligned to a multiple of 4 (which is 8 here).

Placing members with larger alignment requirements first can reduce padding:

```cpp
struct B {
    int32_t x;
    char c;
}; // sizeof(B) 通常是 8，但如果有更多小成员，将更紧凑

```

Alternatively, we can use `#pragma pack` or `__attribute__((packed))` to forcibly remove padding, but note:

- After removing padding, reading unaligned members can cause severe performance degradation or hardware exceptions on some architectures.
- Only use this when we clearly understand the consequences and must save space.

#### Relationship with DMA / Cachelines

- DMA requires buffers to be aligned to peripheral requirements (for example, 32 bytes). Misalignment can cause hardware rejection or severe performance degradation.
- Aligning to a cacheline (typically 32/64 bytes) helps avoid false sharing and cache thrashing, which is especially important in multi-core systems or when concurrently accessing memory with DMA.

------

## The Non-Determinism of Dynamic Memory: Time and Repeatability Issues

- **Non-deterministic allocation/deallocation time**: General-purpose heap implementations use complex data structures (free lists, trees, bitmaps), making the execution time of `malloc`/`free` unpredictable, potentially with long-tail latency.
- **Concurrency and lock contention**: In multi-threaded environments, the heap typically requires locks or thread-local caches (TLC); lock contention impacts real-time performance.
- **Irrecoverable fragmentation**: For standard C/C++ heaps, once fragmentation forms, it is difficult to recover in linear time. We must resolve it through a reboot or specialized compaction strategies (which are usually impractical).

Embedded systems are especially sensitive to this: long-tail latency can lead to dropped frames, control timeouts, or safety issues.

------

## Common Embedded Alternatives and Hybrid Strategies

So what do we do? Let's quickly cover a few common strategies:

#### Memory Pool (Pool / Slab)

- Divide memory into fixed-size blocks (for example, 32B, 64B, 256B). Allocation returns a block index or pointer, and deallocation returns the block to a free list.
- Pros: Allocation and deallocation in constant time (O(1)), no external fragmentation (as long as all object sizes match a specific pool).
- Cons: Requires multiple pools for different object sizes, memory utilization depends on allocation granularity, and it generates internal fragmentation.

#### Bump / Arena Allocator (Linear Allocator)

- Allocates linearly from a contiguous buffer; deallocation is typically all-at-once (the entire arena resets).
- Extremely fast with no fragmentation; suitable for objects with consistent lifetimes (for example, temporary objects during a single task or initialization phase).
- Not suitable for objects that require arbitrary deallocation.

#### Slab Allocation (Linux Style)

- Suitable for caching identical object types (kernel objects). It can reuse already-initialized objects upon deallocation, reducing construction and destruction overhead.

#### Object Pool + RAII (C++ Style)

- Combine a memory pool with `std::unique_ptr<T, Deleter>` or custom smart pointers to guarantee exception safety and automatic deallocation.

------

## Code Examples
