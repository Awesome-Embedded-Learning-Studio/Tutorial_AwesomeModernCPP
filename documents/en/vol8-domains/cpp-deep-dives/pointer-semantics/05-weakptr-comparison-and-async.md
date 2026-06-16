---
chapter: 1
cpp_standard:
- 17
- 20
description: 'Comparison between `std::weak_ptr` and Chrome WeakPtr: Security analysis
  of six asynchronous callback capture modes'
difficulty: advanced
order: 5
platform: host
prerequisites:
- Chrome-like WeakPtr：引用计数控制块与 WeakPtrFactory
- 卷二 · 第一章：weak_ptr 与循环引用
reading_time_minutes: 7
related:
- 跨线程安全、性能取舍与设计原则总结
tags:
- host
- cpp-modern
- advanced
- 智能指针
- 异步编程
- 回调机制
title: '`std::weak_ptr` Comparison and Asynchronous Callback Practice'
translation:
  source: documents/vol8-domains/cpp-deep-dives/pointer-semantics/05-weakptr-comparison-and-async.md
  source_hash: c0377cb04e5d3a14032743c75fcd1bc3a744dbaab75f7af56d983f7815b61c22
  translated_at: '2026-06-16T04:08:29.468353+00:00'
  engine: anthropic
  token_count: 1612
---
# std::weak_ptr Comparison and Async Callback Practice

## Introduction

In the previous four articles, we hand-rolled a non-owning pointer type from scratch—from Borrowed to ObserverPtr to various WeakPtr implementations. Now it is time to bring everything together for a comparison.

In this article, we will do two things: first, we will put `std::weak_ptr` and Chrome-like `WeakPtr` together to clarify their core differences; second, we will use six asynchronous callback capture modes for a practical comparison, so you can intuitively feel the difference between "incorrect capture" and "correct capture."

## Core Differences Between std::weak_ptr and Chrome WeakPtr

First, a frequently overlooked fact: **`std::weak_ptr` and Chrome-like `WeakPtr` do not solve the same problem.**

`std::weak_ptr` solves the problem of "weak references in a shared ownership model." It relies on the control block of `std::shared_ptr`. After successfully calling `lock()`, it obtains a `std::shared_ptr`, thereby **temporarily extending the object's lifetime**. This means that as long as your `lock()` succeeds, the object will definitely not be destructed while your `shared_ptr` is alive.

Chrome-like `WeakPtr` solves the problem of "weak references on objects not managed by `shared_ptr`." It does not depend on `shared_ptr`. Calling `get()` does not extend the object's lifetime—it simply returns a pointer. The object may be destructed at any time, and the pointer you obtained may be invalid before you use it. It only guarantees that you can **safely detect invalidation**, not that the object is still alive after you get the pointer.

These are two completely different lifetime strategies:

| Feature | Chrome-like WeakPtr\<T\> | std::weak_ptr\<T\> |
|---------|-------------------------|-------------------|
| Depends on shared_ptr | No | Yes |
| Extends lifetime when acquiring reference | **No** | **Yes** (lock returns shared_ptr) |
| Safe null check after object destruction | Yes | Yes |
| Suitable for non-shared_ptr managed objects | **Yes** | No |
| Naturally thread-safe | No (sequence-bound) | Partial (lock() is atomic, but access to T needs synchronization) |
| Control block overhead | Small (custom ref count) | Larger (shared_ptr control block) |

**When to use `std::weak_ptr`?** When the object is already managed by `std::shared_ptr`, you need to observe it safely in asynchronous scenarios, and you might need to temporarily extend its lifetime.

**When to use Chrome-like WeakPtr?** When the object is not managed by `std::shared_ptr` (stack objects, `unique_ptr`, framework-managed objects), and you need to safely detect invalidation in asynchronous callbacks.

**When should you NOT use `std::weak_ptr`?** Forcibly changing object management to `std::shared_ptr` just to use `std::weak_ptr`. This introduces unnecessary reference counting overhead and can easily cause performance bottlenecks in multi-threaded environments (atomic reference counting contention).

## Six Asynchronous Callback Capture Modes

Next, we will use actual code to compare six ways to capture object references in asynchronous callbacks. For each method, we will analyze: where the danger lies, what happens after the object is destroyed, and whether it is UB.

### Mode 1: Capturing Raw `this` — Dangerous

```cpp
// Capturing raw this
auto callback = [this]() {
    // If the object is destroyed before callback runs, `this` is dangling.
    this->doSomething(); // UB
};
```

**Problem**: `this` is just a raw pointer and carries no lifetime information. After the object is destructed, `this` in the callback is a dangling pointer, and any member access is UB. This is the most common source of crashes in C++ asynchronous programming.

### Mode 2: Capturing Raw `T*` — Equally Dangerous

```cpp
// Capturing raw T*
T* raw_ptr = getPointer();
auto callback = [raw_ptr]() {
    raw_ptr->doSomething(); // UB if object destroyed
};
```

