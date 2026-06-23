---
chapter: 7
cpp_standard:
- 11
- 17
- 20
description: 'Here is the translation, formatted as a description suitable for documentation
  or a course syllabus:


  An in-depth look at custom allocators: mechanisms and trade-offs of Bump, Pool,
  and Stack strategies; placement new and object construction/destruction; the C++17
  `std::pmr` `memory_resource` hierarchy (`monotonic`/`pool`) and PMR containers;
  and when to manage memory manually.'
difficulty: advanced
order: 13
platform: host
reading_time_minutes: 7
related:
- vector 深入：三指针、扩容与迭代器失效
tags:
- host
- cpp-modern
- advanced
- 内存管理
- 容器
title: 'Custom Allocators & PMR: Managing Memory Yourself'
translation:
  source: documents/vol3-standard-library/containers/13-custom-allocators.md
  source_hash: c7a1da24b0d9d6a7fbfa5dccbd29e3c5b2513eced131f027cf5a54c478d84293
  translated_at: '2026-06-16T04:01:25.767452+00:00'
  engine: anthropic
  token_count: 1662
---
# Custom Allocators & PMR: Managing Your Own Memory

## Why We Need Custom Allocators

Default `new`/`malloc` are convenient, but they have several weaknesses: indeterminate allocation timing (potentially blocking real-time tasks), heap fragmentation, poor locality, and a one-size-fits-all approach. When you encounter these requirements, default allocators fall short—real-time tasks cannot be stalled by sporadic `malloc` calls, you might want to allocate everything at startup to avoid runtime allocation, you need high-frequency allocation of fixed-size small objects, or you want to dedicate a large block of memory to a specific module for easier tracking. In these scenarios, managing your own memory becomes an essential skill for engineers.

Allocators essentially do two things: **allocate** (provide unused memory) and **deallocate** (return it). In C++, you also handle alignment and object construction/destruction. First, let's look at three classic strategies to understand the mechanisms, then we'll look at the C++17 standard library solution: `std::pmr`.

## Three Classic Allocation Strategies

### Bump (Linear) Allocator

The simplest allocator: maintain a pointer, move it up to allocate, and do not support individual deallocation (only a global reset). Allocation is O(1), making it suitable for startup or short-cycle tasks.

```cpp
#include <cstddef>
#include <cstdint>
#include <new>

class BumpAllocator {
    char* start_;
    char* ptr_;
    char* end_;
public:
    BumpAllocator(void* buffer, std::size_t size)
        : start_(static_cast<char*>(buffer)),
          ptr_(start_),
          end_(start_ + size) {}

    void* allocate(std::size_t n, std::size_t align = alignof(std::max_align_t)) noexcept
    {
        std::uintptr_t p = reinterpret_cast<std::uintptr_t>(ptr_);
        std::size_t mis = p % align;
        std::size_t offset = mis ? (align - mis) : 0;
        if (n + offset > static_cast<std::size_t>(end_ - ptr_)) {
            return nullptr;
        }
        ptr_ += offset;
        void* res = ptr_;
        ptr_ += n;
        return res;
    }

    void reset() noexcept { ptr_ = start_; }
};
```

It cannot deallocate individual objects (unless you add tagging/rollback), but the implementation is extremely simple and fast. It fits scenarios where you "allocate a bunch, use them, and reset everything at once."

### Fixed-Size Memory Pool (Free-list)

For a large number of small objects of the same size (message nodes, connection objects), use a fixed-size pool: each slot has a fixed size, and when deallocated, the slot is hooked back onto the free list. Allocation/deallocation are both O(1) with minimal fragmentation.

```cpp
class SimpleFixedPool {
    struct Node { Node* next; };
    void* buffer_;
    Node* free_head_;
    std::size_t slot_size_;
public:
    SimpleFixedPool(void* buf, std::size_t slot_size, std::size_t count)
        : buffer_(buf), free_head_(nullptr),
          slot_size_(slot_size < sizeof(Node*) ? sizeof(Node*) : slot_size)
    {
        char* p = static_cast<char*>(buffer_);
        for (std::size_t i = 0; i < count; ++i) {
            Node* n = reinterpret_cast<Node*>(p + i * slot_size_);
            n->next = free_head_;
            free_head_ = n;
        }
    }
    void* allocate() noexcept
    {
        if (!free_head_) return nullptr;
        Node* n = free_head_;
        free_head_ = n->next;
        return n;
    }
    void deallocate(void* p) noexcept
    {
        Node* n = static_cast<Node*>(p);
        n->next = free_head_;
        free_head_ = n;
    }
};
```

`slot_size` must include alignment and control information; thread safety requires locks or lock-free mechanisms.

### Stack (LIFO) Allocator

When allocation/deallocation follows a Last-In-First-Out (LIFO) pattern, this is fastest. It supports "mark + rollback to mark." Ideal for frame allocation (allocate per frame, reclaim uniformly at frame end) or short-lived chains. Its `allocate` is similar to Bump (move pointer up + align), adding `mark`/`rollback`:

```cpp
class StackAllocator {
    char* start_;
    char* top_;
    char* end_;
public:
    using Marker = char*;
    StackAllocator(void* buf, std::size_t size)
        : start_(static_cast<char*>(buf)), top_(start_), end_(start_ + size) {}
    // allocate 同 Bump（指针上移 + 对齐处理），略
    Marker mark() noexcept { return top_; }
    void rollback(Marker m) noexcept { top_ = m; }
};
```

The trade-off between the three strategies: Bump is simplest but lacks single deallocation; Pool fits fixed-size high-frequency usage; Stack fits LIFO lifecycles. They all solve the problem of "how to efficiently manage a pre-allocated block of memory."

