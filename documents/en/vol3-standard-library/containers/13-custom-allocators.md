---
chapter: 7
cpp_standard:
- 11
- 17
- 20
description: 'Custom allocators explained end to end: the mechanics and trade-offs of the Bump/Pool/Stack strategies, placement new and object construction/destruction, the C++17 `std::pmr` `memory_resource` system (`monotonic`/`pool`) and `pmr` containers, and when you should manage memory yourself.'
difficulty: advanced
order: 13
platform: host
reading_time_minutes: 7
related:
- 'Deep Dive into vector: Three Pointers, Reallocation, and Iterator Invalidation'
tags:
- host
- cpp-modern
- advanced
- 内存管理
- 容器
title: 'Custom Allocators and PMR: Managing Memory Yourself'
translation:
  source: documents/vol3-standard-library/containers/13-custom-allocators.md
  source_hash: 3d6f35e9607d6d59e654176774a067b00942b0b84c8d013328c78c3e05e31382
  translated_at: '2026-09-26T02:40:34+00:00'
  engine: anthropic
  token_count: 4100
---
# Custom Allocators and PMR: Managing Memory Yourself

## Why We Need Custom Allocators

The default `new` / `malloc` are convenient, but they have a few soft spots: allocation timing is non-deterministic (it can stall a real-time task), they fragment the heap, locality is poor, and they take a one-size-fits-all approach to every workload. Once you run into requirements like these, the default allocator starts falling short: a real-time task can't afford to be dragged down by an occasional malloc; you want to allocate everything in one shot during startup and avoid allocating at runtime; you're allocating small fixed-size objects at high frequency; or you want to carve out one big block of memory for a specific module so it's easier to track. In scenarios like these, managing memory yourself becomes part of an engineer's basic training.

At the end of the day, an allocator does two things: **allocate** (hand out a stretch of unused memory) and **deallocate** (give it back). In C++, we additionally have to take care of alignment and of object construction/destruction. Below we'll first look at three classic strategies to understand the mechanics; then at the standard library's answer as of C++17, `std::pmr`.

## Three Classic Allocation Strategies

### Bump (Linear) Allocator

The simplest allocator there is: keep one pointer, bump it up on each allocation, and don't support freeing individual objects (the only way out is a wholesale reset). Allocation is O(1), a good fit for startup phases or short-cycle tasks.

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

It can't free individual objects (unless you add markers/rollback), but the implementation is minimal and extremely fast. A good fit for the "allocate a pile, then reset it all in one go when you're done" pattern.

### Fixed-Size Memory Pool (Free-list)

For large numbers of same-sized small objects (message nodes, connection objects), use a fixed-size pool: every slot has the same size, and deallocation simply hangs the slot back onto the free list. Allocation and deallocation are both O(1), and fragmentation stays low.

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

`slot_size` must cover alignment and control information; for thread safety you have to add a lock or go lock-free.

### Stack (LIFO) Allocator

This one is fastest when allocations and frees come in last-in-first-out order, and it supports "mark + roll back to the mark". It suits frame allocation (allocate during the frame, reclaim everything together at frame end) and chains of short-lived objects. Its `allocate` is the same as Bump's (pointer moves up + alignment), with mark / rollback added on top:

```cpp
class StackAllocator {
    char* start_;
    char* top_;
    char* end_;
public:
    using Marker = char*;
    StackAllocator(void* buf, std::size_t size)
        : start_(static_cast<char*>(buf)), top_(start_), end_(start_ + size) {}
    // allocate is the same as Bump (pointer bump-up + alignment handling), omitted
    Marker mark() noexcept { return top_; }
    void rollback(Marker m) noexcept { top_ = m; }
};
```

The trade-offs among the three: Bump is the simplest but doesn't support individual frees; Pool fits fixed-size, high-frequency allocation; Stack fits LIFO lifetimes. What they all solve is "how to efficiently manage one pre-allocated block of memory".

## placement new and Object Construction/Destruction

The allocator only hands you raw memory (bytes); constructing and destructing the objects is your business — construct with placement new, and call the destructor explicitly:

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

Remember: **allocation ≠ construction**. `allocate` gives you memory, and only `new (mem) T(...)` constructs; `obj->~T()` destructs, and `deallocate` returns the memory. This four-step cycle — allocate / construct / destruct / deallocate — is the kernel of both hand-written allocators and the standard library's allocator concept.

## The Standard Library's Answer: std::pmr (C++17)

Hand-written allocators help you understand the mechanics, but when you actually want to use "your own allocation strategy" inside STL containers, writing a complete `std::allocator`-compatible type (a whole pile of typedefs, `rebind`) gets tedious. C++17 shipped a better answer: **std::pmr (polymorphic memory resource)**.

The core of pmr is `std::pmr::memory_resource` — an abstract base class providing the `allocate` / `deallocate` interface (you inherit from it and implement your own strategy). The standard library ships several ready-made implementations:

