---
chapter: 1
cpp_standard:
- 17
- 20
description: Implement a teaching version of Chrome's WeakPtr and understand the ref-counted
  control block and the sequence-bound model
difficulty: advanced
order: 4
platform: host
prerequisites:
- 'SimpleWeakPtr: A Safety Improvement over T* + shared_ptr<Flag>'
reading_time_minutes: 9
related:
- 'std::weak_ptr Compared: Async Callbacks in Practice'
tags:
- host
- cpp-modern
- advanced
- 智能指针
- 引用计数
- 回调机制
title: 'Chrome-like WeakPtr: Reference-Counted Control Block and WeakPtrFactory'
translation:
  source: documents/vol8-domains/cpp-deep-dives/pointer-semantics/04-chrome-weakptr.md
  source_hash: 176284eae4fa309c923eb3d115d38f7eb2be4c8406a4be2860d081171ffa2ab4
  translated_at: '2026-09-27T02:44:49+00:00'
  engine: anthropic
  token_count: 4100
---
# Chrome-like WeakPtr: Reference-Counted Control Block and WeakPtrFactory

In the previous article we solved the control block lifetime problem with `shared_ptr<Flag>`. It does work, but it drags in `shared_ptr`'s own overhead — a heap allocation, two atomic reference counts (strong count + weak count), and the memory occupied by the control block object itself.

For a small struct that stores nothing more than a `bool alive`, that is a lot of overhead.

The Chromium project ran into this problem early. Chrome's codebase is full of async callbacks, timers, and message loops — they all need WeakPtr, but they neither need to nor should manage every object with `shared_ptr`. So Chrome designed its own WeakPtr mechanism. The core idea: **manage the invalidation state with a reference-counted control block, but make that control block much simpler than the one `shared_ptr` uses.**

In this article we implement a teaching version of the Chrome-like WeakPtr and understand why it is lighter than `shared_ptr<Flag>` and safer than a `raw Flag*`.

## Core Design

Chrome's WeakPtr design has a few key characteristics:

**First, the control block is reference-counted, but without `shared_ptr`.** Chrome manages the reference count itself and maintains just one simple counter — no weak count, no custom deleters, no allocator support. That means the control block can be smaller and faster.

**Second, the Factory pattern.** The only way to create a WeakPtr is through a `WeakPtrFactory<T>`. The factory holds the control block and is responsible for invalidating all WeakPtrs when the owner is destroyed. This centralized management avoids the confusion of "who gets to invalidate".

**Third, it is sequence-bound.** Chrome's WeakPtr is by design not cross-thread safe — it assumes that all code using the same WeakPtr runs on the same sequence (a logical thread). This is a fundamental difference from `std::weak_ptr`'s cross-thread design.

Now let's implement the teaching version.

## Implementation

### WeakFlag — the Reference-Counted Control Block

```cpp
// weak_flag.h
// Teaching version of a reference-counted control block

#pragma once

#include <atomic>

class WeakFlag {
public:
    WeakFlag() = default;

    // Copying and moving are forbidden — the control block is not copyable
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
    std::atomic<int> ref_count_{1};  // The factory initially holds one reference
    // Note: no virtual destructor, no custom deleter wired into delete
    // The design goal of this control block is to be lighter than shared_ptr's control block

    ~WeakFlag() = default;
};
```

Compared with `shared_ptr`'s control block, `WeakFlag` has only two atomic variables: `is_valid_` and `ref_count_`. No strong/weak double counting, no virtual destructor, no allocator. A `WeakFlag` object is only 8 bytes (1 byte for the `atomic<bool>` + 3 bytes of alignment padding + 4 bytes for the `atomic<int>`).

### WeakPtr\<T\>

