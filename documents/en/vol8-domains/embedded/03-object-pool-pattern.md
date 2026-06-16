---
chapter: 5
cpp_standard:
- 11
- 14
- 17
- 20
description: Object Pool Pattern Application
difficulty: intermediate
order: 3
platform: stm32f1
prerequisites:
- 'Chapter 3: 内存与对象管理'
reading_time_minutes: 5
tags:
- cpp-modern
- intermediate
- stm32f1
title: Object Pool Pattern
translation:
  source: documents/vol8-domains/embedded/03-object-pool-pattern.md
  source_hash: 9fe8b59860d7daffd1fa4ec3c59358d7a2527eb4871f73abf07ce9440de0f589
  translated_at: '2026-06-16T04:11:27.824010+00:00'
  engine: anthropic
  token_count: 1211
---
# Embedded C++ Tutorial: Object Pool Pattern

## Introduction

Memory allocation is an inevitable topic we cannot avoid. Any object whose lifetime we must manage manually—whether you call it a struct or a variable—requires heap allocation. Although the boundary on an MCU might not be strictly defined, we inevitably need some persistently allocated objects.

On host machines, we typically use `new`/`delete` (which wrap `malloc`/`free` underneath) for memory allocation. However, on general MCUs, `new`/`delete` can easily lead to memory fragmentation, non-deterministic latency, and unacceptable failure risks on certain platforms.

These real-time constraints make it difficult for us to freely and frequently use `new`/`delete` or `malloc`/`free` as we would on a host system.

Here, the **Object Pool** serves as a common and practical pattern: we pre-allocate a group of objects (or memory blocks), and at runtime, we borrow objects from the pool and return them when done. This achieves deterministic memory usage and low-latency allocation/reclamation.

------

## When to Use an Object Pool

We can view an object pool as an aggregate of a fixed number of objects. Since embedded scenarios are often fixed, we can usually estimate (or set an upper limit on) object size and quantity. Furthermore, object allocation is frequent and requires deterministic latency (e.g., network packet buffers, task objects, or driver contexts). The system cannot tolerate runtime memory fragmentation (for long-running devices or unattended systems).

For more complex scenarios—such as when object size and maximum concurrency cannot be estimated in advance, or when elastic scaling is required—an object pool may not be suitable.

## API Design

```cpp
class ObjectPool {
public:
    // Acquire an object (blocking or assert on exhaustion)
    T* acquire();

    // Acquire an object (non-blocking, returns nullptr if exhausted)
    T* try_acquire();

    // Return an object to the pool
    void release(T* obj);
};
```

We provide a combination of `acquire` (blocking or assert on exhaustion) and `try_acquire` (non-blocking, returns `nullptr`).

------

## Core Implementation

Let's look at a possible implementation:

```cpp
#include <array>
#include <atomic>
#include <cstdint>
#include <new>

template <typename T, std::size_t N>
class ObjectPool {
    // Use a union to avoid calling constructors for unused slots
    union Node {
        T object;
        Node* next;
    };

    std::array<Node, N> pool_;
    Node* free_list_;
    std::atomic_flag lock_ = ATOMIC_FLAG_INIT;

public:
    ObjectPool() {
        // Initialize the free list
        for (std::size_t i = 0; i < N; ++i) {
            pool_[i].next = (i == N - 1) ? nullptr : &pool_[i + 1];
        }
        free_list_ = &pool_[0];
    }

    // Non-blocking acquire
    T* try_acquire() {
        // Simple spinlock implementation
        while (lock_.test_and_set(std::memory_order_acquire)) {
            // Spin or yield
        }

        T* result = nullptr;
        if (free_list_ != nullptr) {
            Node* node = free_list_;
            free_list_ = free_list_->next;
            result = &node->object;
            // Use placement new to initialize the object
            new (result) T();
        }

        lock_.clear(std::memory_order_release);
        return result;
    }

    void release(T* obj) {
        if (obj == nullptr) return;

        // Call destructor explicitly
        obj->~T();

        while (lock_.test_and_set(std::memory_order_acquire)) {
            // Spin or yield
        }

        // Cast back to Node*
        Node* node = reinterpret_cast<Node*>(obj);
        node->next = free_list_;
        free_list_ = node;

        lock_.clear(std::memory_order_release);
    }
};
```

> **Note:** Interrupt enabling/disabling in `test_and_set`/`clear` is platform-dependent and needs to be replaced with the target MCU implementation (e.g., PRIMASK reads/writes on ARM Cortex-M). If using FreeRTOS, map the `std::atomic_flag` `lock_` implementation to `taskENTER_CRITICAL`/`taskEXIT_CRITICAL` or a mutex.

How do we use it?

```cpp
struct Packet {
    int id;
    float data[10];
};

// Create a pool for 10 Packet objects
ObjectPool<Packet, 10> packet_pool;

void driver_task() {
    // Borrow an object
    if (Packet* pkt = packet_pool.try_acquire()) {
        pkt->id = 1;
        pkt->data[0] = 3.14f;
        // ... use the object ...

        // Return it to the pool
        packet_pool.release(pkt);
    } else {
        // Handle pool exhaustion
    }
}
```

For allocation within an interrupt context, if allocating/releasing in an ISR, be sure to use `try_acquire` or implement a lock-free algorithm. Avoid performing complex initialization in the ISR; try to only borrow the object and defer processing to the task context.

------

## Quick Recap

The object pool is an extremely practical tool in embedded development: it reduces the unpredictability of runtime memory management to a controllable range while providing efficient allocation/reclamation paths. Implementation requires balancing thread safety, ISR scenarios, object construction costs, and diagnostic capabilities.

------

## Code Example