## Placement New & Object Construction/Destruction

Allocators only provide raw memory (bytes); object construction/destruction is your responsibility—use placement new to construct and explicitly call the destructor:

```cpp
#include <new>
#include <utility>

template<typename T, typename Alloc, typename... Args>
T* construct_with(Alloc& a, Args&&... args)
{
    void* mem = a.allocate(sizeof(T), alignof(T));
    if (!mem) return nullptr;
    return new (mem) T(std::forward<Args>(args)...);
}

template<typename T, typename Alloc>
void destroy_with(Alloc& a, T* obj) noexcept
{
    if (!obj) return;
    obj->~T();
    a.deallocate(static_cast<void*>(obj));
}
```

Remember: **Allocation ≠ Construction**. `allocate` gives memory, `new` constructs; `destroy` destructs, `deallocate` returns memory. This four-step process of "allocate / construct / destroy / deallocate" is the core of both hand-written allocators and the standard library allocator concept.

## The Standard Library Solution: std::pmr (C++17)

Writing allocators by hand helps you understand the mechanisms, but if you really want to use "your own allocation strategy" in STL containers, writing a full `std::allocator` compatible type (a bunch of typedefs, `allocate`) is tedious. C++17 offers a better solution: **std::pmr (polymorphic memory resource)**.

The core of pmr is `std::pmr::memory_resource`—an abstract base class providing `do_allocate`/`do_deallocate` interfaces (you inherit from it to implement your own strategy). The standard library comes with several ready-made implementations:

- `std::pmr::monotonic_buffer_resource`: The Bump allocator mentioned earlier, allocating linearly on a stack/static buffer. Extremely fast, no individual deallocation, suitable for frame allocation or one-off tasks.
- `std::pmr::unsynchronized_pool_resource` / `synchronized_pool_resource`: Fixed-size pools, suitable for large numbers of same-size small objects (use the synchronized version for multithreading).
- `std::pmr::null_memory_resource`: Borrows but never returns, used for "prohibit allocation from here on" scenarios.

Then there are **pmr containers**: `std::pmr::vector`, `std::pmr::string`, `std::pmr::list`, etc. Internally they use `std::pmr::polymorphic_allocator`, and you pass a `memory_resource`* upon construction. You can change the allocation strategy without changing the container type (they are all `std::pmr::vector`), just swap the resource. This is pmr's biggest advantage over hand-written allocator templates: **type erasure, runtime strategy switching**.

```cpp
#include <memory_resource>
#include <vector>
#include <cstdint>

std::byte buffer[4096];
std::pmr::monotonic_buffer_resource mbr(buffer, sizeof(buffer));
std::pmr::vector<int> v(&mbr);   // v 的内存来自 buffer，不走全局堆
```

## Let's Run It: pmr::vector with monotonic buffer

Let's run this to confirm that `pmr::vector` actually allocates from a stack buffer:

```cpp
#include <memory_resource>
#include <vector>
#include <iostream>
#include <cstdint>

int main()
{
    // 栈上一块 buffer，用 monotonic_buffer_resource 当分配源
    std::byte buffer[4096];
    std::pmr::monotonic_buffer_resource mbr(buffer, sizeof(buffer));

    // pmr::vector 从这块 buffer 分配，不走全局堆
    std::pmr::vector<int> v(&mbr);
    for (int i = 0; i < 100; ++i) {
        v.push_back(i);
    }
    std::cout << "v.size() = " << v.size() << "\n";
    std::cout << "vector 的内存来自栈上 buffer，零全局堆分配\n";
    return 0;
}
```

```bash
g++ -std=c++20 -O2 -o /tmp/pmr_test /tmp/pmr_test.cpp && /tmp/pmr_test
```

```text
v.size() = 100
vector 的内存来自栈上 buffer，零全局堆分配
```

This vector's elements come entirely from that 4096-byte stack buffer; there isn't a single global `new`/`malloc`. This is the typical usage of pmr + monotonic: feed a block of pre-allocated memory (stack, static area, or self-managed heap block) to a container to gain deterministic allocation behavior, zero fragmentation, and zero global heap overhead. Swap the resource (e.g., to a pool) to swap strategies without changing a single line of container code.

## Wrapping Up

The core of custom allocators is "managing the allocation/deallocation of a block of memory yourself." Three classic strategies—Bump (fast, no single deallocation), Pool (fixed-size high-frequency), and Stack (LIFO)—each have their use cases. Once you understand them, the preferred way to use them in the STL is C++17's `std::pmr`: `memory_resource` abstraction + standard implementations (monotonic/pool) + pmr containers for runtime strategy switching and type explosion avoidance. Hand-written allocators are useful for understanding mechanisms or for special needs not covered by pmr; for常规 scenarios, pmr is sufficient. This concludes our container arc; in the next article, we will shift to the standard library's iterator and algorithms system.

Want to run it and see the results immediately? Open the online example below (you can run it and view the assembly):

<OnlineCompilerDemo
  title="Custom Allocators: Bump Arena & std::pmr"
  source-path="code/examples/vol3/13_custom_allocators.cpp"
  description="Hand-written linear allocator prototype, std::pmr::monotonic_buffer_resource makes vector allocate on stack buffer"
  allow-run
/>

## Reference Resources

- [std::pmr (memory_resource) — cppreference](https://en.cppreference.com/w/cpp/memory/resource)
- [monotonic_buffer_resource — cppreference](https://en.cppreference.com/w/cpp/memory/monotonic_buffer_resource)
- [polymorphic_allocator — cppreference](https://en.cppreference.com/w/cpp/memory/polymorphic_allocator)
