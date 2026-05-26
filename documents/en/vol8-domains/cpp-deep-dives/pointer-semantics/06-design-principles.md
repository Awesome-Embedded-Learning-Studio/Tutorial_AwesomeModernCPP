---
title: Cross-Thread Safety, Performance Trade-offs, and Design Principles Summary
description: Lifetime safety does not equal thread safety—the ultimate engineering
  rules and recommended naming conventions for pointer-semantic design
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
- std::weak_ptr 对比与异步回调实战
related:
- 卷二 · 第一章：智能指针与 RAII
cpp_standard:
- 17
- 20
translation:
  source: documents/vol8-domains/cpp-deep-dives/pointer-semantics/06-design-principles.md
  source_hash: 1377d6587f4356ac48fbc24fad885afa68c2202f49efd783e4cae39993492fca
  translated_at: '2026-05-26T11:56:27.979531+00:00'
  engine: anthropic
  token_count: 1283
---
# Cross-Thread Safety, Performance Trade-offs, and Design Principles Summary

## Introduction

By this point, we have walked through the entire spectrum of non-owning pointers—from the simplest `T*` and `T&`, to hand-rolled `Borrowed<T>` and `ObserverPtr<T>`, to three weak reference approaches (`UnsafeWeakPtr`, `SimpleWeakPtr`, and Chrome-like `WeakPtr`), and finally a comprehensive comparison with `std::weak_ptr`.

This chapter serves as the conclusion. We will clarify three topics that haven't been fully explored yet: where exactly the boundaries of cross-thread safety lie, how the performance overhead of each type compares, and how to synthesize everything into a set of actionable engineering rules.

## Lifetime Safety ≠ Thread Safety

This is the most important conclusion of the entire series, and it is worth repeating.

**Lifetime safety** means "can you safely detect invalidation after an object is destroyed." The control block of `WeakPtr` solves exactly this problem—you can safely call `is_valid()` or `get()` without invoking undefined behavior (UB).

**Thread safety** means "will problems arise when multiple threads access the object simultaneously." This is a completely different dimension from lifetime safety.

We can use a 2×2 table to clarify these four quadrants:

