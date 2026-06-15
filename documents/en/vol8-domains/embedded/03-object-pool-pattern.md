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
  engine: anthropic
  source: documents/vol8-domains/embedded/03-object-pool-pattern.md
  source_hash: 5ba90bc727848fabfbb3137a5b4c15371df735a928e8a72bddf7c2f81e87792f
  token_count: 1211
  translated_at: '2026-05-26T12:13:24.526475+00:00'
---
# Embedded C++ Tutorial: Object Pool Pattern

## Introduction

Memory allocation is a common occurrence, and it is a topic we cannot avoid discussing. Any object whose lifetime we need to manage manually rather than automatically (whether you call it a struct or a variable) requires heap memory allocation. Although there might not be a strict division on an MCU (Microcontroller Unit), we definitely need some persistently allocated objects.

In desktop applications, we typically use `new`/`delete` (which wrap `malloc`/`free` under the hood) for memory allocation. However, on a typical MCU, `new`/`delete` can easily lead to memory fragmentation, along with non-deterministic latency and an unacceptable risk of failure on some platforms.

These real-time characteristics make it difficult for us to freely and frequently use `new`/`delete` or `malloc`/`free` the way we do in desktop applications.

Here, the Object Pool pattern serves as a common and practical solution: we allocate a group of objects (or memory blocks) upfront, borrow objects from the pool at runtime, and return them when we are done. This achieves deterministic memory usage and low-latency allocation/deallocation.

------

## When to Use an Object Pool

An object pool can be viewed as an aggregation of a set of objects. Because embedded scenarios are fixed, our object sizes and quantities are generally predictable (or have an upper bound). Furthermore, object allocation is frequent and requires deterministic latency (such as for network packet buffers, task objects, or driver contexts). The system cannot tolerate runtime memory fragmentation (for long-running devices, unattended systems).

For more complex scenarios, such as when object sizes and maximum concurrency cannot be estimated in advance, or when elastic scaling is required, an object pool might not be appropriate.

## API Design

```cpp
// 高层语义
template<typename T, size_t N, typename SyncPolicy>
class ObjectPool;

// 使用方式（伪代码）
static ObjectPool<MyObj, 16, NoLockPolicy> pool;
auto ptr = pool.try_acquire(); // 返回 nullptr 表示耗尽
ptr->init(...);
// 使用
pool.release(ptr);

```

We provide a combination of `acquire` (blocking or asserting on exhaustion) and `try_acquire` (non-blocking, returning `nullptr`).

------

## Core Implementation

Let's first look at a possible implementation —

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>

// 简单断言（可替换为项目断言）
#ifndef EP_ASSERT
#include <cassert>
#define EP_ASSERT(x) assert(x)
#endif

// ========== 同步策略接口 ==========
// 这些策略为空壳或实现平台相关的保护操作

struct NoLockPolicy {
    static void lock() {}
    static void unlock() {}
};

// 关中断保护（伪代码，需由平台实现）
struct InterruptLockPolicy {
    static inline unsigned primask_save() { unsigned p = 0; /* read PRIMASK */ return p; }
    static inline void primask_restore(unsigned p) { /* write PRIMASK */ }
    unsigned state;
    InterruptLockPolicy() : state(primask_save()) {}
    ~InterruptLockPolicy() { primask_restore(state); }
};

// 基于 mutex 的保护（RTOS）
struct MutexLockPolicy {
    static void lock();   // 在平台文件中实现
    static void unlock();
};

// ========== 对象池实现 ==========

template<typename T, size_t N, typename Sync = NoLockPolicy>
class ObjectPool {
public:
    static_assert(N > 0, "Pool size must be > 0");
    static_assert(std::is_default_constructible<T>::value || std::is_trivially_default_constructible<T>::value,
                  "T must be default constructible or trivially default constructible for placement new usage");

    ObjectPool() {
        for (size_t i = 0; i < N; ++i) {
            next_idx_[i] = (i + 1 < N) ? i + 1 : kInvalidIndex;
        }
        free_head_ = 0;
    }

    // 非阻塞借出，耗尽返回 nullptr
    T* try_acquire() {
        Sync::lock();
        if (free_head_ == kInvalidIndex) {
            Sync::unlock();
            return nullptr;
        }
        size_t idx = free_head_;
        free_head_ = next_idx_[idx];
        used_count_++;
        Sync::unlock();

        T* obj = reinterpret_cast<T*>(&storage_[idx]);
        // placement-new 初始化
        new (obj) T();
        return obj;
    }

    // 归还对象（必须来自本池）
    void release(T* obj) {
        EP_ASSERT(obj != nullptr);
        size_t idx = ptr_to_index(obj);
        EP_ASSERT(idx < N);

        // 调用析构
        obj->~T();

        Sync::lock();
        next_idx_[idx] = free_head_;
        free_head_ = idx;
        used_count_--;
        Sync::unlock();
    }

    // 获取当前空闲/已用数量
    size_t free_count() const {
        return N - used_count_;
    }
    size_t used_count() const { return used_count_; }

private:
    static constexpr size_t kInvalidIndex = static_cast<size_t>(-1);
    // 未初始化的原始存储
    typename std::aligned_storage<sizeof(T), alignof(T)>::type storage_[N];
    size_t next_idx_[N];
    size_t free_head_ = kInvalidIndex;
    size_t used_count_ = 0;

    static size_t ptr_to_index(T* ptr) {
        uintptr_t base = reinterpret_cast<uintptr_t>(&storage_[0]);
        uintptr_t p = reinterpret_cast<uintptr_t>(ptr);
        EP_ASSERT(p >= base);
        size_t offset = (p - base) / sizeof(storage_[0]);
        return offset;
    }
};

```

> Note: The interrupt read/write operations in `CriticalSection` are platform-dependent and need to be replaced with the target MCU's implementation (such as reading/writing PRIMASK on ARM Cortex-M). If using FreeRTOS, map the `CriticalSection`'s `enter`/`exit` implementation to `taskENTER_CRITICAL`/`taskEXIT_CRITICAL` or `portENTER_CRITICAL`/`portEXIT_CRITICAL`.

How do we use it?

```cpp
// 假设我们有一个包缓冲对象
struct Packet {
    uint8_t buf[256];
    size_t len;
    void init() { len = 0; }
};

// 在全局或模块静态区分配池
static ObjectPool<Packet, 8, NoLockPolicy> pktPool;

void on_receive() {
    Packet* p = pktPool.try_acquire();
    if (!p) {
        // 资源耗尽：丢包或记录错误
        return;
    }
    p->init();
    // 填充 p->buf, p->len ...

    // 使用完毕
    pktPool.release(p);
}

```

For allocation in an interrupt context, if we are allocating/freeing inside an ISR, we must use `try_acquire` or implement a lock-free algorithm. We should avoid performing complex initialization in the ISR, and instead try to only borrow the object and defer the processing to the task context.

------

## Quick Recap

The object pool is an extremely practical tool in embedded development: it reduces the unpredictability of runtime memory management to a controllable range while providing efficient allocation/deallocation paths. When implementing one, we need to weigh thread safety, ISR scenarios, object construction costs, and diagnostic capabilities.

------

## Code Example
