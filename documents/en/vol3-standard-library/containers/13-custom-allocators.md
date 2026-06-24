---
chapter: 7
cpp_standard:
- 11
- 17
- 20
description: 'Deep dive into custom allocators: mechanisms and trade-offs of Bump/Pool/Stack
  strategies, placement new and object construction/destruction, the C++17 `std::pmr`
  `memory_resource` system (`monotonic`/`pool`) and `pmr` containers, and when to
  manage memory yourself.'
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
  source_hash: 3d6f35e9607d6d59e654176774a067b00942b0b84c8d013328c78c3e05e31382
  translated_at: '2026-06-24T00:37:18.709278+00:00'
  engine: anthropic
  token_count: 1820
---
# Custom Allocators & PMR: Managing Your Own Memory

## Why We Need Custom Allocators

The default `new` / `malloc` are convenient, but they have some weaknesses: allocation timing is non-deterministic (potentially blocking real-time tasks), they cause heap fragmentation, they suffer from poor locality, and they apply a "one size fits all" approach. When you encounter requirements like these, the default allocators fall short—real-time tasks cannot be stalled by sporadic malloc calls, you might want to allocate everything once during startup to avoid runtime allocation, you need high-frequency allocation of small fixed-size objects, or you want to dedicate a large block of memory to a specific module for easier tracking. In these scenarios, managing your own memory becomes an essential skill for engineers.

Allocators essentially do two things: **allocate** (hand out unused memory) and **deallocate** (reclaim it). In C++, we also need to handle alignment and object construction/destruction. We will first look at three classic strategies to understand the mechanisms, and then examine the C++17 standard library solution: `std::pmr`.

## Three Classic Allocation Strategies

### Bump (Linear) Allocator

The simplest allocator: maintain a pointer, move it up to allocate, and do not support individual deallocation (only a global reset). Allocation is O(1), making it suitable for startup phases or short-lived tasks.

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

Cannot deallocate individual objects (unless we add bookkeeping/rollback), but the implementation is extremely simple and fast. Ideal for "allocate a batch, use it, then reset everything at once" scenarios.

### Fixed-size Memory Pool (Free-list)

For many small objects of the same size (message nodes, connection objects), use a fixed-size pool: each slot has a fixed size, and upon deallocation, we link the slot back to the free list. Both allocation and deallocation are O(1), with minimal fragmentation.

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

`slot_size` must include padding and control information; to achieve thread safety, we must add locks or make it lock-free.

### Stack (LIFO) Allocator

Allocation and deallocation are fastest when they follow a Last-In-First-Out (LIFO) pattern, supporting "mark + rollback to mark". This is suitable for frame allocation (allocate per frame, reclaim uniformly at frame end) and short-lived chains. Its `allocate` behaves like Bump (move pointer up + align), adding `mark` and `rollback`:

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

Trade-offs among the three strategies: Bump is the simplest but does not support individual deallocation; Pool is suitable for fixed-size, high-frequency allocations; Stack fits LIFO lifecycles. They all solve the problem of "how to efficiently manage a pre-allocated memory block."

## Placement new and object construction/destruction

Allocators only provide raw memory (bytes); object construction and destruction are your responsibility—use placement new for construction and explicitly call the destructor:

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

Remember: **allocation is not construction**. `allocate` provides memory, while `new (mem) T(...)` constructs the object; `obj->~T()` destroys it, and `deallocate` returns the memory. This four-step process of "allocate / construct / destroy / deallocate" is the core concept behind custom allocators and the standard library allocator.

## The Standard Library Solution: std::pmr (C++17)

Writing a custom allocator helps you understand the underlying mechanisms, but actually using "your own allocation strategy" within STL containers by implementing a fully `std::allocator`-compatible type (with a bunch of typedefs and `rebind`) is tedious. C++17 offers a better solution: **std::pmr (polymorphic memory resource)**.

The core of pmr is `std::pmr::memory_resource`—an abstract base class that provides `allocate` and `deallocate` interfaces (which you inherit to implement your own strategy). The standard library includes several ready-made implementations:

- `monotonic_buffer_resource`: This is the Bump allocator mentioned earlier. It performs linear allocation on a stack or static buffer. It is extremely fast, does not free individual blocks, and is suitable for frame allocation or one-off tasks.
- `synchronized_pool_resource` / `unsynchronized_pool_resource`: Fixed-size pools suitable for large numbers of small objects of the same size (use the synchronized version in multi-threaded contexts).
- `null_memory_resource`: Borrows memory but never returns it, used for scenarios where "allocation is prohibited thereafter."

Then there are **pmr containers**: `std::pmr::vector<T>`, `std::pmr::string`, `std::pmr::map`, and so on. Internally, they use `polymorphic_allocator` and accept a `memory_resource*` upon construction. You can change the allocation strategy without changing the container type (they are all `pmr::vector`); you simply swap the resource. This is the biggest advantage of pmr compared to handwritten allocator templates: **type erasure and runtime strategy switching**.

```cpp
#include <memory_resource>
#include <vector>
#include <cstdint>

std::byte buffer[4096];
std::pmr::monotonic_buffer_resource mbr(buffer, sizeof(buffer));
std::pmr::vector<int> v(&mbr);   // v 的内存来自 buffer，不走全局堆
```

## Let's Run It: `pmr::vector` with a Monotonic Buffer

Let's run this to verify that `pmr::vector` actually allocates from the stack buffer:

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

The address of `v.data()`, `0x7fff303c500c`, falls squarely within the stack buffer range `[0x7fff303c4e10, 0x7fff303c5e10]`—this is hard proof of "zero global heap allocation." While stack addresses change between runs, `v.data()` always lands within the buffer interval.

> This printout, which verifies "zero heap allocation" by comparing `v.data()` against the stack buffer range, was contributed by [@YukunJ](https://github.com/YukunJ) in [PR #77](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/pull/77).

All elements of this vector originate from that 4096-byte stack buffer, without a single global `new`. This is the typical usage of pmr + monotonic: feeding a pre-allocated memory block (stack, static memory, or a self-managed heap block) to containers yields deterministic allocation behavior, zero fragmentation, and zero global heap overhead. Swapping the resource (e.g., to a pool) changes the strategy without altering a single line of container code.

## Wrapping Up

The core of custom allocators is "managing the allocation and deallocation of a memory block yourself." Three classic strategies—Bump (fast, no single free), Pool (fixed size, high frequency), and Stack (LIFO)—each have their use cases. Once we understand them, the preferred way to use them in the STL is C++17's `std::pmr`: the `memory_resource` abstraction combined with standard implementations (monotonic/pool) and pmr containers allows for runtime strategy switching without type explosion. Hand-writing allocators is useful for understanding the mechanism or for specific needs not covered by pmr; for general scenarios, pmr is sufficient. This concludes our deep dive into containers. In the next article, we will shift our focus to the standard library's iterator and algorithm architecture.

Want to run it and see the effect immediately? Open the online example below (you can run it and view the assembly):

<OnlineCompilerDemo
  title="Custom Allocators: Bump Arena & std::pmr"
  source-path="code/examples/vol3/13_custom_allocators.cpp"
  description="Hand-written linear allocator prototype, using std::pmr::monotonic_buffer_resource to make vector allocate on a stack buffer"
  allow-run
/>

## References

- [std::pmr (memory_resource) — cppreference](https://en.cppreference.com/w/cpp/memory/resource)
- [monotonic_buffer_resource — cppreference](https://en.cppreference.com/w/cpp/memory/monotonic_buffer_resource)
- [polymorphic_allocator — cppreference](https://en.cppreference.com/w/cpp/memory/polymorphic_allocator)