|  | Lifetime Unsafe | Lifetime Safe |
|------|--------------|------------|
| **Thread Unsafe** | `T*`, `T&`, `ObserverPtr` | Chrome `WeakPtr` (single sequence) |
| **Partially Thread Safe** | N/A (meaningless) | `std::weak_ptr` (atomic lock, but T's internal state requires synchronization) |

`T*` sits in the top-left corner—solving neither the lifetime problem nor the thread problem. Chrome `WeakPtr` solves the lifetime problem, but still suffers from TOCTOU races in cross-thread scenarios. The `lock()` of `std::weak_ptr` is atomic, and once locked, the object will not be destroyed, but concurrent access to the object's **internal state** still requires protection from a mutex or other mechanisms.

So: `WeakPtr` solves "do I know if the object is dead," not "is it safe for multiple threads to touch this object at the same time."

### Why Chrome WeakPtr is Sequence-Bound

Chrome's design philosophy is that most callbacks in UI and asynchronous frameworks run on the same logical sequence. Timer callbacks, event handling, IO completion notifications—they are all dispatched and executed by the same task runner. Under this model, `invalidate` and `get` cannot execute simultaneously because they run sequentially in a queue.

This is far more efficient than "just add a mutex for cross-thread safety"—a mutex incurs runtime overhead, whereas being sequence-bound is a zero-overhead design constraint. The trade-off is that your usage pattern is restricted: you cannot pass a WeakPtr across sequences. However, this constraint naturally holds true in most UI and event loop frameworks.

### How to Handle Cross-Thread Scenarios

If you genuinely need cross-thread weak references, there are a few options:

- **Use `std::weak_ptr`**: `lock()` atomically acquires a `shared_ptr`, ensuring the object will not be destroyed within your scope. However, the thread safety of T's internals must be handled separately.
- **Use `std::atomic<std::shared_ptr<T>>` (C++20)**: Provides atomic operations to safely read and write `shared_ptr` across threads.
- **Use message passing**: Instead of sharing a WeakPtr directly across threads, pass a "please do this on your sequence" request through a message queue, letting the target sequence handle it itself.

## Performance Comparison

Let's do a performance comparison of all the types covered in this series. The numbers are approximate and depend on the specific platform and compiler:

| Type | Object Size | Control Block Allocation | Atomic Operations | Best For |
|------|---------|-----------|---------|------|
| `T*` | 8B | None | None | Synchronous function parameters |
| `T&` | 8B (pointer implementation) | None | None | Synchronous function parameters |
| `Borrowed<T>` | 8B | None | None | Synchronous function parameters (explicit semantics) |
| `ObserverPtr<T>` | 8B | None | None | Class member observation |
| `UnsafeWeakPtr<T>` | 16B | None | None | Should not be used |
| `SimpleWeakPtr<T>` | 24B (T* + shared_ptr) | 1x `new` | 1x copy, 1~2x destruction atomic ops | Teaching, simple scenarios |
| Chrome `WeakPtr<T>` | 16B (`T*` + `WeakFlag*`) | 1x `new` | 1x atomic op each for copy/destruction | In-framework async callbacks |
| `std::weak_ptr<T>` | 16B | Managed by `shared_ptr` | 2x atomic ops each for lock/unlock | shared_ptr ecosystem |

A few details worth noting:

The overhead of `Borrowed<T>` and `ObserverPtr<T>` is zero—after compiler optimization, they are identical to raw pointers. Their value lies purely in semantics.

`SimpleWeakPtr<T>` is 8 bytes larger than Chrome `WeakPtr<T>` because `shared_ptr` internally holds two pointers (object pointer + control block pointer), whereas Chrome's `WeakPtr` only stores `T*` and `WeakFlag*`. Every copy of `shared_ptr` requires two atomic operations (strong count + weak count), while Chrome only needs one.

The control block of Chrome `WeakPtr` (`WeakFlag`) is much smaller than that of `shared_ptr`—it contains only one atomic bool and one atomic int, with no virtual destructor, no allocator, and no weak count.

The extra overhead of `std::weak_ptr` depends on the `shared_ptr` it relies on. If you force an object that doesn't originally need `shared_ptr` to be managed by `shared_ptr` just to use `weak_ptr`, you not only pay the cost of the control block but also introduce the risk of atomic reference count contention.

## Engineering Rules

Summarized into a set of actionable rules:

**For function parameters**, prefer `T&`, `T*`, or `Borrowed<T>`. Do not use smart pointers to express non-owning relationships in function parameters. `Borrowed<T>` provides the most explicit semantics (non-null + non-owning), but `const T&` is sufficient in most scenarios.

**For class member observation**, you can use `ObserverPtr<T>`. When you want to express "I observe it but don't own it," `ObserverPtr<T>` is much more readable than a raw `T*`. But remember that it cannot check liveness.

**For async callbacks**, never capture a raw `this`, a raw `T*`, `ObserverPtr`, or any "weak reference" without an independent control block. The correct choices are a Chrome-like `WeakPtr<T>` (for non-`shared_ptr` scenarios) or `std::weak_ptr<T>` (for `shared_ptr` scenarios).

**Do not use `ObserverPtr` as a WeakPtr**. `ObserverPtr` can only express "I don't own it"; it cannot express "do I know if it's still alive."

**Do not call `T* + raw Flag*` a WeakPtr**. If the Flag's lifetime is bound to the Owner, it is not a reliable WeakPtr. Give it an honest name—`UnsafeWeakPtr` or `OwnerBoundWeakPtr`.

**For cross-thread scenarios**, prefer `std::weak_ptr<T>` or message passing. Chrome-like `WeakPtr` is designed to be sequence-bound; do not use it as a cross-thread safe pointer.

**WeakPtr solves lifetime awareness, not thread safety**. Regardless of which WeakPtr you use, concurrent access to T's internal state requires additional synchronization mechanisms.

## Recommended Naming System

Finally, here is a set of recommended naming conventions:

| Type | Name | Meaning |
|------|------|------|
| `Borrowed<T>` | Borrow | Non-null, non-owning, short-term use, suitable for function parameters |
| `ObserverPtr<T>` | Observer | Nullable, non-owning, no liveness check, suitable for class members |
| `UnsafeWeakPtr<T>` | Unsafe Weak Reference | `T*` + `raw Flag*`, the name explicitly flags it as unsafe |
| `WeakPtr<T>` | Safe Weak Reference | A true weak reference that can safely check for null after the object is destroyed |
| `WeakPtrFactory<T>` | Weak Reference Factory | Centrally creates and manages the invalidation of WeakPtrs |

The name `UnsafeWeakPtr` is not derogatory—it is an **honest naming**. When you see `UnsafeWeakPtr` in a codebase, you immediately know "this has pitfalls, pay attention to the constraints when using it." This is far more responsible than wrapping it as `WeakPtr` and burying a single line of fine print in the documentation saying "ensure the WeakPtr does not outlive the Owner."

## Summary

- Lifetime safety and thread safety are two orthogonal problems; WeakPtr only solves the former
- Chrome `WeakPtr` achieves zero-overhead safety through the sequence-bound model, but restricts cross-thread usage
- `Borrowed` and `ObserverPtr` have zero runtime overhead; their value lies in semantic expression
- The control block of Chrome `WeakPtr` is lighter than that of `shared_ptr`
- Do not force the introduction of `shared_ptr` just to use `weak_ptr`
- Naming should be honest—if something is unsafe, call it unsafe

This concludes the entire series. Starting from `T*`, we hand-rolled Borrowed, ObserverPtr, UnsafeWeakPtr, SimpleWeakPtr, and a Chrome-like WeakPtr, explaining the design rationale and engineering trade-offs at every step. We hope this content helps you make clearer pointer semantic choices in your real-world engineering work.

## References

- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
- [Chromium Smart Pointer Guidelines](https://www.chromium.org/developers/smart-pointer-guidelines/)
- [std::weak_ptr - cppreference](https://en.cppreference.com/w/cpp/memory/weak_ptr)
- [GSL: Guidelines Support Library](https://github.com/microsoft/GSL)
- [P1408R0: Abandon observer_ptr (Stroustrup)](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1408r0.pdf)
