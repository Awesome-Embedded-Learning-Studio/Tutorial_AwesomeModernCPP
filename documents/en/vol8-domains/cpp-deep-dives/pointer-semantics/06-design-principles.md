---
title: 'Series Wrap-Up: Cross-Thread Safety, Performance Trade-offs, and Design Principles'
description: Lifetime safety is not thread safety — the final engineering rules and recommended naming for pointer semantics
chapter: 1
order: 6
tags:
  - host
  - cpp-modern
  - intermediate
  - 智能指针
  - 内存管理
difficulty: intermediate
platform: host
reading_time_minutes: 7
prerequisites:
  - 'std::weak_ptr Compared: Async Callbacks in Practice'
related:
  - Smart Pointers and RAII
cpp_standard: [17, 20]
translation:
  source: documents/vol8-domains/cpp-deep-dives/pointer-semantics/06-design-principles.md
  source_hash: bf5f5251068ee15c1d69e7553bf2daa32d94762adc1dc722fe8cf89f6014f822
  translated_at: '2026-09-27T02:53:39+00:00'
  engine: anthropic
  token_count: 1900
---

# Series Wrap-Up: Cross-Thread Safety, Performance Trade-offs, and Design Principles

By this point we have walked the entire spectrum of non-owning pointers — from the simplest `T*` and `T&`, through the hand-rolled `Borrowed<T>` and `ObserverPtr<T>`, on to three weak-reference designs (`UnsafeWeakPtr`, `SimpleWeakPtr`, and a Chrome-like `WeakPtr`), and finally a comprehensive comparison against `std::weak_ptr`.

This article is the wrap-up. We will settle three topics we have not fully unpacked yet: where exactly the boundary of cross-thread safety lies, how much the performance overhead of each type actually differs, and how to fold everything into a set of engineering rules you can put to work.

## Lifetime Safety ≠ Thread Safety

This is the single most important conclusion of the entire series, and it deserves repeating.

**Lifetime safety** asks: "after the object is destroyed, can you safely detect the invalidation?" That is exactly the problem the `WeakPtr` control block solves — you can call `is_valid()` or `get()` safely without triggering UB.

**Thread safety** asks: "will anything go wrong when multiple threads access the object at the same time?" This is a completely different dimension of the problem from lifetime safety.

Let's lay out the four quadrants in a 2×2 table:

|  | Lifetime-Unsafe | Lifetime-Safe |
|------|--------------|------------|
| **Thread-Unsafe** | `T*`, `T&`, `ObserverPtr` | Chrome `WeakPtr` (single sequence) |
| **Thread-Safe (partially)** | N/A (meaningless) | `std::weak_ptr` (atomic lock, but T's internal state still needs synchronization) |

`T*` sits in the top-left corner — it solves neither the lifetime problem nor the thread problem. Chrome `WeakPtr` solves the lifetime problem, but still admits TOCTOU races in cross-thread scenarios. The `lock()` of `std::weak_ptr` is atomic, and once you have locked it the object will not be destroyed, yet concurrent access to the object's **internal state** still needs protection from a mutex or some other mechanism.

So: what `WeakPtr` solves is "do I know whether the object is dead or not", not "is it safe for multiple threads to touch this object at the same time".

### Why Chrome WeakPtr Is Sequence-Bound

Chrome's design philosophy is that most callbacks in UI and async frameworks run on the same logical sequence. Timer callbacks, event handling, IO-completion notifications — they are all dispatched and executed by the same task runner. Under this model, invalidate and get can never run simultaneously, because they execute in queue order.

This is far more efficient than "slap a mutex on it and it is cross-thread safe" — a mutex carries runtime overhead, whereas being sequence-bound is a zero-overhead design constraint. The price is that your usage is restricted: you cannot pass a WeakPtr across sequences. But in most UI / event-loop frameworks this constraint holds naturally.

### What to Do in Cross-Thread Scenarios

If you genuinely need a cross-thread weak reference, there are a few options:

- **Use `std::weak_ptr`**: `lock()` atomically acquires a `shared_ptr`, and the object will not be destroyed within your scope. The thread safety of T's internals, however, needs to be handled separately.
- **Use `std::atomic<std::shared_ptr<T>>`** (C++20): provides atomic operations to read and write a `shared_ptr` safely across threads.
- **Use message passing**: instead of sharing a WeakPtr across threads directly, send a "please do this on your sequence" request through a message queue and let the target sequence handle it itself.

## Performance Comparison

Let's put every type covered in this series side by side. The numbers are approximations and depend on the platform and compiler:

| Type | Object Size | Control Block Allocation | Atomic Operations | Best For |
|------|---------|-----------|---------|------|
| `T*` | 8B | None | None | Synchronous function parameters |
| `T&` | 8B (pointer implementation) | None | None | Synchronous function parameters |
| `Borrowed<T>` | 8B | None | None | Synchronous function parameters (explicit semantics) |
| `ObserverPtr<T>` | 8B | None | None | Class-member observation |
| `UnsafeWeakPtr<T>` | 16B | None | None | Should not be used |
| `SimpleWeakPtr<T>` | 24B (`T*` + `shared_ptr`) | 1 `new` | 1 atomic op on copy, 1–2 on destruction | Teaching, simple scenarios |
| Chrome `WeakPtr<T>` | 16B (`T*` + `WeakFlag*`) | 1 `new` | 1 atomic op each on copy/destruction | Async callbacks within a framework |
| `std::weak_ptr<T>` | 16B | Managed by `shared_ptr` | 2 atomic ops each on lock/unlock | The `shared_ptr` ecosystem |

A few details worth calling out:

The overhead of `Borrowed<T>` and `ObserverPtr<T>` is zero — after compiler optimization they are exactly the same as raw pointers. Their value is purely semantic.

`SimpleWeakPtr<T>` is 8 bytes larger than Chrome `WeakPtr<T>`, because a `shared_ptr` internally holds two pointers (object pointer + control block pointer), while Chrome's `WeakPtr` stores only `T*` and `WeakFlag*`. Every copy of a `shared_ptr` requires two atomic operations (strong count + weak count); Chrome needs only one.

The control block of Chrome `WeakPtr` (`WeakFlag`) is much smaller than a `shared_ptr` control block — just one atomic bool and one atomic int, with no virtual destructor, no allocator, and no weak count.

The extra overhead of `std::weak_ptr` depends on the `shared_ptr` it rides on. If you force an object that never needed `shared_ptr` into `shared_ptr` management just so you can use `weak_ptr`, you not only pay for the control block but also invite the risk of atomic reference-count contention.

## Engineering Rules

Condensed into a set of rules you can actually apply:

**Function parameters** — prefer `T&`, `T*`, or `Borrowed<T>`. Do not use smart pointers in function parameters to express a non-owning relationship. `Borrowed<T>` provides the most explicit semantics (non-null + non-owning), but `const T&` is good enough in most scenarios.

**Class-member observation relationships** — you can use `ObserverPtr<T>`. When you want to express "I observe it but do not own it", `ObserverPtr<T>` reads far better than a raw `T*`. Just remember that it cannot check liveness.

**Async callbacks** — never capture a raw `this`, a raw `T*`, an `ObserverPtr`, or any "weak reference" without an independent control block. The correct choices are a Chrome-like `WeakPtr<T>` (in non-`shared_ptr` scenarios) or `std::weak_ptr<T>` (in `shared_ptr` scenarios).

**Do not use `ObserverPtr` as a WeakPtr**. `ObserverPtr` can only express "I don't own it"; it cannot express "do I know whether it is still alive".

**Do not call `T* + raw Flag*` a WeakPtr**. If the Flag's lifetime is bound to the Owner, it is not a reliable WeakPtr. Give it an honest name — `UnsafeWeakPtr` or `OwnerBoundWeakPtr`.

**Cross-thread scenarios** — prefer `std::weak_ptr<T>` or message passing. Chrome-like `WeakPtr` is sequence-bound by design; do not use it as a cross-thread-safe pointer.

**WeakPtr solves lifetime awareness, not thread safety**. Whichever WeakPtr you use, concurrent access to T's internal state still requires an additional synchronization mechanism.

## Recommended Naming System

Finally, here is a recommended set of naming conventions:

| Type | Name | Meaning |
|------|------|------|
| `Borrowed<T>` | Borrow | Non-null, non-owning, short-term use, fits function parameters |
| `ObserverPtr<T>` | Observe | Nullable, non-owning, no liveness check, fits class members |
| `UnsafeWeakPtr<T>` | Unsafe weak reference | `T*` + `raw Flag*`, with the name openly flagging the unsafety |
| `WeakPtr<T>` | Safe weak reference | A true weak reference that can safely null-check after the object is destroyed |
| `WeakPtrFactory<T>` | Weak-reference factory | Creates WeakPtrs centrally and manages their invalidation |

The name `UnsafeWeakPtr` is not derogatory — it is **honest naming**. When you see `UnsafeWeakPtr` in a codebase, you know immediately that "this thing has pitfalls; mind the constraints when using it". That is far more responsible than dressing it up as `WeakPtr` and burying a line of fine print in the docs saying "ensure the WeakPtr does not outlive the Owner".

## Summary

- Lifetime safety and thread safety are two orthogonal problems; WeakPtr solves only the former
- Chrome `WeakPtr` achieves zero-overhead safety through its sequence-bound model, at the cost of restricting cross-thread usage
- `Borrowed` and `ObserverPtr` have zero runtime overhead; their value lies in semantic expression
- The control block of Chrome `WeakPtr` is lighter than that of `shared_ptr`
- Do not force `shared_ptr` onto an object just to use `weak_ptr`
- Naming should be honest — if something is unsafe, call it unsafe

This is where the series ends. Starting from `T*`, we hand-rolled Borrowed, ObserverPtr, UnsafeWeakPtr, SimpleWeakPtr, and a Chrome-like WeakPtr, explaining the design rationale and engineering trade-offs at every step. We hope this content helps you make clearer pointer-semantics choices in real-world engineering.

## References

- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
- [Chromium Smart Pointer Guidelines](https://www.chromium.org/developers/smart-pointer-guidelines/)
- [std::weak_ptr - cppreference](https://en.cppreference.com/w/cpp/memory/weak_ptr)
- [GSL: Guidelines Support Library](https://github.com/microsoft/GSL)
- [P1408R0: Abandon observer_ptr (Stroustrup)](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1408r0.pdf)
