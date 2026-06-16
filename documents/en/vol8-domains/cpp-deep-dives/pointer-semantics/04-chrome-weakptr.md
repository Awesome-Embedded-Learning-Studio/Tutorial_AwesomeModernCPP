---
chapter: 1
cpp_standard:
- 17
- 20
description: Implement a teaching version of Chrome's `WeakPtr` to understand ref-counted
  control blocks and the structured binding model.
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
title: 'Chrome-like WeakPtr: Reference Counting Control Block and WeakPtrFactory'
translation:
  source: documents/vol8-domains/cpp-deep-dives/pointer-semantics/04-chrome-weakptr.md
  source_hash: 1f51ec2db262a9adc85b5251f8b957eec8ebe324bea37328f461e674571f54a4
  translated_at: '2026-06-16T04:08:43.220631+00:00'
  engine: anthropic
  token_count: 2297
---
# Chrome-like WeakPtr: Reference Counted Control Block and WeakPtrFactory

## Introduction

In the previous post, we used `std::shared_ptr` to solve the lifetime safety issues of the control block. It certainly works, but it brings the overhead of `std::shared_ptr` itself—heap allocation, two atomic reference counts (strong count + weak count), and the memory footprint of the control block object.

For a small structure that just holds a `bool`, these overheads are a bit heavy.

The Chromium project encountered this problem early on. Chrome's codebase is full of asynchronous callbacks, timers, and message loops—they need `WeakPtr`, but they don't need to, and shouldn't, use `std::shared_ptr` to manage all objects. So, Chrome designed its own `WeakPtr` mechanism. The core idea is: **use a reference-counted control block to manage the invalidation state, but this control block is much simpler than `std::shared_ptr`'s.**

In this post, we will implement a teaching version of the Chrome-like `WeakPtr` to understand why it is lighter than `std::weak_ptr` and safer than a raw flag pointer.

## Core Design Philosophy

Chrome's `WeakPtr` design has several key characteristics:

**First, the control block is reference-counted but doesn't use `std::shared_ptr`.** Chrome manages the reference counts itself, maintaining only a simple counter—no weak count, no custom deleters, and no allocator support. This means the control block can be smaller and faster.

**Second, the Factory pattern.** The only way to create a `WeakPtr` is through a `WeakPtrFactory`. The Factory holds the control block and is responsible for invalidating all `WeakPtr`s when the Owner is destructed. This centralized management avoids the confusion of "who invalidates what."

**Third, Sequence-bound.** Chrome's `WeakPtr` is not designed to be thread-safe by default—it assumes all code using the same `WeakPtr` runs on the same sequence (logical thread). This is fundamentally different from `std::weak_ptr`'s cross-thread design.

Let's implement the teaching version now.

## Implementation

### WeakFlag — The Reference Counted Control Block

```cpp
struct WeakFlag {
    std::atomic<int> ref_count;  // How many WeakPtrs are holding this flag
    std::atomic<bool> is_valid;  // Is the Owner still alive?

    explicit WeakFlag() : ref_count(1), is_valid(true) {}

    void AddRef() { ref_count.fetch_add(1, std::memory_order_relaxed); }

    void Release() {
        if (ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete this;
        }
    }
};
```

Compared to `std::shared_ptr`'s control block, `WeakFlag` has only two atomic variables: `ref_count` and `is_valid`. There are no strong/weak dual counts, no virtual destructors, and no allocators. A `WeakFlag` object is only 8 bytes (`ref_count` 4 bytes + `is_valid` 1 byte + padding 3 bytes).

### WeakPtr\<T\>

```cpp
template <typename T>
class WeakPtr {
public:
    WeakPtr() : ptr_(nullptr), flag_(nullptr) {}

    void Reset() {
        if (flag_) {
            flag_->Release();
            flag_ = nullptr;
            ptr_ = nullptr;
        }
    }

    T* Get() const {
        // Acquire ensures we see the latest is_valid write
        return (flag_ && flag_->is_valid.load(std::memory_order_acquire)) ? ptr_ : nullptr;
    }

    // Copy constructor
    WeakPtr(const WeakPtr& other) : ptr_(other.ptr_), flag_(other.flag_) {
        if (flag_) {
            flag_->AddRef();
        }
    }

    // Move constructor
    WeakPtr(WeakPtr&& other) noexcept : ptr_(other.ptr_), flag_(other.flag_) {
        other.ptr_ = nullptr;
        other.flag_ = nullptr;
    }

    ~WeakPtr() {
        Reset();
    }

private:
    T* ptr_;
    WeakFlag* flag_;

    friend class WeakPtrFactory<T>;
    WeakPtr(T* p, WeakFlag* f) : ptr_(p), flag_(f) {}
};
```

### WeakPtrFactory\<T\>

```cpp
template <typename T>
class WeakPtrFactory {
public:
    explicit WeakPtrFactory(T* owner) : owner_(owner), flag_(new WeakFlag()) {}

    ~WeakPtrFactory() {
        // Invalidate all WeakPtrs
        flag_->is_valid.store(false, std::memory_order_release);
        flag_->Release(); // Release the reference held by the Factory
    }

    WeakPtr<T> GetPtr() {
        return WeakPtr<T>(owner_, flag_);
    }

    // Prevent copying and moving
    WeakPtrFactory(const WeakPtrFactory&) = delete;
    WeakPtrFactory& operator=(const WeakPtrFactory&) = delete;

private:
    T* owner_;
    WeakFlag* flag_;
};
```

