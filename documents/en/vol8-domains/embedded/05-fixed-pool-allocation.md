---
chapter: 5
cpp_standard:
- 11
- 14
- 17
- 20
description: Fixed-size memory pool allocator
difficulty: intermediate
order: 5
platform: stm32f1
prerequisites:
- 'Chapter 3: 内存与对象管理'
reading_time_minutes: 4
tags:
- cpp-modern
- intermediate
- stm32f1
title: Fixed pool allocation
---
# Embedded C++ Tutorial: Slab / Arena Implementation and Comparison

We now continue our exploration of memory allocation using fixed pools, slabs, and arenas—specifically **Slab** and **Arena (Bump / Region)**. While some of these concepts belong to the operating system level, understanding them is always beneficial!

## TL;DR

- **Fixed pool**: Fixed-size objects, ultra-low fragmentation, commonly used for drivers/object pools; simple implementation with O(1) allocation and deallocation.
- **Slab**: Commonly used in kernels and complex embedded systems, supports multiple object size-classes, reduces memory fragmentation, and is easy to optimize for cache locality.
- **Arena (Region)**: Ideal for short-lived objects or one-off allocation scenarios (e.g., parsing, startup phases), allocation is fast (just moving a pointer), and deallocation happens all at once `reset()`.

## Slab Allocator

The Linux kernel's slab concept is particularly well-suited for optimizing multiple object sizes: it maintains one or more slabs for each size-class (essentially a set of fixed-size object pools), tracks object usage, and optimizes object construction/destruction (caching warm objects). While a microcontroller might not be suited for a heavyweight slab, we can mimic a similar design and implement a simplified memory management mechanism for higher-end chips.

Simply put, we define several size-classes (e.g., 16B, 32B, 64B, 128B...), and each size-class manages multiple slabs. A slab is a contiguous block of memory divided into N object slots. A slab itself can be in one of three states: `empty` (completely empty), `partial` (partially used), or `full` (no free slots). When allocating, we take a slot from a `partial` or `empty` slab; when deallocating, we add it back to the slab's free list.

This might not seem like much. However, we can now implement adjacent merging strategies to prevent memory from being scattered everywhere. We can also quickly match the nearest size. Since everything is pooled, we can perform specialized optimizations for different object types (construction/destruction caching, debug headers).

#### A Simplified Version

For brevity, we implement a simplified slab:

- Statically defined size-classes (selecting the appropriate bucket at runtime)
- Each slab uses a contiguous block of memory and a bitmap/linked list to manage free slots

#### Simplified Key Structures

```cpp
struct Slab {
    uint8_t* data; // 指向对象存储区
    uint32_t freeBitmap; // 仅示例，最多32个 slot
    Slab* next;
};

struct SlabBucket {
    size_t objSize;
    Slab* partial;
    Slab* full;
    Slab* empty;
};

```

> A real-world system would require more complex bitmaps, locking strategies, and expansion mechanisms, but this example is sufficient for embedded scenarios.

------

## Arena (Region / Bump Allocator)

Arenas are commonly found in parsers, one-off allocation tasks, initialization phases, or short-lived object pools. Its core is ridiculously simple:

- Grab a large block of memory (or multiple chunks)
- Use a pointer `head` to track the current allocation position
- `alloc(size)` simply moves `head` forward by `size` and returns the old position
- `reset()` resets `head` back to the initial position (deallocating all allocations at once)

As a result, Arena allocation is extremely fast (pointer arithmetic), highly suitable for temporary memory, and has zero/low fragmentation. However, there are also several drawbacks:

- Individual objects cannot be deallocated separately (unless more complex reclamation strategies are implemented)
- External lifetime management is the user's responsibility

```cpp
class Arena {
public:
    Arena(void* buffer, size_t size) : base_(reinterpret_cast<uint8_t*>(buffer)), cap_(size), head_(0) {}
    void* alloc(size_t n, size_t align = alignof(std::max_align_t)) {
        size_t cur = reinterpret_cast<size_t>(base_) + head_;
        size_t aligned = (cur + (align - 1)) & ~(align - 1);
        size_t offset = aligned - reinterpret_cast<size_t>(base_);
        if (offset + n > cap_) return nullptr;
        head_ = offset + n;
        return base_ + offset;
    }
    void reset() { head_ = 0; }
private:
    uint8_t* base_;
    size_t cap_;
    size_t head_;
};

```

Of course, the code above is not thread-safe, which is important to keep in mind.

------

## Code Examples
