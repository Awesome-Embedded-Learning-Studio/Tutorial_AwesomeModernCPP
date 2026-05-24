---
title: 'OnceCallback Prerequisites (Part 5): std::move_only_function (C++23)'
description: Deep dive into C++23's std::move_only_function—the core storage type
  of OnceCallback, from the evolution motivation of std::function to SBO behavior,
  and why OnceCallback requires independent three-state management
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
---
# Prerequisites for OnceCallback (Part 5): std::move_only_function (C++23)

## Introduction

`std::move_only_function` is the heart of OnceCallback—it handles all the heavy lifting of type erasure. OnceCallback's `func_` member is of type `std::move_only_function<FuncSig>`, which wraps lambdas, function pointers, functors, and other callable forms into a single, known-signature call interface.

In this post, we cover three things: what exactly differentiates `std::move_only_function` from `std::function`, how its SBO (Small Buffer Optimization) behavior works, and why OnceCallback cannot rely directly on its null-checking mechanism and instead needs its own three-state management.

> **Learning Objectives**
>
> - Understand the design motivation for `std::move_only_function`—why `std::function` isn't enough
> - Master the four core operations: construction, move, invocation, and null checking
> - Understand the principles of SBO and the allocation behavior of `std::move_only_function`
> - Understand why OnceCallback needs an independent `Status` enum

---

## From std::function to std::move_only_function

### Limitations of std::function

`std::function` is a general-purpose callable wrapper introduced in C++11 that uses type erasure to unify various callables under a single interface. But `std::function` has a fundamental limitation: it requires the stored callable to be **copyable**.

The reason is that `std::function` itself is copyable—when you copy a `std::function`, it needs to copy the internally stored callable as well. If you try to construct a `std::function` with a lambda that captures a `std::unique_ptr`, the compiler will error out on the copy semantics:

```cpp
#include <functional>
#include <memory>

auto ptr = std::make_unique<int>(42);

// 编译错误！unique_ptr 不可拷贝，std::function 要求可拷贝
std::function<int()> f = [p = std::move(ptr)]() { return *p; };
```

This limitation is fatal for OnceCallback's use case—OnceCallback's core selling point is being move-only, and it must support lambdas that capture `unique_ptr`.

### The std::move_only_function Solution

`std::move_only_function` (C++23, defined in `<functional>`) is the "move-only version of `std::function`". It deletes copy operations and only retains move operations, thus no longer requiring the stored callable to be copyable.

```cpp
#include <functional>
#include <memory>

auto ptr = std::make_unique<int>(42);

// OK！move_only_function 不要求可拷贝
std::move_only_function<int()> f = [p = std::move(ptr)]() { return *p; };

int result = f();  // result == 42
```

The key interface difference between the two types can be summarized as: `std::function` is copyable and movable, requiring the stored object to be copyable; `std::move_only_function` is not copyable but is movable, only requiring the stored object to be movable.

---

## Four Core Operations

### Construction: Creating from a Callable

`std::move_only_function<R(Args...)>` accepts any callable that matches the signature `R(Args...)`—lambdas, function pointers, functors, and even another `std::move_only_function`:

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

### Move: Transferring Ownership

A move operation transfers the callable from the source to the target. After the move, the source object's state is **unspecified**—the standard does not guarantee it will be empty.

```cpp
std::move_only_function<int()> f = []() { return 42; };
auto g = std::move(f);
// f 的状态未指定——可能为空，也可能不为空
// 不要依赖 f 在移动后的行为
```

This is very important—and one of the reasons OnceCallback needs its own `Status` enum. We will expand on this later.

### Invocation: Executing via operator()

The invocation syntax is the same as `std::function`—just use the `()` operator directly:

```cpp
std::move_only_function<int(int, int)> f = [](int a, int b) { return a + b; };
int result = f(3, 4);  // result == 7
```

If `f` is empty (via default construction or `= nullptr`), invoking it throws a `std::bad_function_call` exception.

### Null Checking: Checking Whether a Callable Is Held

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

We can also explicitly clear it by assigning `nullptr`:

```cpp
f = nullptr;  // 清空 f，析构之前持有的可调用对象
```

---

## SBO: Small Buffer Optimization

### What Is SBO

`std::move_only_function` (like `std::function`) internally implements **Small Buffer Optimization** (SBO). The idea is simple: the object reserves a fixed-size internal buffer (typically a few pointers in size). If the callable is small enough, it is stored directly in the buffer, avoiding heap allocation; if it is too large, memory is allocated on the heap to store it.

