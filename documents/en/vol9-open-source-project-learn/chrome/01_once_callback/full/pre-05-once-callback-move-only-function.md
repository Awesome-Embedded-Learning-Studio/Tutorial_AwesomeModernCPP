---
title: 'Prerequisites for OnceCallback (Part 5): std::move_only_function (C++23)'
description: An in-depth look at C++23's std::move_only_function—the core storage
  type of OnceCallback—covering the evolution from std::function, SBO behavior, and
  why OnceCallback requires independent three-state management.
chapter: 0
order: 5
tags:
- host
- cpp-modern
- intermediate
- 函数对象
- 智能指针
difficulty: intermediate
platform: host
cpp_standard:
- 23
reading_time_minutes: 10
prerequisites:
- OnceCallback 前置知识速查：C++11/14/17 核心特性回顾
- OnceCallback 前置知识（一）：函数类型与模板偏特化
related:
- OnceCallback 实战（二）：核心骨架搭建
- OnceCallback 实战（六）：测试与性能对比
translation:
  source: documents/vol9-open-source-project-learn/chrome/01_once_callback/full/pre-05-once-callback-move-only-function.md
  source_hash: 61ad68cd231af8ec4dafcf8a45948cfa4ec4353ca184e11c04f06387fe805c6b
  translated_at: '2026-05-26T12:29:11.875311+00:00'
  engine: anthropic
  token_count: 1747
---
# Prerequisites for OnceCallback (Part 5): std::move_only_function (C++23)

## Introduction

`std::move_only_function` is the heart of OnceCallback—it handles all the heavy lifting of type erasure. OnceCallback's `func_` member is of type `std::move_only_function<FuncSig>`, which uniformly wraps lambdas, function pointers, functors, and other callable forms into a single calling interface with a known signature.

In this post, we need to clarify three things: what exactly distinguishes `std::move_only_function` from `std::function`, how its SBO (Small Buffer Optimization) behavior works, and why OnceCallback cannot directly rely on its emptiness-checking mechanism and instead needs its own three-state management.

> **Learning Objectives**
>
> - Understand the design motivation behind `std::move_only_function`—why `std::function` isn't enough
> - Master the four core operations: construction, moving, invocation, and emptiness checking
> - Understand the principles of SBO and the allocation behavior of `std::move_only_function`
> - Understand why OnceCallback needs an independent `Status` enumeration

---

## From std::function to std::move_only_function

### Limitations of std::function

`std::function` is a general-purpose callable object wrapper introduced in C++11. It uses type erasure to unify various callable objects into a single interface. However, `std::function` has a fundamental limitation: it requires the stored callable object to be **copyable**.

The reason is that `std::function` itself is copyable—when you copy a `std::function`, it needs to copy the internally stored callable object as well. If you try to construct a `std::function` using a lambda that captures a `std::unique_ptr`, the compiler will throw an error directly on the copy semantics:

```cpp
#include <functional>
#include <memory>

auto ptr = std::make_unique<int>(42);

// 编译错误！unique_ptr 不可拷贝，std::function 要求可拷贝
std::function<int()> f = [p = std::move(ptr)]() { return *p; };
```

This limitation is fatal in the context of OnceCallback—OnceCallback's core selling point is being move-only, and it must support lambdas that capture `unique_ptr`.

### The std::move_only_function Solution

`std::move_only_function` (C++23, defined in `<functional>`) is the "move-only version of `std::function`". It deletes copy operations and only retains move operations, thereby no longer requiring the stored callable object to be copyable.

```cpp
#include <functional>
#include <memory>

auto ptr = std::make_unique<int>(42);

// OK！move_only_function 不要求可拷贝
std::move_only_function<int()> f = [p = std::move(ptr)]() { return *p; };

int result = f();  // result == 42
```

The key difference between the two types in terms of interface can be summarized as: `std::function` is copyable and movable, requiring the stored object to be copyable; `std::move_only_function` is not copyable but movable, only requiring the stored object to be movable.

---

## Four Core Operations

### Construction: Creating from a Callable Object

`std::move_only_function<R(Args...)>` accepts any callable object that matches the signature `R(Args...)`—lambdas, function pointers, functors, or even another `std::move_only_function`:

```cpp
// 从 lambda 构造
std::move_only_function<int(int, int)> f1 = [](int a, int b) { return a + b; };

// 从函数指针构造
int add(int a, int b) { return a + b; }
std::move_only_function<int(int, int)> f2 = &add;

// 从仿函数构造
struct Multiplier {
    int operator()(int a, int b) { return a * b; }
};
std::move_only_function<int(int, int)> f3 = Multiplier{};

// 默认构造：创建空的 move_only_function
std::move_only_function<int()> f4;  // f4 == nullptr
```

### Moving: Transferring Ownership

A move operation transfers the callable object from the source to the target. After the move, the state of the source object is **unspecified**—the standard does not guarantee that it will be empty.

```cpp
std::move_only_function<int()> f = []() { return 42; };
auto g = std::move(f);
// f 的状态未指定——可能为空，也可能不为空
// 不要依赖 f 在移动后的行为
```

This point is crucial—and it is one of the reasons why OnceCallback needs its own `Status` enumeration. We will elaborate on this later.

### Invocation: Executing via operator()

The invocation syntax is the same as `std::function`—simply use the `()` operator:

```cpp
std::move_only_function<int(int, int)> f = [](int a, int b) { return a + b; };
int result = f(3, 4);  // result == 7
```

If `f` is empty (via default construction or `= nullptr`), invoking it throws a `std::bad_function_call` exception.

### Emptiness Checking: Checking if a Callable Object is Held

Via `operator bool()` or by comparing with `nullptr`:

