# OnceCallback in Practice (Part 1): Motivation and Interface Design

## Introduction

Honestly, the most common pitfall I've hit in async programming is callbacks being invoked multiple times. The scenario is classic—you register a callback for a file I/O completion, expecting it to run exactly once and be done. But due to a logic slip somewhere, it gets triggered an extra time, the resources freed inside the callback get accessed a second time, and you promptly earn a segfault. A major characteristic of this kind of bug is that it's extremely hard to reproduce in tests, because normal async paths typically invoke the callback only once; the real trigger is some race condition or error retry path.

`std::function` can't help us here. It allows multiple invocations, allows copy propagation, and the callback object can end up everywhere. What we need is a mechanism that **constrains callback semantics at the type system level**—making the "callable only once" rule a compiler check, rather than a matter of programmer memory.

In this article, we start from the motivation, break down exactly what's wrong with `std::function`, and then design our target API. We'll start writing code in the next article.

> **Learning Objectives**
>
> - Understand the three major flaws of `std::function` in callback scenarios through a real async bug
> - Grasp the design philosophy of Chromium's OnceCallback: move-only + rvalue-qualified + single consumption
> - Design the complete public interface of OnceCallback

---

## Starting from a Bug

### Scenario: Async File Read

Suppose we are writing an async file read wrapper. The user calls `read_file_async(path, callback)`, and upon I/O completion, `callback` is triggered once, passing in the file contents.

```cpp
void read_file_async(const std::string& path,
                     std::function<void(std::string)> callback);

// 使用
void on_file_read(std::string content) {
    process(content);        // 处理内容
    release_resources();     // 释放相关资源
}

read_file_async("data.txt", on_file_read);
```

This looks fine. But if the I/O subsystem triggers a retry due to some error—the callback gets invoked twice. `release_resources()` executes twice, and the second time it accesses already-freed memory. Segfault. In a test environment, this retry path is never triggered; only under high-concurrency production conditions does this bug surface with extremely low probability.

### std::function Doesn't Help

Where does the problem lie? The type signature of `std::function<void(std::string)>` contains no information telling us "how many times this callback should be invoked." The type system provides no constraint, so we can only rely on runtime assertions—if you even have them—or on programmer discipline to guarantee correctness.

Even worse, the characteristics of `std::function` make this problem harder to spot. It is copyable, meaning the callback can be duplicated to multiple places. If multiple execution paths simultaneously hold copies of the same callback, race conditions are lurking within. Its `operator()` is `const`-qualified—invoking it doesn't change the state of the `std::function` object itself, so you cannot use the call interface to express the "invoke to consume" semantics.

---

## Three Major Flaws of std::function

Let's systematize the problem. `std::function`, as a general-purpose callable object wrapper, is a design success—but in the specific scenario of async callbacks, it has three fatal flaws.

### Flaw 1: Copyable

`std::function` inherently supports copying. When you copy a `std::function`, its internal type-erasure mechanism copies the stored callable object as well. In an async system, this means a single callback can be copied to any number of places—one copy in the task queue, one in the timer, one in the error handler—and each copy can be invoked independently.

If the callback captures move-only resources (like a `std::unique_ptr`), copying fails outright at compile time. If it captures raw pointers or references, multiple copies executing simultaneously will produce race conditions. The Chrome team's thinking is straightforward: since async task callbacks fundamentally shouldn't be copied, make them non-copyable at the type level.

### Flaw 2: Repeatedly Invocable

`std::function::operator()` places no constraints on the number of invocations. You can call the same `std::function` a thousand times, and it will happily execute every time. But in async callback scenarios, invoking a file-read-completion callback twice is a logic error—it might trigger double resource release, double state transitions, or double message sends. This kind of error is completely undetectable within the type system.

### Flaw 3: Inability to Express Consumption Semantics

In Chrome's task posting model, once a `PostTask(FROM_HERE, callback)` is invoked, the `callback` should no longer be used—its ownership has been transferred to the task system. The `operator()` of `std::function` is `const`-qualified; invoking it doesn't change the state of the `std::function` object itself, so you cannot use the call interface to express the "invoke to consume" semantics.

These three problems boil down to one point: the interface design of `std::function` cannot express the constraint that "this callback can only be invoked once, and becomes invalid afterward." Our OnceCallback is designed precisely to fill this semantic gap.

---

## Chromium's Answer: OnceCallback Design Philosophy

Chrome's callback system is built on a core principle: **message passing over locking, serialization over threading**. Under this principle, every callback posted to the task system is an independent, one-shot message. After posting, ownership of the callback transfers from the caller to the task system; after execution, the callback is destroyed. No sharing, no reuse, no ambiguity.

This philosophy is directly reflected in the type design of `OnceCallback`, with three key constraints:

**Move-only**: `OnceCallback` deletes copy construction and copy assignment, retaining only move operations. This guarantees at the type level that a callback has only one owner at any given time.

**Rvalue-qualified Run()**: `OnceCallback::Run()` can only be invoked via an rvalue reference. Invoking on an lvalue triggers a compile error. This serves as a syntactic reminder to the caller: "You are consuming this callback; don't use it afterward."

**Single consumption**: `Run()` internally destroys `BindState` through a reference-counting mechanism, making any subsequent access to the same object a safe no-op.

### Chromium Internal Architecture Overview