```mermaid
graph TD
    subgraph outer["std::move_only_function"]
        direction TB
        subgraph sbo_path["SBO path: small objects stored inline"]
            vtable["Function pointer / vtable pointer<br/><i>For type-erased call dispatch</i>"]
            sbo_buf["SBO buffer (typically 16-32 bytes)<br/><i>Small objects stored directly here</i>"]
        end
        subgraph heap_path["Heap path: large objects dynamically allocated"]
            heap_ptr["Heap pointer (points to dynamically allocated object)<br/><i>Large objects stored on the heap</i>"]
        end
    end
```

The SBO threshold is implementation-defined—typically around two to three pointers in size (16–24 bytes). A lambda capturing a small number of parameters (such as `[x = 42]` or `[&ref]`) usually fits within the SBO and does not trigger heap allocation. However, if a lambda captures a large amount of data (such as a `std::string` plus a few `int`s), exceeding the SBO threshold, construction will allocate on the heap.

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

On GCC, typical values are `std::function<void()>` at about 32 bytes, and `std::move_only_function<void()>` also at about 32 bytes. The two are similar in size because they use similar SBO strategies.

---

## Why OnceCallback Needs an Independent Status Enum

You may have noticed a detail—OnceCallback adds its own `Status` enum to track state, on top of `std::move_only_function`. Why not just use `std::move_only_function`'s null-checking mechanism?

The reason is that `std::move_only_function`'s null check cannot distinguish between three different states:

```cpp
enum class Status : uint8_t {
    kEmpty,     // 从未被赋值（默认构造）
    kValid,     // 持有有效的可调用对象
    kConsumed   // 已被 run() 调用过
};
```

`std::move_only_function`'s `operator bool()` can only distinguish between "empty" and "non-empty" states. But OnceCallback needs to know whether a callback "was never assigned a value" (kEmpty) or "once had a value but has already been invoked" (kConsumed). These two cases have completely different meanings during debugging—kEmpty means "you forgot to assign a callback," while kConsumed means "the callback was correctly invoked, and you should not use it again."

There is a more subtle issue: the state of `std::move_only_function` after a move is **unspecified**—the standard does not guarantee that the source object's `operator bool()` returns `false` after a move. Some implementations might still return `true`, even though the internal data is already invalid. If OnceCallback relied on `std::move_only_function`'s null check to determine state, it might get incorrect results after a move operation. The independent `Status` enum is entirely under our control—the move constructor explicitly sets the source object to `kEmpty`, leaving no ambiguity.

---

## Comparison with Chromium's BindState

Chromium does not use the standard library's type erasure facilities—it hand-writes a `BindState` system. Let's compare the core differences between the two approaches.

Chromium's `BindState<Functor, BoundArgs...>` is a heap-allocated object that stores the callable and all bound arguments. `OnceCallback` itself only holds a smart pointer (`scoped_refptr`) to `BindState`, making it just 8 bytes in size—one pointer. All state resides in the heap-allocated `BindState`, and the callback object itself is merely a "thin proxy."

Our approach replaces the entire `BindState` layer with `std::move_only_function`—it internally implements type erasure and SBO, saving us the work of hand-writing function pointer tables, SBO buffers, and move/destruction operations. The trade-off is that the object size bloats from 8 bytes to about 32 bytes (the size of `std::move_only_function` itself), and with the `Status` enum and an optional `CancelableToken` pointer, the entire `OnceCallback` comes to about 56–64 bytes.

| Metric | Chromium BindState | Our std::move_only_function |
|--------|-------------------|-------------------------------|
| Callback object size | 8 bytes (one pointer) | 56–64 bytes |
| Heap allocation | Always (new BindState) | Only when the lambda exceeds the SBO threshold |
| Move cost | Copying one pointer | Copying 32+ bytes |
| Implementation complexity | High (hand-written reference counting + function pointer table) | Low (reusing the standard library) |

For teaching purposes and most real-world scenarios, a 56–64 byte callback object is not a bottleneck at all. If your project truly demands extreme compactness, you can refer to Chromium's approach—we will explain the core ideas clearly in the later hands-on chapters.

---

## Summary

In this post, we clarified the origins and workings of `std::move_only_function`. It is the move-only version of `std::function` introduced in C++23, deleting copy operations to support move-only callables. It internally implements SBO to optimize storage for small objects. However, its post-move state is unspecified, and it can only distinguish between "empty" and "non-empty" states—this is why OnceCallback needs an independent three-state `Status` enum. Compared to Chromium's hand-written `BindState`, we traded an increase in object size for a significant boost in implementation simplicity.

In the next post, we will look at the final prerequisite for OnceCallback—C++23's deducing this (explicit object parameter), which is the core mechanism enabling the `run()` method to intercept lvalue/rvalue dispatch at compile time.

## References

- [cppreference: std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [P0288R9 - move_only_function proposal](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p0288r9.html)
- [cppreference: std::function](https://en.cppreference.com/w/cpp/utility/functional/function)
