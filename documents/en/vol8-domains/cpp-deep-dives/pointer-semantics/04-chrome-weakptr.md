---
chapter: 1
cpp_standard:
- 17
- 20
description: Implement an educational version of Chrome's WeakPtr, and understand
  the ref-counted control block and sequence binding model
difficulty: advanced
order: 4
platform: host
prerequisites:
- SimpleWeakPtr：T* + shared_ptr<Flag> 的安全改进
reading_time_minutes: 9
related:
- std::weak_ptr 对比与异步回调实战
tags:
- host
- cpp-modern
- advanced
- 智能指针
- 引用计数
- 回调机制
title: 'Chrome-like WeakPtr: Reference Count Control Block and WeakPtrFactory'
translation:
  engine: anthropic
  source: documents/vol8-domains/cpp-deep-dives/pointer-semantics/04-chrome-weakptr.md
  source_hash: c7810fbd0d1980848dcbc7a8559e8c564c7ff5f8a2359266da316380b557d05e
  token_count: 2303
  translated_at: '2026-05-26T11:55:12.912926+00:00'
---
# Chrome-like WeakPtr: Reference-Counted Control Block and WeakPtrFactory

## Introduction

In the previous article, we used `shared_ptr<Flag>` to solve the control block's lifetime safety issue. It certainly works, but it brings the overhead of `shared_ptr` itself—heap allocation, two atomic reference counts (strong count + weak count), and the memory footprint of the control block object.

For a small structure that merely holds a `bool alive`, this overhead is a bit heavy.

The Chromium project encountered this problem early on. Chrome's codebase is full of asynchronous callbacks, timers, and message loops—they need WeakPtr, but they don't need and shouldn't use `shared_ptr` to manage all objects. So Chrome designed its own WeakPtr mechanism. The core idea is: **use a reference-counted control block to manage the invalidation state, but this control block is much simpler than `shared_ptr`'s.**

In this article, we will implement an educational version of the Chrome-like WeakPtr to understand why it is lighter than `shared_ptr<Flag>` and safer than `raw Flag*`.

## Core Design Ideas

Chrome's WeakPtr design has a few key characteristics:

**First, the control block is reference-counted, but it does not use `shared_ptr`.** Chrome manages the reference count itself, maintaining only a simple counter—no weak count, no custom deleters, and no allocator support. This means the control block can be smaller and faster.

**Second, the Factory pattern.** The only way to create a WeakPtr is through a `WeakPtrFactory<T>`. The Factory holds the control block and is responsible for invalidating all WeakPtrs when the Owner is destructed. This centralized management avoids the confusion of "who should invalidate."

**Third, sequence-bound.** Chrome's WeakPtr is not designed to be thread-safe by default—it assumes that all code using the same WeakPtr runs on the same sequence (a logical thread). This is fundamentally different from `std::weak_ptr`'s cross-thread design.

Next, we will implement the educational version.

## Implementation

### WeakFlag — The Reference-Counted Control Block

```cpp
// weak_flag.h
// 教学版引用计数控制块

#pragma once

#include <atomic>

class WeakFlag {
public:
    WeakFlag() = default;

    // 禁止拷贝和移动——控制块是不可复制的
    WeakFlag(const WeakFlag&) = delete;
    WeakFlag& operator=(const WeakFlag&) = delete;

    void add_ref() { ref_count_.fetch_add(1, std::memory_order_relaxed); }

    void release()
    {
        if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete this;
        }
    }

    void invalidate()
    {
        is_valid_.store(false, std::memory_order_release);
    }

    bool is_valid() const
    {
        return is_valid_.load(std::memory_order_acquire);
    }

private:
    std::atomic<bool> is_valid_{true};
    std::atomic<int> ref_count_{1};  // Factory 初始持有一份引用
    // 注意：不使用虚析构函数，不使用 delete 的自定义删除器
    // 这个控制块的设计目标是比 shared_ptr 的控制块更轻量

    ~WeakFlag() = default;
};
```

Compared to `shared_ptr`'s control block, `WeakFlag` has only two atomic variables: `is_valid_` and `ref_count_`. There is no strong/weak dual counting, no virtual destructor, and no allocator. A `WeakFlag` object is only 8 bytes (`atomic<bool>` 1 byte + alignment padding 3 bytes + `atomic<int>` 4 bytes).

### WeakPtr\<T\>

