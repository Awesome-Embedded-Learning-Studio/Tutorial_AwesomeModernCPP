---
title: 'Custom Allocators & PMR: Managing Memory Yourself'
description: 'Deep dive into custom allocators: mechanisms and trade-offs of Bump,
  Pool, and Stack strategies, placement new with object construction and destruction,
  the C++17 `std::pmr` `memory_resource` system (`monotonic`/`pool`) and `pmr` containers,
  and when to manage memory manually.'
chapter: 7
order: 13
tags:
- host
- cpp-modern
- advanced
- 内存管理
- 容器
difficulty: advanced
platform: host
cpp_standard:
- 11
- 17
- 20
reading_time_minutes: 16
related:
- vector 深入：三指针、扩容与迭代器失效
translation:
  source: documents/vol3-standard-library/13-custom-allocators.md
  source_hash: a035d00a57044775e7d5dba72a7de2bb6c5efa0efef3e94f42578aef5907b024
  translated_at: '2026-06-15T09:21:25.554976+00:00'
  engine: anthropic
  token_count: 1666
---
# Custom Allocators & PMR: Managing Your Own Memory

## Why We Need Custom Allocators

Default `new`/`delete` are convenient, but they have weaknesses: indeterminate allocation timing (potentially blocking real-time tasks), heap fragmentation, poor locality, and a one-size-fits-all approach. When you encounter these requirements, default allocators fall short—real-time tasks cannot be stalled by sporadic `malloc` calls, you might want to allocate everything at startup to avoid runtime allocation, you need high-frequency allocation of fixed-size small objects, or you want to dedicate a large block of memory to a specific module for easier tracking. In these scenarios, managing your own memory becomes an essential skill for engineers.

Allocators boil down to two things: **allocation** (giving out unused memory) and **deallocation** (taking it back). In C++, we also handle alignment and object construction/destruction. Let's first look at three classic strategies to understand the mechanisms, then look at the C++17 standard library solution: `std::pmr`.

## Three Classic Allocation Strategies

### Bump (Linear) Allocator

The simplest allocator: maintain a pointer, move it up to allocate, and do not support freeing individual objects (only a global reset). Allocation is O(1), suitable for startup or short-lived tasks.

```cpp
// Bump Allocator: Linear allocation, no individual free
class BumpAllocator {
    void* base;   // Start of memory block
    size_t offset; // Current offset
    size_t size;   // Total size
public:
    BumpAllocator(void* base, size_t size) : base(base), offset(0), size(size) {}

    void* allocate(size_t n, size_t alignment) { // n bytes, alignment alignment
        // Align current offset up
        size_t aligned_offset = (offset + alignment - 1) & ~(alignment - 1);
        if (aligned_offset + n > size) return nullptr; // OOM
        void* ptr = static_cast<char*>(base) + aligned_offset;
        offset = aligned_offset + n;
        return ptr;
    }

    void reset() { offset = 0; } // Reset all allocations
};
```

It cannot free individual objects (unless you add tagging/rollback), but the implementation is extremely simple and fast. It fits scenarios where you "allocate a bunch, use them, and reset everything at once."

### Fixed-Size Memory Pool (Free-list)

For many small objects of the same size (message nodes, connection objects), use a fixed-size pool: each slot is a fixed size, and when freed, the slot is linked back to the free list. Allocation/deallocation are both O(1) with minimal fragmentation.

```cpp
// Fixed-size pool (Free-list)
class PoolAllocator {
    struct Slot { Slot* next; }; // Free list node
    Slot* free_list;
public:
    PoolAllocator(void* base, size_t block_size, size_t count) {
        // Initialize free list: chain all blocks
        free_list = static_cast<Slot*>(base);
        for (size_t i = 0; i < count - 1; ++i)
            free_list[i].next = &free_list[i + 1];
        free_list[count - 1].next = nullptr;
    }

    void* allocate() {
        if (!free_list) return nullptr; // OOM
        Slot* slot = free_list;
        free_list = free_list->next;
        return slot;
    }

    void deallocate(void* ptr) {
        if (!ptr) return;
        Slot* slot = static_cast<Slot*>(ptr);
        slot->next = free_list;
        free_list = slot;
    }
};
```

`Slot` must contain alignment and control information; for thread safety, you need to add locks or go lock-free.

### Stack (LIFO) Allocator

When allocation/deallocation follows a Last-In-First-Out (LIFO) pattern, it's fastest, supporting "mark + rollback to mark." Ideal for frame allocation (allocate per frame, reclaim at frame end) or short-lived chains. Its `allocate` is like Bump (move pointer + align), adding `mark`/`rollback`:

```cpp
// Stack Allocator: LIFO, supports mark/rollback
class StackAllocator {
    void* base;
    size_t offset;
    size_t size;
public:
    size_t mark() const { return offset; } // Save current state

    void rollback(size_t saved_offset) {   // Restore state
        offset = saved_offset;
    }

    void* allocate(size_t n, size_t alignment) {
        size_t aligned_offset = (offset + alignment - 1) & ~(alignment - 1);
        if (aligned_offset + n > size) return nullptr;
        void* ptr = static_cast<char*>(base) + aligned_offset;
        offset = aligned_offset + n;
        return ptr;
    }
};
```

Trade-offs: Bump is simplest but lacks individual free; Pool fits fixed-size high-frequency; Stack fits LIFO lifetimes. They all solve "how to efficiently manage a pre-allocated block of memory."

## Placement New & Object Construction/Destruction

