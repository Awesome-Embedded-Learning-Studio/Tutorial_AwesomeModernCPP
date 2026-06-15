---
chapter: 7
cpp_standard:
- 11
- 14
- 17
- 20
description: Custom STL allocator
difficulty: intermediate
order: 6
platform: host
prerequisites:
- 'Chapter 6: RAII与智能指针'
reading_time_minutes: 8
tags:
- cpp-modern
- host
- intermediate
title: Custom Allocator
translation:
  source: documents/vol3-standard-library/13-custom-allocators.md
  source_hash: 73c2d198fac8c850e3849d204355a05b5b7fa2c4e94f8e5d380b349e37d03617
  translated_at: '2026-05-26T11:37:53.288166+00:00'
  engine: anthropic
  token_count: 1185
---
# Modern C++ for Embedded Systems Tutorial — Custom Allocators

In the embedded world, memory isn't an "infinite" set of drawers, but rather a suitcase that always seems to complain about how much space you're taking up. Are the default `new` / `malloc` friendly to us? Sometimes they are (yes, they're convenient); but more often, they are latent performance bombs, sources of unpredictable latency, and breeding grounds for fragmentation. Therefore, writing a "custom allocator"—your own memory management strategy—becomes an essential rite of passage for engineers.

------

## Why Customize an Allocator?

Imagine these scenarios: a real-time task cannot be blocked by sporadic `malloc`; the startup phase requires allocating several objects at once to avoid runtime allocation; small objects are allocated frequently but at a constant size; or you want to carve out a large block of memory for a specific module to make tracking and reclamation easier. Default allocators often fail to satisfy all of these simultaneously: determinism, low memory footprint, low fragmentation, and high performance.

Custom allocators modify the specific patterns of memory requests, allowing us to plug in our own fixed-size pools, stack allocators, or fast allocators. As discussed in previous blog posts, these implementations can effectively avoid heap fragmentation and improve locality.

------

## Fundamental Concepts of Allocators

An allocator, at its core, boils down to two things: **allocation** (providing a chunk of unused memory) and **deallocation** (returning memory to the pool). In C++, we also need to pay attention to alignment and object construction/destruction (placement `new`, explicit `destroy`).

Common strategies include: **Bump (pointer-bumping) allocators**, **Free-list (memory pool) allocators**, **Stack allocators**, and more complex approaches like **TLSF / hierarchical bitmaps**. Let's compare them directly through code.

------

## The Simplest: Bump (Linear) Allocator — A Great Friend for Startup and Temporary Use Cases

Characteristics: Extremely simple to implement, O(1) allocation, does not support individual object deallocation (but can be reset all at once). Suitable for startup-phase allocation or short-cycle tasks.

```cpp
// bump_allocator.h - 非线程安全，简单演示
#include <cstddef>
#include <new>
#include <cassert>

class BumpAllocator {
    char* start_;
    char* ptr_;
    char* end_;
public:
    BumpAllocator(void* buffer, std::size_t size)
      : start_(static_cast<char*>(buffer)), ptr_(start_), end_(start_ + size) {}

    void* allocate(std::size_t n, std::size_t align = alignof(std::max_align_t)) noexcept {
        std::size_t space = end_ - ptr_;
        std::uintptr_t p = reinterpret_cast<std::uintptr_t>(ptr_);
        std::size_t mis = p % align;
        std::size_t offset = mis ? (align - mis) : 0;
        if (n + offset > space) return nullptr;
        ptr_ += offset;
        void* res = ptr_;
        ptr_ += n;
        return res;
    }

    void reset() noexcept { ptr_ = start_; }
};

```

Use cases: Allocating all necessary objects at startup with no subsequent deallocation, or temporary buffer pools. Remember: you cannot deallocate individual objects unless you support rolling back to a specific snapshot point (implementing a "mark/rollback" mechanism).

------

## Fixed-Size Memory Pool (Free-list)

When you have a large number of small objects of the same size (such as message nodes or connection objects), a fixed-size memory pool is highly efficient. Each slot has a fixed size, and upon deallocation, the slot is pushed back onto the free list. Both allocation and deallocation are O(1).

```cpp
// simple_pool.h - 单线程示例
#include <cstddef>
#include <cassert>
#include <cstdint>

class SimpleFixedPool {
    struct Node { Node* next; };
    void* buffer_;
    Node* free_head_;
    std::size_t slot_size_;
    std::size_t slot_count_;
public:
    SimpleFixedPool(void* buf, std::size_t slot_size, std::size_t count)
      : buffer_(buf), free_head_(nullptr), slot_size_((slot_size < sizeof(Node*))? sizeof(Node*): slot_size), slot_count_(count) {
        // 初始化空闲链表
        char* p = static_cast<char*>(buffer_);
        for (std::size_t i = 0; i < slot_count_; ++i) {
            Node* n = reinterpret_cast<Node*>(p + i * slot_size_);
            n->next = free_head_;
            free_head_ = n;
        }
    }
    void* allocate() noexcept {
        if (!free_head_) return nullptr;
        Node* n = free_head_;
        free_head_ = n->next;
        return n;
    }
    void deallocate(void* p) noexcept {
        Node* n = static_cast<Node*>(p);
        n->next = free_head_;
        free_head_ = n;
    }
};

```

Key takeaway: `slot_size` should include alignment and control information; when thread safety is required, you need to add locks or use lock-free structures (increasing complexity). Memory utilization is high, and fragmentation is low.

------

## Stack Allocator — The Holy Grail for LIFO Scenarios

When your allocation/deallocation pattern follows LIFO (Last-In, First-Out), a stack allocator is the fastest, allowing you to deallocate a series of allocations up to a specific "marker."

```cpp
// stack_allocator.h - 支持标记回滚
class StackAllocator {
    char* start_;
    char* top_;
    char* end_;
public:
    StackAllocator(void* buf, std::size_t size) : start_(static_cast<char*>(buf)), top_(start_), end_(start_+size) {}
    void* allocate(std::size_t n, std::size_t align = alignof(std::max_align_t)) noexcept {
        // 类似Bump的对齐处理
        // ...
    }
    // 标记与回滚API
    using Marker = char*;
    Marker mark() noexcept { return top_; }
    void rollback(Marker m) noexcept { top_ = m; }
};

```

Applicable to: short-lived chains, task stack allocation, and frame allocation (allocate per frame, reclaim uniformly at the end of the frame).

------

## Wrapping in C++ Style (placement new and Destructors)

Allocators only provide raw memory; constructing and destructing objects is still your job. Here is an example:

```cpp
#include <new> // placement new

// allocate memory for T and construct
template<typename T, typename Alloc, typename... Args>
T* construct_with(Alloc& a, Args&&... args) {
    void* mem = a.allocate(sizeof(T), alignof(T));
    if (!mem) return nullptr;
    return new (mem) T(std::forward<Args>(args)...);
}

// 销毁并归还内存（手动调用析构）
template<typename T, typename Alloc>
void destroy_with(Alloc& a, T* obj) noexcept {
    if (!obj) return;
    obj->~T();
    a.deallocate(static_cast<void*>(obj));
}

```

Important: In embedded systems, disabling exceptions or using `noexcept`'s allocate in exception-sensitive code is a common practice; therefore, many implementations return `nullptr` instead of throwing exceptions.

------

## How to Use Custom Allocators with the STL

The standard library's `std::allocator` interface is rather cumbersome in older standards. C++17/20 introduced `std::pmr::memory_resource` (more modern) to replace the default allocation strategy. However, in embedded development, we often don't enable the full `<memory_resource>`, so you can:

- Write a simple wrapper for containers, using your pool internally to allocate nodes.
- Or implement a class compatible with the `std::allocator` interface (which requires a bunch of typedefs and `rebind`), and then pass it to `std::vector<T, MyAlloc<T>>`.

If your build environment allows it, prioritize `std::pmr` — its semantics are clearer, but the overhead and support level depend on your platform.