```cpp
std::move_only_function<int()> f;
if (!f) {
    std::cout << "f is empty\n";
}
// 等价于
if (f == nullptr) {
    std::cout << "f is empty\n";
}

f = []() { return 42; };
if (f) {
    std::cout << "f is not empty\n";
}
```

We can also actively clear it by assigning `nullptr`:

```cpp
f = nullptr;  // 清空 f，析构之前持有的可调用对象
```

---

## SBO: Small Buffer Optimization

### What is SBO

`std::move_only_function` (like `std::function`) internally implements **Small Buffer Optimization** (SBO). The idea is simple: a fixed-size buffer (usually a few pointers in size) is reserved inside the object. If the callable object is small enough, it is stored directly in the buffer, avoiding heap allocation; if it is too large, memory is allocated on the heap to store it.

![SBO small object optimization internal structure](./pre-05-sbo-structure.drawio)

The SBO threshold is implementation-defined—typically around two to three pointer sizes (16-24 bytes). Lambdas that capture a small number of parameters (such as `[x = 42]` or `[&ref]`) usually fit into the SBO without triggering heap allocation. However, if a lambda captures a large amount of data (such as a `std::string` plus a few `int`), exceeding the SBO threshold, a heap allocation will occur during construction.

### sizeof Comparison

```cpp
#include <functional>
#include <iostream>

int main() {
    std::cout << "sizeof(std::function<void()>):        "
              << sizeof(std::function<void()>) << "\n";
    std::cout << "sizeof(std::move_only_function<void()>): "
              << sizeof(std::move_only_function<void()>) << "\n";
}
```

On GCC, typical values are `std::function<void()>` at about 32 bytes, and `std::move_only_function<void()>` also at about 32 bytes. The two are similar in size because they use a similar SBO strategy.

---

## Why OnceCallback Needs an Independent Status Enumeration

You may have noticed a detail—OnceCallback adds its own `Status` enumeration to track state, in addition to `std::move_only_function`. Why not just use the emptiness-checking mechanism of `std::move_only_function`?

The reason is that the emptiness check of `std::move_only_function` cannot distinguish between three different states:

```cpp
enum class Status : uint8_t {
    kEmpty,     // 从未被赋值（默认构造）
    kValid,     // 持有有效的可调用对象
    kConsumed   // 已被 run() 调用过
};
```

The `operator bool()` of `std::move_only_function` can only distinguish between "empty" and "non-empty" states. However, OnceCallback needs to know whether a callback "has never been assigned a value" (kEmpty) or "once had a value but has already been invoked" (kConsumed). These two scenarios have completely different meanings during debugging—kEmpty means "you forgot to assign a value to the callback," while kConsumed means "the callback has already been correctly invoked, and you should not use it again."

There is also a more subtle issue: the state of `std::move_only_function` after a move is **unspecified**—the standard does not guarantee that the `operator bool()` of the source object returns `false` after a move. Certain implementations might still return `true`, even though the internal data is already invalid. If OnceCallback relied on the emptiness check of `std::move_only_function` to determine state, it might get incorrect results after a move operation. The independent `Status` enumeration is entirely under our control—the move constructor explicitly sets the source object to `kEmpty`, leaving no ambiguity.

---

## Comparison with Chromium's BindState

Chromium does not use the standard library's type erasure facilities—it hand-writes a `BindState` system. Let's compare the core differences between the two approaches.

Chromium's `BindState<Functor, BoundArgs...>` is a heap-allocated object that stores the callable object and all bound parameters. `OnceCallback` itself only holds a smart pointer (`scoped_refptr`) pointing to the `BindState`, with a size of just 8 bytes—one pointer. All state is placed in the heap-allocated `BindState`, and the callback object itself is merely a "thin proxy."

Our approach uses `std::move_only_function` to replace the entire `BindState` layer—it internally implements type erasure and SBO, saving us the work of hand-writing function pointer tables, SBO buffers, and move/destruction operations. The trade-off is that the object size bloats from 8 bytes to about 32 bytes (the size of `std::move_only_function` itself), and with the addition of the `Status` enumeration and the optional `CancelableToken` pointer, the entire `OnceCallback` is about 56-64 bytes.

| Metric | Chromium BindState | Our std::move_only_function |
|--------|-------------------|-------------------------------|
| Callback object size | 8 bytes (one pointer) | 56-64 bytes |
| Heap allocation | Always (new BindState) | Only when the lambda exceeds the SBO threshold |
| Move cost | Copying one pointer | Copying 32+ bytes |
| Implementation complexity | Very high (hand-written reference counting + function pointer table) | Low (reusing the standard library) |

For teaching purposes and most practical scenarios, a 56-64 byte callback object is not a bottleneck at all. If your project truly requires extreme compactness, you can refer to Chromium's approach—we will explain the core ideas clearly in the subsequent hands-on chapters.

---

## Summary

In this post, we clarified the origins and workings of `std::move_only_function`. It is the move-only version of `std::function` introduced in C++23, deleting copy operations to support move-only callable objects. It internally implements SBO to optimize the storage of small objects. However, its post-move state is unspecified, and it can only distinguish between "empty" and "non-empty" states—this is why OnceCallback needs an independent three-state `Status` enumeration. Compared to Chromium's hand-written `BindState`, we traded an increase in object size for a significant boost in implementation simplicity.

In the next post, we will look at the final prerequisite for OnceCallback—C++23's deducing this (explicit object parameter), which is the core mechanism enabling the `run()` method to intercept lvalue/rvalue dispatch at compile time.

## References

- [cppreference: std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [P0288R9 - move_only_function proposal](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p0288r9.html)
- [cppreference: std::function](https://en.cppreference.com/w/cpp/utility/functional/function)