**Problem**: There is no essential difference from capturing `this`. `T*` provides no lifetime guarantees. The only difference is that it "looks" like a conscious capture of a pointer, but it is actually no safer than a raw `this`.

### Mode 3: Capturing `ObserverPtr` — Still Dangerous

```cpp
// Capturing ObserverPtr
auto callback = [observer]() {
    if (observer) { // Check passes
        observer->doSomething(); // UB
    }
};
```

**Problem**: `ObserverPtr`'s `operator bool` only checks if the internal pointer is `nullptr`. After the object is destructed, the internal pointer is not `nullptr` (it is dangling), so the check passes, and then the dangling pointer is dereferenced. UB.

### Mode 4: Capturing `WeakPtr` (Custom) — UB

```cpp
// Capturing custom WeakPtr
auto callback = [weak]() {
    if (weak.get() != nullptr) { // UB here!
        weak.get()->doSomething();
    }
};
```

**Problem**: As analyzed in detail in the second article, the control block accessed by `weak.get()` may already be a dangling pointer. The null check itself is UB. This is the most insidious danger of the six modes—it looks like there is a "liveness" check mechanism, but even the check itself is unsafe.

### Mode 5: Capturing Chrome-like `WeakPtr` — Correct

```cpp
// Capturing Chrome-like WeakPtr
auto callback = [weak]() {
    if (weak.get() != nullptr) { // Safe check
        weak.get()->doSomething();
    }
};
```

**Analysis**: `weak.get()` first checks the control block. Since the control block is reference-counted, as long as the `Factory` is alive, the control block must exist, so the check won't be UB. After the object is destructed, the Factory's destructor invalidates the `WeakPtr`, `get()` returns `nullptr`, and the callback safely skips.

**But there is a premise**: The execution of the callback and the destruction of the object are on the same sequence. If crossing sequences, after `get()` returns non-null but before actually using the pointer, another sequence might be destructing the object—this is a TOCTOU race.

### Mode 6: Capturing `std::weak_ptr` — Correct

```cpp
// Capturing std::weak_ptr
auto callback = [weak]() {
    if (auto shared = weak.lock()) { // Atomic operation
        shared->doSomething(); // Safe
    }
};
```

**Analysis**: `lock()` is an atomic operation—it either returns a valid `std::shared_ptr` (with reference count +1) or returns empty. If it returns a valid `std::shared_ptr`, the object will definitely not be destructed during the lifetime of your `shared` variable. This is safer than Chrome WeakPtr—it not only detects invalidation but also prevents the object from being destructed between detection and use.

**But the cost is**: The object must be managed by `std::shared_ptr`, and `lock()` adds atomic reference counting operations. In high-frequency asynchronous scenarios, these atomic operations can become a performance bottleneck.

## Summary of Six Modes

| Mode | Liveness Check | Behavior After Object Destruction | UB? | Suitable Scenario |
|------|----------------|-----------------------------------|-----|-------------------|
| Raw `this` | None | Dangling pointer access | Yes | None—never capture raw `this` in async callbacks |
| Raw `T*` | None | Dangling pointer access | Yes | None—same as above |
| `ObserverPtr` | None | Check passes but pointer is dangling | Yes | Synchronous observation, not for async callbacks |
| Custom `WeakPtr` | Fake | Null check itself is UB | Yes | None—should not be used |
| Chrome `WeakPtr` | Yes (control block) | Safely returns nullptr | No (single sequence) | Async callbacks for non-shared_ptr objects |
| `std::weak_ptr` | Yes (shared_ptr control) | Safely returns empty shared_ptr | No | Async callbacks for shared_ptr managed objects |

## Conclusion

- `std::weak_ptr` depends on `std::shared_ptr`, and `lock()` temporarily extends the object's lifetime.
- Chrome-like `WeakPtr` does not depend on `std::shared_ptr`, does not extend the object's lifetime, and only detects invalidation.
- Do not forcibly change object management to `std::shared_ptr` just to use `std::weak_ptr`.
- Never capture raw `this`, raw `T*`, `ObserverPtr`, or custom `WeakPtr` in asynchronous callbacks.
- Chrome `WeakPtr` is suitable for non-`shared_ptr` scenarios, but be aware of sequence binding.
- `std::weak_ptr` is suitable for `shared_ptr` scenarios; `lock()` provides stronger safety guarantees.

## References

- [std::weak_ptr - cppreference](https://en.cppreference.com/w/cpp/memory/weak_ptr)
- [std::enable_shared_from_this - cppreference](https://en.cppreference.com/w/cpp/memory/enable_shared_from_this)
- [C++ Core Guidelines - CP.51: Do not use capturing lambdas that are coroutines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
- [Chromium WeakPtr Design Document](https://www.chromium.org/developers/weak-ptrs-in-chromium/)