```cpp
// weak_ptr.h
// Teaching version of a Chrome-like WeakPtr<T>

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

    // Copy constructor: bump the reference count
    WeakPtr(const WeakPtr& other) : ptr_(other.ptr_), flag_(other.flag_)
    {
        if (flag_) {
            flag_->add_ref();
        }
    }

    // Move constructor: transfer the reference
    WeakPtr(WeakPtr&& other) noexcept
        : ptr_(other.ptr_), flag_(other.flag_)
    {
        other.ptr_ = nullptr;
        other.flag_ = nullptr;
    }

    // Assignment
    WeakPtr& operator=(const WeakPtr& other)
    {
        if (this != &other) {
            // Release the old one first
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

    // Destructor: drop the reference count
    ~WeakPtr()
    {
        if (flag_) {
            flag_->release();
        }
    }

    // Check whether it is still valid
    bool is_valid() const { return flag_ && flag_->is_valid(); }

    // Get the pointer
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
// Teaching version of WeakPtrFactory<T>

#pragma once

#include "weak_flag.h"
#include "weak_ptr.h"

template <typename T>
class WeakPtrFactory {
public:
    explicit WeakPtrFactory(T* owner) : owner_(owner)
    {
        // The factory allocates the control block on creation
        flag_ = new WeakFlag();
    }

    // Copying and moving are forbidden — the factory is bound to its owner
    WeakPtrFactory(const WeakPtrFactory&) = delete;
    WeakPtrFactory& operator=(const WeakPtrFactory&) = delete;

    // Create a new WeakPtr
    WeakPtr<T> get_weak_ptr()
    {
        return WeakPtr<T>(owner_, flag_);
    }

    // Invalidate every WeakPtr that has been handed out
    void invalidate_weak_ptrs()
    {
        if (flag_) {
            flag_->invalidate();
        }
    }

    // Automatically invalidate when the factory is destroyed
    ~WeakPtrFactory()
    {
        invalidate_weak_ptrs();
        // The factory releases the reference it holds
        // If any WeakPtr is still alive, flag_ is not deleted
        // flag_ is deleted only when the last WeakPtr is destroyed
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

## Why the Control Block Is Reference-Counted

As with Part 3's `shared_ptr<Flag>`, the point of reference counting is to guarantee that the control block outlives every WeakPtr. But Chrome's implementation is lighter than `shared_ptr`, because:

**There is only one counter.** `shared_ptr` internally keeps two atomic variables, the strong count and the weak count. `WeakFlag` has a single `ref_count_` — because there is no notion of "shared ownership" here, only a count of "who still holds this control block".

**No extra heap-side bookkeeping for the control block.** `shared_ptr`'s control block is usually allocated with `new` (unless you use `make_shared`), and it has to maintain a virtual destructor table, allocator information, and so on. `WeakFlag` is a plain `new` + `delete`, with no extra overhead.

**Invalidation is more direct.** Invalidating a `shared_ptr<Flag>` means mutating a member variable of the Flag, whereas `WeakFlag::invalidate()` writes an atomic variable directly — one atomic store.

## Why It Is Safer Than a raw Flag*

We already answered this question in the previous article, but it is worth repeating with `WeakFlag`:

The problem with a `raw Flag*` is that the Flag's lifetime is bound to the factory/owner. The factory is destroyed → the Flag is destroyed → the `flag_` held by outside WeakPtrs dangles → `is_valid()` is UB.

A `WeakFlag*` plus reference counting solves this. When the factory is destroyed, it calls `flag_->release()` to drop the reference count by 1, but as long as any WeakPtr is still alive the count stays > 0 and the `WeakFlag` object is not `delete`d. Whatever `is_valid()` accesses is guaranteed to be a still-living `WeakFlag` object.

## Why It Suits Some Scenarios Better Than std::weak_ptr

`std::weak_ptr<T>` depends on `std::shared_ptr<T>`'s control block. If you want to use `std::weak_ptr<T>`, you must first manage the object with `std::shared_ptr<T>`. But in many scenarios objects are not managed by `shared_ptr` — they may be stack objects, heap objects owned by `unique_ptr`, or members of some framework's object pool. Forcing every object into `shared_ptr` management just so you can use `weak_ptr` is a common form of over-engineering.

The Chrome-like WeakPtr does not require the object to be managed by `shared_ptr`. It only requires the object to have a `WeakPtrFactory<T>` member inside it — the object itself can follow any ownership model. That makes it an excellent fit for UI frameworks, game engines, and networking libraries: places where "object lifetimes are managed by the framework, not by `shared_ptr`".

## The Sequence-Bound Model: Why It Is Not Cross-Thread Safe

Chrome's WeakPtr assumes by design that all of its users run on the same sequence. A sequence is a logical order of execution — it can be a single thread, or multiple threads with a message loop (each thread having its own task runner).

Under this assumption, there is no TOCTOU race between `is_valid()` and `get()` — invalidate and get can never execute at the same time (they are queued and executed on the same sequence).

But use it across sequences — say, invalidate on one sequence and get on another — and the race described in Part 3 can appear. The `atomic<bool>` guarantees that `is_valid()` itself is not UB, but there can still be a race between "read valid=true, then access T" and "T's destruction".

So the correct way to use the Chrome-like WeakPtr is: **create, use, and invalidate it on the same sequence.** For cross-sequence scenarios, use `std::weak_ptr` or additional synchronization mechanisms.

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
    // The factory is the last member — ensure invalidation happens before the other members are destroyed
    WeakPtrFactory<Session> factory_{this};
};

int main()
{
    WeakPtr<Session> weak = [] {
        auto s = std::make_unique<Session>(42);
        auto w = s->get_weak_ptr();
        std::cout << "Before destroy: valid = " << w.is_valid() << "\n";
        return w;
        // The Session is destroyed here
        // factory_ destroyed → invalidate → release (ref_count: 2→1)
        // The WeakFlag is still alive (held by weak)
    }();

    // The Session has been destroyed
    std::cout << "After destroy: valid = " << weak.is_valid() << "\n";
    std::cout << "get() returns: "
              << (weak.get() ? "non-null" : "nullptr") << "\n";

    // weak destroyed → release (ref_count: 1→0) → delete WeakFlag
}
```

Output:

```text
Before destroy: valid = true
After destroy: valid = false
get() returns: nullptr
```

Compare this with Part 2's `UnsafeWeakPtr` — in the very same scenario, `UnsafeWeakPtr` is UB, while the Chrome-like WeakPtr safely returns `false`.

## Summary

- The Chrome-like WeakPtr replaces `shared_ptr<Flag>` with a custom reference-counted control block (`WeakFlag`), which is lighter
- `WeakPtrFactory<T>` centrally manages the creation and invalidation of the control block, avoiding confusion
- Reference counting guarantees that the control block outlives every WeakPtr — `is_valid()` is always safe
- Objects are not required to be managed by `shared_ptr` — a good fit for framework-internal object lifetime patterns
- It is bound to a single sequence by design and is not suitable for arbitrary cross-thread use
- The `atomic<bool>` solves the data race on the flag, but not the concurrent-access safety of `T`

## References

- [Chromium Smart Pointer Guidelines](https://www.chromium.org/developers/smart-pointer-guidelines/)
- [Chromium source: base/memory/weak_ptr.h](https://source.chromium.org/chromium/chromium/src/+/main:base/memory/weak_ptr.h)
- [C++ Core Guidelines - CP.50: Define a mutex together with the data it guards](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
