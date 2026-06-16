---
chapter: 0
cpp_standard:
- 23
description: Gain a deep understanding of C++23's `std::move_only_function`—the core
  storage type of `OnceCallback`. We cover the evolution from `std::function`, Small
  Buffer Optimization (SBO) behavior, and why `OnceCallback` requires independent
  three-state management.
difficulty: intermediate
order: 5
platform: host
prerequisites:
- OnceCallback 前置知识速查：C++11/14/17 核心特性回顾
- OnceCallback 前置知识（一）：函数类型与模板偏特化
reading_time_minutes: 9
related:
- OnceCallback 实战（二）：核心骨架搭建
- OnceCallback 实战（六）：测试与性能对比
tags:
- host
- cpp-modern
- intermediate
- 函数对象
- 智能指针
title: 'Prerequisites for OnceCallback (Part 5): std::move_only_function (C++23)'
translation:
  source: documents/vol9-open-source-project-learn/chrome/01_once_callback/full/pre-05-once-callback-move-only-function.md
  source_hash: e0cd8cad6cee646a86f5196837ef97f53ff080452df2faf67cc0d1d398df01e9
  translated_at: '2026-06-16T04:13:45.026070+00:00'
  engine: anthropic
  token_count: 1739
---
# OnceCallback Prerequisites (Part 5): std::move_only_function (C++23)

## Introduction

`std::move_only_function` is the heart of `OnceCallback`—it handles all the heavy lifting of type erasure. The `OnceCallback::callback_` member is a `std::move_only_function` type, which wraps various forms of callable objects—lambdas, function pointers, functors—into a unified calling interface with a known signature.

In this post, we will clarify three things: what exactly is the difference between `std::move_only_function` and `std::function`, how its SBO (Small Buffer Optimization) behavior works, and why `OnceCallback` cannot rely directly on its null-check mechanism and needs to implement its own three-state management.

> **Learning Objectives**
>
> - Understand the design motivation of `std::move_only_function`—why `std::function` isn't enough
> - Master the four core operations: construction, move, invocation, and null checking
> - Understand the principles of SBO and the allocation behavior of `std::move_only_function`
> - Clarify why `OnceCallback` needs an independent `Status` enumeration

---

## From std::function to std::move_only_function

### Limitations of std::function

`std::function` is a generic callable object container introduced in C++11. It unifies various callable objects into the same interface through type erasure. However, `std::function` has a fundamental limitation: it requires that the stored callable object **must be copyable**.

The reason is that `std::function` itself is copyable—when you copy a `std::function`, it needs to copy the internally stored callable object as well. If you attempt to construct a `std::function` using a lambda that captures a `std::unique_ptr`, the compiler will error out directly on copy semantics:

```cpp
// std::function requires the stored object to be copyable
std::function<void()> f = [up = std::make_unique<int>(42)] {
    *up = 100;
};

// Error: std::unique_ptr is not copyable
std::function<void()> f2 = f;
```

This limitation is fatal in the context of `OnceCallback`—the core selling point of `OnceCallback` is being move-only, and it must support lambdas that capture move-only types like `std::unique_ptr`.

### The Solution: std::move_only_function

`std::move_only_function` (C++23, defined in `<functional>`) is the "move-only version of `std::function`". It deletes copy operations and retains only move operations, thus no longer requiring the stored callable object to be copyable.

```cpp
// std::move_only_function only requires the object to be movable
std::move_only_function<void()> f = [up = std::make_unique<int>(42)] {
    *up = 100;
};

// OK: Transfer ownership
std::move_only_function<void()> f2 = std::move(f);
```

The key difference in interface between the two types can be summarized as: `std::function` is copyable and movable, requiring the stored object to be copyable; `std::move_only_function` is not copyable but is movable, only requiring the stored object to be movable.

---

## Four Core Operations

### Construction: Creating from Callable Objects

`std::move_only_function` accepts any callable object matching the signature `R(Args...)`—lambdas, function pointers, functors, and even another `std::move_only_function`:

```cpp
// From a lambda
std::move_only_function<void(int)> f1 = [](int x) { std::cout << x; };

// From a function pointer
void func(int x);
std::move_only_function<void(int)> f2 = func;

// From another move_only_function (move construction)
std::move_only_function<void(int)> f3 = std::move(f1);
```

### Move: Transferring Ownership

The move operation transfers the callable object from the source to the target. After the move, the state of the source object is **unspecified**—the standard does not guarantee that it will definitely be empty.

```cpp
std::move_only_function<void()> f = [] { /* ... */ };
std::move_only_function<void()> g = std::move(f);

// f is now in an unspecified state
// DO NOT rely on f being empty or non-empty!
```

This is very important—and one of the reasons why `OnceCallback` needs its own `Status` enumeration. We will expand on this later.

### Invocation: Executing via operator()

The invocation syntax is the same as `std::function`—using the `operator()` directly:

```cpp
std::move_only_function<int(int, int)> add = [](int a, int b) {
    return a + b;
};

int result = add(3, 4); // result is 7
```

If the `std::move_only_function` is empty (via default construction or `std::move`), the invocation will throw a `std::bad_function_call` exception.

### Null Check: Checking if it Holds a Callable Object

Use `operator bool` or compare with `nullptr`:

```cpp
std::move_only_function<void()> f;

if (!f) {
    std::cout << "f is empty\n";
}

f = [] { std::cout << "Hello"; };
if (f != nullptr) {
    f(); // Invokes the lambda
}
```

You can also actively clear it by assigning `nullptr`:

```cpp
f = nullptr; // Clears the stored callable
```

---

## SBO: Small Object Optimization

### What is SBO

`std::move_only_function` (just like `std::function`) internally implements **Small Buffer Optimization** (SBO). The idea is simple: a fixed-size buffer (usually a few pointers in size) is reserved inside the object. If the callable object is small enough, it is stored directly in this buffer, avoiding heap allocation; if it is too large, memory is allocated on the heap to store it.

![SBO 小对象优化内部结构](./pre-05-sbo-structure.drawio)

The threshold for SBO is implementation-defined—typically around 2-3 pointer sizes (16-24 bytes). A lambda capturing a few arguments (e.g., a `std::unique_ptr` or a few integers) can usually fit into the SBO without triggering a heap allocation. However, if the lambda captures a large amount of data (like a `std::string` + several `std::vector`s), exceeding the SBO threshold, construction will allocate on the heap.

### sizeof Comparison

```cpp
std::cout << sizeof(std::function<void()>) << "\n";   // e.g., 32 bytes
std::cout << sizeof(std::move_only_function<void()>) << "\n"; // e.g., 32 bytes
```

On GCC, typical values are `std::function` at about 32 bytes, and `std::move_only_function` also at about 32 bytes. They are similar in size because they use similar SBO strategies.

---

## Why OnceCallback Needs an Independent Status Enumeration

You may have noticed a detail—`OnceCallback` adds its own `Status` enumeration to track state, separate from `std::move_only_function`. Why not just use `std::move_only_function`'s null-check mechanism?

The reason is that `std::move_only_function`'s null check cannot distinguish between three different states:

```cpp
enum class Status {
    kEmpty,    // Never assigned a value
    kValid,    // Has a value, can be invoked
    kConsumed  // Was valid, but has been moved out or invoked
};
```

`std::move_only_function`'s `operator bool` can only distinguish between "empty" and "non-empty" states. However, `OnceCallback` needs to know whether a callback is "never been assigned" (`kEmpty`) or "had a value but has already been invoked" (`kConsumed`). These two scenarios have completely different meanings during debugging—`kEmpty` implies "you forgot to assign a callback", while `kConsumed` implies "the callback was correctly invoked, and you should not use it again".

There is a more subtle issue: the state of a moved-from `std::move_only_function` is **unspecified**—the standard does not guarantee that `operator bool` of the source object returns `false` after a move. Some implementations might still return `true`, even though the internal data is invalid. If `OnceCallback` relied on `std::move_only_function`'s null check to determine state, it might get incorrect results after move operations. The independent `Status` enumeration is entirely under our control—the move constructor explicitly sets the source object to `kEmpty`, leaving no ambiguity.

---

## Comparison with Chromium's BindState

Chromium does not use the standard library's type erasure facilities—it hand-writes a `BindState` system. Let's compare the core differences between the two approaches.

Chromium's `BindState` is a heap-allocated object that stores the callable object and all bound parameters. `OnceCallback` itself only holds a smart pointer (`std::unique_ptr`) to the `BindState`, making it only 8 bytes in size—the size of a pointer. All state is placed in the heap-allocated `BindState`, and the callback object itself is just a "thin proxy".

Our approach replaces the entire `BindState` layer with `std::move_only_function`—it implements type erasure and SBO internally, saving us the work of hand-writing function pointer tables, SBO buffers, and move/destructor operations. The cost is that the object size expands from 8 bytes to about 32 bytes (the size of `std::move_only_function` itself), plus the `Status` enumeration and an optional `std::unique_ptr` pointer, making the whole `OnceCallback` about 56-64 bytes.

| Metric | Chromium BindState | Our std::move_only_function |
|--------|-------------------|-------------------------------|
| Callback Object Size | 8 bytes (one pointer) | 56-64 bytes |
| Heap Allocation | Always (new BindState) | Only when lambda exceeds SBO threshold |
| Move Cost | Copying one pointer | Copying 32+ bytes |
| Implementation Complexity | High (manual ref-count + function pointer table) | Low (reuse standard library) |

For educational purposes and most practical scenarios, a 56-64 byte callback object is not a bottleneck at all. If your project indeed requires extreme compactness, you can refer to Chromium's approach—we will cover the core concepts in a future practical post.

---

## Summary

In this post, we clarified the ins and outs of `std::move_only_function`. It is the move-only version of `std::function` introduced in C++23, removing copy operations to support move-only callable objects. It implements SBO internally to optimize storage for small objects. However, its post-move state is unspecified, and it can only distinguish between "empty" and "non-empty" states—this is why `OnceCallback` needs an independent three-state `Status` enumeration. Compared to Chromium's hand-written `BindState`, we traded an increase in object size for a significant gain in implementation simplicity.

In the next post, we will look at the last prerequisite for `OnceCallback`—C++23's deducing this (explicit object parameter), which is the core mechanism enabling `OnceCallback::Invoke` to intercept compile-time lvalue/rvalue dispatch.

## References

- [cppreference: std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [P0288R9 - move_only_function Proposal](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p0288r9.html)
- [cppreference: std::function](https://en.cppreference.com/w/cpp/utility/functional/function)