```cpp
// weak_ptr.h
// 教学版 Chrome-like WeakPtr<T>

#pragma once

#include "weak_flag.h"

template <typename T>
class WeakPtr {
public:
    WeakPtr() : ptr_(nullptr), flag_(nullptr) {}

    WeakPtr(T* ptr, WeakFlag* flag) : ptr_(ptr), flag_(flag)
    {
        if (flag_) {
            flag_->add_ref();
        }
    }

    // 拷贝构造：增加引用计数
    WeakPtr(const WeakPtr& other) : ptr_(other.ptr_), flag_(other.flag_)
    {
        if (flag_) {
            flag_->add_ref();
        }
    }

    // 移动构造：转移引用
    WeakPtr(WeakPtr&& other) noexcept
        : ptr_(other.ptr_), flag_(other.flag_)
    {
        other.ptr_ = nullptr;
        other.flag_ = nullptr;
    }

    // 赋值
    WeakPtr& operator=(const WeakPtr& other)
    {
        if (this != &other) {
            // 先释放旧的
            if (flag_) {
                flag_->release();
            }
            ptr_ = other.ptr_;
            flag_ = other.flag_;
            if (flag_) {
                flag_->add_ref();
            }
        }
        return *this;
    }

    WeakPtr& operator=(WeakPtr&& other) noexcept
    {
        if (this != &other) {
            if (flag_) {
                flag_->release();
            }
            ptr_ = other.ptr_;
            flag_ = other.flag_;
            other.ptr_ = nullptr;
            other.flag_ = nullptr;
        }
        return *this;
    }

    // 析构：减少引用计数
    ~WeakPtr()
    {
        if (flag_) {
            flag_->release();
        }
    }

    // 检查是否有效
    bool is_valid() const { return flag_ && flag_->is_valid(); }

    // 获取指针
    T* get() const
    {
        if (is_valid()) {
            return ptr_;
        }
        return nullptr;
    }

    T& operator*() const { return *get(); }
    T* operator->() const { return get(); }
    explicit operator bool() const { return get() != nullptr; }

private:
    T* ptr_;
    WeakFlag* flag_;
};
```

### WeakPtrFactory\<T\>

```cpp
// weak_ptr_factory.h
// 教学版 WeakPtrFactory<T>

#pragma once

#include "weak_flag.h"
#include "weak_ptr.h"

template <typename T>
class WeakPtrFactory {
public:
    explicit WeakPtrFactory(T* owner) : owner_(owner)
    {
        // Factory 创建时分配 control block
        flag_ = new WeakFlag();
    }

    // 禁止拷贝和移动——Factory 和 Owner 绑定
    WeakPtrFactory(const WeakPtrFactory&) = delete;
    WeakPtrFactory& operator=(const WeakPtrFactory&) = delete;

    // 创建一个新的 WeakPtr
    WeakPtr<T> get_weak_ptr()
    {
        return WeakPtr<T>(owner_, flag_);
    }

    // 使所有已发出的 WeakPtr 失效
    void invalidate_weak_ptrs()
    {
        if (flag_) {
            flag_->invalidate();
        }
    }

    // Factory 析构时自动 invalidate
    ~WeakPtrFactory()
    {
        invalidate_weak_ptrs();
        // Factory 释放自己持有的引用
        // 如果还有 WeakPtr 活着，flag_ 不会被 delete
        // 最后一个 WeakPtr 析构时才会 delete flag_
        if (flag_) {
            flag_->release();
        }
        flag_ = nullptr;
    }

private:
    T* owner_;
    WeakFlag* flag_;
};
```

## Why the Control Block Needs Reference Counting

Just like the `shared_ptr<Flag>` in the third article, the purpose of reference counting is to ensure the control block outlives all WeakPtrs. But Chrome's implementation is lighter than `shared_ptr` because:

**There is only one counter.** `shared_ptr` internally has two atomic variables: strong count and weak count. `WeakFlag` has only one `ref_count_`—because there is no concept of "shared ownership" here, only a count of "who still holds this control block."

**No extra heap management overhead for the control block.** `shared_ptr`'s control block is usually allocated via `new` (unless using `make_shared`), and it must maintain a virtual destructor table, allocator information, and so on. `WeakFlag` is simply `new` + `delete`, with no extra overhead.

**A more direct invalidation mechanism.** `shared_ptr<Flag>`'s invalidation requires modifying a Flag's member variable, whereas `WeakFlag::invalidate()` directly modifies an atomic variable—a single atomic store.

## Why It Is Safer Than a Raw Flag*

We already answered this question in the previous article, but let's reiterate it using `WeakFlag`:

The problem with `raw Flag*` is that the Flag's lifetime is bound to the Factory/Owner. Factory destruction → Flag destruction → the `flag_` held by external WeakPtrs becomes dangling → `is_valid()` is UB.