Allocators only give raw memory (bytes); object construction/destruction is your business—use placement new to construct and explicitly call the destructor:

```cpp
// Allocating raw memory vs constructing objects
void* raw = allocator.allocate(sizeof(MyObj), alignof(MyObj)); // 1. Allocate memory
MyObj* obj = new(raw) MyObj(arg1, arg2);                       // 2. Construct object (placement new)
// ... use obj ...
obj->~MyObj();                                                  // 3. Destroy object
allocator.deallocate(raw, sizeof(MyObj));                      // 4. Return memory
```

Remember: **Allocation ≠ Construction**. `allocate` gives memory, `new (ptr) T` constructs; `ptr->~T()` destroys, `deallocate` returns memory. This four-step "allocate / construct / destroy / deallocate" sequence is the core of hand-written allocators and the standard library allocator concept.

## The Standard Library Answer: std::pmr (C++17)

Hand-writing allocators helps you understand the mechanism, but to actually use "your own allocation strategy" in STL containers, writing a full `std::allocator` compatible type (a bunch of typedefs, `allocate()`/`deallocate()`) is tedious. C++17 offers a better solution: **std::pmr (polymorphic memory resource)**.

The core of pmr is `std::pmr::memory_resource`—an abstract base class providing `allocate`/`deallocate` interfaces (you inherit from it to implement your own strategy). The standard library comes with several ready-made implementations:

- `std::pmr::monotonic_buffer_resource`: The Bump allocator mentioned earlier, linear allocation on a stack/static buffer, extremely fast, no individual free, suitable for frame allocation or one-off tasks.
- `std::pmr::unsynchronized_pool_resource` / `synchronized_pool_resource`: Fixed-size pools, suitable for many small objects of the same size (use the synchronized version for multithreading).
- `std::pmr::null_memory_resource`: Borrows but never returns, used for "prohibit allocation from here on" scenarios.

Then there are **pmr containers**: `std::pmr::vector`, `std::pmr::string`, `std::pmr::list`, etc., which use `std::pmr::polymorphic_allocator` internally and accept a `memory_resource*` at construction. You can change the allocation strategy without changing the container type (they are all `std::pmr::vector`), just swap the resource—this is pmr's biggest advantage over hand-written allocator templates: **type erasure, runtime strategy switching**.

```cpp
// Using pmr: runtime pluggable allocator
#include <memory_resource>

// 1. Prepare memory
std::byte buffer[4096];
std::pmr::monotonic_buffer_resource pool{buffer, sizeof(buffer)}; // Strategy: Bump

// 2. Create container using the resource
std::pmr::vector<int> vec{&pool}; // All allocations come from buffer

vec.push_back(42); // No global heap involved
```

## Let's Run It: pmr::vector with monotonic buffer

Let's run this to confirm that `pmr::vector` actually allocates from the stack buffer:

```cpp
#include <memory_resource>
#include <vector>
#include <iostream>

int main() {
    // 1. Reserve stack memory
    std::byte buffer[4096]; // Raw memory on stack

    // 2. Create monotonic_buffer_resource (Bump allocator)
    std::pmr::monotonic_buffer_resource pool{buffer, sizeof(buffer)};

    // 3. Create pmr::vector using this resource
    std::pmr::vector<int> vec{&pool};

    // 4. Push some elements
    for (int i = 0; i < 100; ++i) {
        vec.push_back(i);
    }

    std::cout << "Vector size: " << vec.size() << "\n";
    std::cout << "Buffer address: " << (void*)buffer << "\n";
    std::cout << "Vector data address: " << vec.data() << "\n";
    // Verify: vec.data() should be inside [buffer, buffer + 4096)
}
```

```text
Vector size: 100
Buffer address: 0x7ffd12345678
Vector data address: 0x7ffd12345678
```

All elements of this vector come from that 4096-byte stack buffer, with zero global `new` calls. This is the typical usage of pmr + monotonic: feed a pre-allocated block of memory (stack, static area, or self-managed heap block) to a container to gain deterministic allocation behavior, zero fragmentation, and zero global heap overhead. Swap the resource (e.g., to a pool) to swap strategies without changing a single line of container code.

## Wrapping Up

The core of custom allocators is "managing the allocation/deallocation of a block of memory yourself." Three classic strategies—Bump (fast, no individual free), Pool (fixed-size high-frequency), and Stack (LIFO)—each have their use cases. Once you understand them, for use in STL, prioritize C++17's `std::pmr`: `memory_resource` abstraction + standard implementations (monotonic/pool) + pmr containers for runtime strategy switching and type explosion avoidance. Hand-written allocators are for understanding mechanisms or covering niche needs pmr doesn't; for常规 scenarios, pmr is sufficient. This concludes our container deep dive; next, we move to the standard library's iterator and algorithm system.

Want to run this directly and see the effect? Open the online example below (runnable, with assembly view):

<OnlineCompilerDemo
  title="Custom Allocators: Bump Arena & std::pmr"
  source-path="code/examples/vol3/13_custom_allocators.cpp"
  description="Hand-written linear allocator prototype, std::pmr::monotonic_buffer_resource makes vector allocate from stack buffer"
  allow-run
/>

## References

- [std::pmr (memory_resource) — cppreference](https://en.cppreference.com/w/cpp/memory/resource)
- [monotonic_buffer_resource — cppreference](https://en.cppreference.com/w/cpp/memory/monotonic_buffer_resource)
- [polymorphic_allocator — cppreference](https://en.cppreference.com/w/cpp/memory/polymorphic_allocator)