## Why Reference Count the Control Block

Just like the `std::shared_ptr` approach in Part 3, the purpose of reference counting is to ensure the control block outlives all `WeakPtr`s. However, Chrome's implementation is lighter than `std::shared_ptr` because:

**There is only one counter.** `std::shared_ptr` internally has two atomic variables: `use_count` and `weak_count`. `WeakFlag` only has `ref_count`—because there is no concept of "shared ownership" here, only a count of "who is still holding this control block."

**No extra heap management overhead for the control block.** `std::shared_ptr`'s control block is usually allocated via `new` (unless using `make_shared`), and must maintain virtual destructor tables and allocator info. `WeakFlag` is just a simple `int` + `bool`, with no extra overhead.

**The invalidation mechanism is more direct.** `std::weak_ptr` invalidation requires modifying the `use_count` inside the control block, whereas `WeakFlag` directly modifies an atomic variable—a single atomic store.

## Why It Is Safer Than a Raw Flag*

We answered this question in the last post, but let's reiterate using `WeakFlag`:

The problem with `std::weak_ptr<Flag>` is that the Flag's lifetime is bound to the Factory/Owner. Factory destructs → Flag destructs → the raw `Flag*` held by external `WeakPtr` becomes dangling → `flag_->is_valid` access is UB.

`WeakFlag` + reference counting solves this. When the Factory destructs, it calls `Release()` to decrement the reference count. However, as long as a `WeakPtr` is still alive, the reference count remains > 0, so the `WeakFlag` object will not be `delete`d. `WeakPtr::Get` is guaranteed to access a living `WeakFlag` object.

## Why It Is More Suitable for Certain Scenarios Than std::weak_ptr

`std::weak_ptr` relies on `std::shared_ptr`'s control block. If you want to use `std::weak_ptr`, you must first use `std::shared_ptr` to manage the object. But in many scenarios, objects are not managed by `std::shared_ptr`—they might be stack objects, heap objects managed by `std::unique_ptr`, or belong to some framework's object pool. Forcing all objects to be managed by `std::shared_ptr` just to use `std::weak_ptr` is a common over-engineering practice.

Chrome-like `WeakPtr` does not require the object to be managed by `std::shared_ptr`. It only requires the object to have a `WeakPtrFactory` member—the object itself can be under any ownership model. This makes it very suitable for UI frameworks, game engines, and network libraries where "object lifecycles are managed by the framework, not by `shared_ptr`."

## Sequence-Bound Model: Why It Is Not Cross-Thread Safe

Chrome's `WeakPtr` is designed under the assumption that all users run on the same sequence. A sequence is a logical execution order—it can be single-threaded, or multi-threaded with a message loop (each thread has its own task runner).

Under this assumption, there will be no TOCTOU race between `Invalidate()` and `Get()`—because invalidate and get cannot execute simultaneously (they are queued sequentially on the same sequence).

But if used across sequences—for example, invalidating on one sequence and getting on another—the race condition mentioned in Part 3 might appear. `WeakFlag` guarantees that accessing `WeakFlag` itself won't UB, but a race can still occur between "read valid=true then access T" and "T's destruction."

Therefore, the correct way to use Chrome-like `WeakPtr` is: **create, use, and invalidate on the same sequence.** For cross-sequence scenarios, use `std::weak_ptr` or additional synchronization mechanisms.

## Usage Example

```cpp
class Controller {
public:
    Controller() : weak_factory_(this) {}

    void DoWork() {
        std::cout << "Controller working..." << std::endl;
    }

    WeakPtr<Controller> GetWeakPtr() {
        return weak_factory_.GetPtr();
    }

private:
    WeakPtrFactory<Controller> weak_factory_;
};

void AsyncTask(WeakPtr<Controller> weak_ctrl) {
    // Simulate async delay
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    if (auto* ctrl = weak_ctrl.Get()) {
        ctrl->DoWork();
    } else {
        std::cout << "Controller destroyed, skipping work." << std::endl;
    }
}

int main() {
    auto ctrl = std::make_unique<Controller>();
    auto weak = ctrl->GetWeakPtr();

    std::thread t(AsyncTask, weak);

    // Destroy the controller before the task finishes
    ctrl.reset();

    t.join();
    return 0;
}
```

Output:

```text
Controller destroyed, skipping work.
```

Comparing this with the `std::weak_ptr<Flag>` example from Part 2—in the same scenario, the raw pointer version would UB, while the Chrome-like `WeakPtr` safely returns `nullptr`.

## Summary

- Chrome-like `WeakPtr` uses a custom reference-counted control block (`WeakFlag`) instead of `std::shared_ptr`, making it lighter.
- `WeakPtrFactory` centralizes the creation and invalidation of the control block, avoiding confusion.
- Reference counting ensures the control block outlives all `WeakPtr`s—`WeakPtr::Get` is always safe.
- It does not require objects to be managed by `std::shared_ptr`—suitable for framework-internal object lifecycle patterns.
- Designed to be bound to a single sequence, not suitable for arbitrary cross-thread use.
- `std::atomic<bool>` solves the data race on the Flag, but does not solve the concurrent access safety of `T`.

## Reference Resources

- [Chromium Smart Pointer Guidelines](https://www.chromium.org/developers/smart-pointer-guidelines/)
- [Chromium Source: base/memory/weak_ptr.h](https://source.chromium.org/chromium/chromium/src/+/main:base/memory/weak_ptr.h)
- [C++ Core Guidelines - CP.50: Define a mutex together with the data it guards](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