`WeakFlag*` + reference counting solves this problem. When the Factory is destructed, it calls `flag_->release()` to decrement the reference count by one. But as long as there are still WeakPtrs alive, the reference count remains > 0, and the `WeakFlag` object will not be `delete`. What `is_valid()` accesses is guaranteed to be a living `WeakFlag` object.

## Why It Is More Suitable Than std::weak_ptr in Certain Scenarios

`std::weak_ptr<T>` relies on `std::shared_ptr<T>`'s control block. If you want to use `std::weak_ptr<T>`, you must first use `std::shared_ptr<T>` to manage the object. However, in many scenarios, objects are not managed by `shared_ptr`—they might be stack objects, heap objects managed by `unique_ptr`, or part of some framework's object pool. Forcing all objects to be managed by `shared_ptr` just to use `weak_ptr` is a common form of over-engineering.

The Chrome-like WeakPtr does not require the object to be managed by `shared_ptr`. It only requires the object to have a `WeakPtrFactory<T>` member internally—the object itself can follow any ownership model. This makes it highly suitable for UI frameworks, game engines, and network libraries where "object lifetimes are managed by the framework, not by shared_ptr."

## The Sequence-Bound Model: Why It Is Not Thread-Safe

Chrome's WeakPtr is designed under the assumption that all users run on the same sequence. A sequence is a logical execution order—it can be single-threaded, or multi-threaded with message loops (where each thread has its own task runner).

Under this assumption, there will be no TOCTOU race condition between `is_valid()` and `get()`—because invalidate and get cannot execute simultaneously (they are queued for execution on the same sequence).

But if used across sequences—for example, invalidating on one sequence and calling get on another—the race condition mentioned in the third article can occur. `atomic<bool>` guarantees that accessing the `is_valid()` itself won't cause UB, but a race can still exist between "reading valid=true and then accessing T" and "T's destruction."

Therefore, the correct way to use the Chrome-like WeakPtr is: **create, use, and invalidate on the same sequence.** For cross-sequence scenarios, we should use `std::weak_ptr` or additional synchronization mechanisms.

## Usage Example

```cpp
#include <iostream>
#include <memory>
#include "weak_ptr_factory.h"

class Session {
public:
    Session(int id) : id_(id) {}

    WeakPtr<Session> get_weak_ptr()
    {
        return factory_.get_weak_ptr();
    }

    void do_work()
    {
        std::cout << "Session " << id_ << " working\n";
    }

    int id() const { return id_; }

private:
    int id_;
    // Factory 作为最后一个成员变量——确保在其他成员析构之前 invalidate
    WeakPtrFactory<Session> factory_{this};
};

int main()
{
    WeakPtr<Session> weak = [] {
        auto s = std::make_unique<Session>(42);
        auto w = s->get_weak_ptr();
        std::cout << "Before destroy: valid = " << w.is_valid() << "\n";
        return w;
        // Session 在这里析构
        // factory_ 析构 → invalidate → release (ref_count: 2→1)
        // WeakFlag 仍然活着（weak 持有）
    }();

    // Session 已经销毁
    std::cout << "After destroy: valid = " << weak.is_valid() << "\n";
    std::cout << "get() returns: "
              << (weak.get() ? "non-null" : "nullptr") << "\n";

    // weak 析构 → release (ref_count: 1→0) → delete WeakFlag
}
```

Output:

```text
Before destroy: valid = true
After destroy: valid = false
get() returns: nullptr
```

Compared to the `UnsafeWeakPtr` in the second article—in the same scenario, `UnsafeWeakPtr` would result in UB, whereas the Chrome-like WeakPtr safely returns `false`.

## Summary

- The Chrome-like WeakPtr replaces `shared_ptr<Flag>` with a custom reference-counted control block (`WeakFlag`), making it lighter
- `WeakPtrFactory<T>` centrally manages the creation and invalidation of the control block, avoiding confusion
- Reference counting ensures the control block outlives all WeakPtrs—`is_valid()` is always safe
- It does not require objects to be managed by `shared_ptr`—making it suitable for a framework's internal object lifetime patterns
- It is designed to be bound to a single sequence, making it unsuitable for arbitrary cross-thread use
- `atomic<bool>` solves data races on the Flag, but does not solve concurrent access safety for T

## References

- [Chromium Smart Pointer Guidelines](https://www.chromium.org/developers/smart-pointer-guidelines/)
- [Chromium Source: base/memory/weak_ptr.h](https://source.chromium.org/chromium/chromium/src/+/main:base/memory/weak_ptr.h)
- [C++ Core Guidelines - CP.50: Define a mutex together with the data it guards](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