Chromium's callback system consists of three layers. The bottom layer is `BindStateBase`—a type-erased base class with reference counting, using function pointer members instead of virtual functions to implement polymorphism. The middle layer is `BindState<Functor, BoundArgs...>`—a templated concrete class that stores the actual callable object and bound arguments. The top layer is `OnceCallback<Signature>`—the type users directly interact with, essentially a smart pointer wrapper around `BindState`, with a size of only 8 bytes.

Our implementation will retain the layered approach of "outer interface + internal storage + type erasure," but we'll use `std::move_only_function` to replace Chromium's hand-rolled `BindState` + reference counting combo, and use deducing this to replace the dual overload + `!sizeof` hack.

---

## Designing the Target API

Let's nail down the target API first, then circle back to discuss each design decision. This is how engineers work—figure out "what I want" first, then "how to do it."

### Construction and Invocation

```cpp
#include "once_callback/once_callback.hpp"

using namespace tamcpp::chrome;

// 从 lambda 构造
auto cb = OnceCallback<int(int, int)>([](int a, int b) {
    return a + b;
});

// 调用：必须通过右值
int result = std::move(cb).run(3, 4);  // result == 7

// 调用后 cb 被消费
// std::move(cb).run(1, 2);  // 运行时断言失败
```

### Argument Binding

```cpp
// bind_once：预绑定部分参数，返回一个新的 OnceCallback
auto bound = bind_once<int(int)>(
    [](int x, int y, int z) { return x + y + z; },
    10, 20  // 预绑定前两个参数
);

int r = std::move(bound).run(30);  // r == 60
```

### Cancellation Checks

```cpp
auto cb = OnceCallback<void(int)>([](int x) { /* ... */ });

// 检查回调是否仍然有效
if (!cb.is_cancelled()) {
    std::move(cb).run(42);
}

// maybe_valid：乐观检查
if (cb.maybe_valid()) {
    std::move(cb).run(42);
}
```

### Chained Composition

```cpp
auto pipeline = OnceCallback<int(int, int)>([](int a, int b) {
    return a + b;
}).then([](int sum) {
    return sum * 2;
});

int final_result = std::move(pipeline).run(3, 4);
// final_result == 14，因为 (3+4)*2 = 14
```

---

## Interface Design Decision Analysis

### Why run() Instead of operator()

Chromium uses `Run()` (Google style requires capitalization at the start). We use `run()` to conform to snake_case naming conventions. The deeper reason is semantic distinction—`operator()` is too generic; any callable object has `operator()`. `run()` explicitly expresses the "execute task" semantics, making it immediately obvious during code review that you are consuming a OnceCallback, not just calling a regular function.

### Why run() Must Be Rvalue-Qualified

This is the most critical point in the entire design. We use deducing this to let the compiler intercept lvalue invocations for us—if you write `cb.run(args)` instead of `std::move(cb).run(args)`, the compiler errors out immediately, with an error message that clearly tells you what to do. This mechanism was covered in detail in the prerequisite knowledge (Part 6).

### Why Distinguish is_cancelled() and maybe_valid()

The difference lies in the strength of the safety guarantee. `is_cancelled()` provides a deterministic answer—it can only be called on the sequence the callback is bound to, guaranteeing an accurate result. `maybe_valid()` provides an optimistic estimate—it can be called from any thread, but the result might be stale. In Chromium's full implementation, this distinction is related to thread safety guarantees. Our simplified version temporarily makes the two semantically identical, but we reserve the interface for future extension.

### Why then() Consumes *this

The semantics of `then()` are "pass the execution result of the current callback to the next callback." This requires the current callback to be fully captured inside the new callback returned by `then()`. If `then()` doesn't consume `*this`, the same callback would exist in two places simultaneously—violating the move-only semantic constraint. Therefore, `then()` is declared as an rvalue-qualified member function, and after invocation, the original callback object enters a consumed state.

---

## Environment Setup

Before we start writing code, let's confirm the toolchain. OnceCallback depends on `std::move_only_function` and deducing this, both of which are C++23 features.

### Compiler Requirements

GCC 13+ or Clang 17+ fully supports the above features. Add `-std=c++23` when compiling.

### Verification Code

```cpp
#include <functional>

// 验证 std::move_only_function 可用
static_assert(__cpp_lib_move_only_function >= 202110L);

// 验证 deducing this 可用
struct Check {
    void test(this auto&& self) {}
};

int main() {
    Check c;
    c.test();
    return 0;
}
```

If this code compiles successfully, your environment is good to go.

### Minimal CMake Configuration

```cmake
cmake_minimum_required(VERSION 3.20)
project(once_callback_demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(once_callback INTERFACE)
target_include_directories(once_callback INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
```

---

## Summary

In this article, starting from the motivation, we clarified three things. `std::function` has three major flaws in async callback scenarios—copyable, repeatedly invocable, and unable to express consumption semantics—the root cause being that the type system cannot constrain "callable only once." Chromium's OnceCallback fills this semantic gap through move-only + rvalue-qualified Run() + single consumption. We designed a target API covering four core features: construction and invocation, argument binding (`bind_once`), cancellation checks (`is_cancelled`/`maybe_valid`), and chained composition (`then()`).

In the next article, we'll start building the core skeleton—from template partial specialization to tri-state management, putting the class skeleton of OnceCallback together.

## References

- [Chromium Callback Documentation](https://chromium.googlesource.com/chromium/src/+/main/docs/callback.md)
- [cppreference: std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [P0847R7 - Deducing this Proposal](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0847r7.html)