- `monotonic_buffer_resource`: exactly the Bump allocator from earlier — it allocates linearly over a stack / static buffer, is extremely fast, never frees individual objects, and suits frame allocation or one-shot tasks.
- `synchronized_pool_resource` / `unsynchronized_pool_resource`: fixed-size pools, suited to large numbers of same-sized small objects (use the synchronized version in multi-threaded code).
- `null_memory_resource`: takes but never gives — used for the "no allocation allowed from here on" scenario.

Then come the **pmr containers**: `std::pmr::vector<T>`, `std::pmr::string`, `std::pmr::map`, and so on. Internally they use `polymorphic_allocator`, and you pass a `memory_resource*` at construction. Swapping the allocation strategy doesn't mean swapping the container type (it's still `pmr::vector`) — you only swap the resource. That is pmr's biggest advantage over hand-written allocator templates: **type erasure, with strategies swappable at runtime**.

```cpp
#include <memory_resource>
#include <vector>
#include <cstdint>

std::byte buffer[4096];
std::pmr::monotonic_buffer_resource mbr(buffer, sizeof(buffer));
std::pmr::vector<int> v(&mbr);   // v's memory comes from buffer, bypassing the global heap
```

## Let's Run It: pmr::vector with a Monotonic Buffer

Let's run it and confirm that pmr::vector really allocates from the stack buffer:

```cpp
#include <memory_resource>
#include <vector>
#include <iostream>
#include <cstdint>

int main()
{
    // a buffer on the stack, with monotonic_buffer_resource as the allocation source
    std::byte buffer[4096];
    std::pmr::monotonic_buffer_resource mbr(buffer, sizeof(buffer));

    // pmr::vector allocates from this buffer, bypassing the global heap
    std::pmr::vector<int> v(&mbr);
    for (int i = 0; i < 100; ++i) {
        v.push_back(i);
    }
    std::cout << "v.size() = " << v.size() << "\n";
    std::cout << "v.data() address = " << std::hex << v.data() << "\n";
    std::cout << "The range of stack buffer is [" << (void*)buffer << ","
              << (void*)(buffer + sizeof(buffer)) << "]\n";
    std::cout << "vector 的内存来自栈上 buffer，零全局堆分配\n";
    return 0;
}
```

```bash
g++ -std=c++20 -O2 -o /tmp/pmr_test /tmp/pmr_test.cpp && /tmp/pmr_test
```

```text
v.size() = 100
v.data() address = 0x7fff303c500c
The range of stack buffer is [0x7fff303c4e10,0x7fff303c5e10]
vector 的内存来自栈上 buffer，零全局堆分配
```

The address of `v.data()`, `0x7fff303c500c`, lands squarely inside the stack buffer range `[0x7fff303c4e10, 0x7fff303c5e10]` — that's hard proof of "zero global-heap allocation". Stack addresses change from run to run, but `v.data()` always falls inside the buffer's range.

> This demonstration — printing `v.data()` alongside the stack buffer range, using the addresses to nail down "zero heap allocation" — was contributed by [@YukunJ](https://github.com/YukunJ) in [PR #77](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/pull/77).

Every element of this vector comes from that 4096-byte buffer on the stack — not a single global `new` anywhere. This is the canonical pmr + monotonic usage: feed a pre-allocated block of memory (on the stack, in static storage, or a heap block you manage yourself) to a container, and you get deterministic allocation behavior, zero fragmentation, and zero global-heap overhead. Swap in a different resource (a pool, say) and you've swapped strategies, without touching a single line of container code.

## A Few Parting Words

The heart of custom allocators is "managing the allocation / deallocation of a block of memory yourself". The three classic strategies — Bump (fast, no individual frees), Pool (fixed-size, high-frequency), Stack (LIFO) — each have their niche. Once you understand them, when it comes to actually using one in the STL, the first choice is C++17's `std::pmr`: a `memory_resource` abstraction plus standard implementations (monotonic / pool) plus pmr containers — strategies swappable at runtime, no type explosion. Hand-written allocators are for understanding the machinery, or for special needs pmr doesn't cover; for everyday scenarios, pmr is enough. That wraps up the main line on containers — in the next article we turn to the standard library's iterators and algorithms.

Want to get your hands on it right away and see the effect? Open the online example below (it runs, and it shows the assembly too):

<OnlineCompilerDemo
  title="Custom allocators: a bump arena and std::pmr"
  source-path="code/examples/vol3/13_custom_allocators.cpp"
  description="A hand-written linear allocator prototype, and std::pmr::monotonic_buffer_resource letting a vector allocate from a stack buffer"
  allow-run
/>

## References

- [std::pmr (memory_resource) — cppreference](https://en.cppreference.com/w/cpp/memory/resource)
- [monotonic_buffer_resource — cppreference](https://en.cppreference.com/w/cpp/memory/monotonic_buffer_resource)
- [polymorphic_allocator — cppreference](https://en.cppreference.com/w/cpp/memory/polymorphic_allocator)
