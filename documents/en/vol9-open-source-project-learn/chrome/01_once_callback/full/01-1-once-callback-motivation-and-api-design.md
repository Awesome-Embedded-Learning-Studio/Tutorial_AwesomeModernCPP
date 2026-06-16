---
chapter: 1
cpp_standard:
- 23
description: Starting from a real asynchronous callback bug, we analyze the three
  major shortcomings of `std::function` in asynchronous scenarios, and design the
  complete target API for `OnceCallback`.
difficulty: beginner
order: 1
platform: host
prerequisites:
- OnceCallback 前置知识（一）：函数类型与模板偏特化
- OnceCallback 前置知识（五）：std::move_only_function
- OnceCallback 前置知识（六）：Deducing this
reading_time_minutes: 10
related:
- OnceCallback 实战（二）：核心骨架搭建
- OnceCallback 前置知识速查：C++11/14/17 核心特性回顾
tags:
- host
- cpp-modern
- beginner
- 回调机制
- 函数对象
title: 'OnceCallback in Practice (Part 1): Motivation and Interface Design'
translation:
  source: documents/vol9-open-source-project-learn/chrome/01_once_callback/full/01-1-once-callback-motivation-and-api-design.md
  source_hash: 97ef56eda9b4d5cd69a586a7360b4c24a83c826da6e674bdd24307e5d313c908
  translated_at: '2026-06-16T04:13:11.873850+00:00'
  engine: anthropic
  token_count: 1724
---
# OnceCallback in Action (Part 1): Motivation and Interface Design

## Introduction

Honestly, the most common pitfall I've encountered while doing asynchronous programming is callbacks being invoked multiple times. The scenario is classic—you register a callback for file I/O completion, expecting it to run once and be done with it. But due to a logic slip-up somewhere, it gets triggered one extra time. The resources released in the callback are accessed a second time, and boom—you get a segmentation fault. A major characteristic of this type of bug is that it is very hard to reproduce in tests, because normal asynchronous paths usually only trigger the callback once; the real trigger is often some race condition or error retry path.

`std::function` can't help us here. It allows multiple invocations, allows copy propagation, and callback objects can end up flying everywhere. We need a mechanism that **constrains callback semantics at the type system level**—making the "invoke only once" rule a compiler check, not a test of the programmer's memory.

In this article, we will start from the motivation, analyze exactly what is wrong with `std::function`, and then design our target API. We will start writing code in the next article.

> **Learning Objectives**
>
> - Understand the three major flaws of `std::function` in callback scenarios through a real-world asynchronous bug.
> - Grasp the design philosophy of Chromium's OnceCallback: move-only + rvalue-qualified + single-shot consumption.
> - Design the complete public interface for OnceCallback.

---

## Starting with a Bug

### Scenario: Asynchronous File Reading

Suppose we are writing an asynchronous file reading wrapper. The user calls `async_read`, and when I/O completes, `on_complete` is triggered once, passing in the file content.

```cpp
void async_read(const std::string& path, std::function<void(std::string)> on_complete);
```

Looks fine. But if the I/O system triggers a retry due to some error—the callback gets invoked twice. `on_complete` executes twice, and the second time it accesses already freed memory. Segmentation fault. In a test environment, this retry path might never be triggered; only in a high-concurrency production environment does this bug appear with a very low probability.

### std::function Doesn't Help Us

Where is the problem? The type signature of `std::function<void(std::string)>` contains no information telling us "how many times this callback should be called". The type system provides no constraint; we can only rely on runtime assertions—if you have them—or programmer discipline to guarantee it.

Even worse, `std::function`'s characteristics make this problem harder to detect. It is copyable, meaning the callback can be copied to multiple places. If multiple execution paths hold copies of the same callback simultaneously, race conditions are lurking. Its `operator()` is `const`-qualified—calling it does not change the state of the `std::function` object itself—so you cannot express the "invoke is consume" semantic through the calling interface.

---

## Three Major Flaws of std::function

Let's systematize the problem. `std::function`, as a general-purpose callable object container, is successful in its design—but in the specific scenario of asynchronous callbacks, it has three fatal flaws.

### Flaw 1: Copyable

`std::function` natively supports copying. When you copy a `std::function`, its internal type-erasure mechanism copies the stored callable object as well. In an asynchronous system, this means a callback can be copied to any number of places—one in the task queue, one in the timer, one in the error handler—and each copy can be invoked independently.

If the callback captures move-only resources (like `std::unique_ptr`), copying fails directly at compile time. If it captures raw pointers or references, multiple copies executing simultaneously will produce races. The Chrome team's approach is straightforward: since asynchronous task callbacks fundamentally shouldn't be copied, make them uncopyable at the type level.

### Flaw 2: Repeatedly Callable

`std::function` has no constraint on the number of calls. You can invoke the same `std::function` a thousand times, and it will run without complaint. But in an asynchronous callback scenario, invoking a file-read completion callback twice is a logical error—it might trigger double resource release, double state transitions, or double message sending. This error is completely undetectable by the type system.

### Flaw 3: Unable to Express Consumption Semantics

In Chrome's task posting model, once a `OnceCallback` is called, it should not be used again—its ownership has been transferred to the task system. `std::function`'s `operator()` is `const`-qualified; calling it does not change the state of the `std::function` object itself, so you cannot express the "invoke is consume" semantic through the calling interface.

These three problems boil down to one point: `std::function`'s interface design cannot express the constraint "this callback can only be invoked once, and becomes invalid after invocation". Our OnceCallback is designed to fill this semantic gap.

---

## Chromium's Answer: OnceCallback Design Philosophy

Chrome's callback system is built on a core principle: **message passing over locks, serialization over threads**. Under this principle, every callback posted to the task system is an independent, one-shot message. After posting, ownership of the callback transfers from the caller to the task system; after execution, the callback is destroyed. No sharing, no reuse, no ambiguity.

This philosophy is directly reflected in `base::OnceCallback`'s type design, with three key constraints:

**Move-only**: `OnceCallback` deletes copy construction and copy assignment, retaining only move operations. This guarantees at the type level that a callback has only one owner at any moment.

**Rvalue-qualified Run()**: `OnceCallback` can only be invoked via an rvalue reference. Invoking via an lvalue triggers a compile error. This syntactically reminds the caller: "You are consuming this callback, don't use it again."

**Single-shot consumption**: Internally, `OnceCallback` uses a reference counting mechanism to destroy the stored object after the first call, making any subsequent access to the same object a safe no-op.

### Chromium Internal Architecture Overview

Chromium's callback system consists of three layers. The bottom layer is `base::InternalCallbackBase`—a type-erased base class with reference counting, using function pointer members instead of virtual functions for polymorphism. The middle layer is `base::InternalCallback`—a templated concrete class storing the actual callable object and bound arguments. The top layer is `base::OnceCallback`—the type users directly interact with, essentially a smart pointer wrapper to `base::InternalCallbackBase`, only 8 bytes in size.

Our implementation will retain the layered approach of "outer interface + internal storage + type erasure", but we will use `std::move_only_function` to replace Chromium's hand-rolled `base::InternalCallbackBase` + reference counting combo, and use deducing this to replace the double overload + `std::enable_if` hack.

---

## Designing the Target API

Let's define the target API first, then discuss each design decision. This is how engineers work—figure out "what I want" first, then "how to do it".

### Construction and Invocation

```cpp
// Construction from lambdas/function pointers
OnceCallback<void()> cb = [] { /* ... */ };

// Invocation (must be rvalue)
std::move(cb).run();
```

### Argument Binding

```cpp
// Binding arguments (Currying)
auto add = [](int a, int b) { return a + b; };
auto add_five = OnceCallback<int(int)>(add).bind(5);
int result = std::move(add_five).run(10); // Returns 15
```

### Cancellation Checks

```cpp
// Check if cancelled
if (cb.is_cancelled()) { /* ... */ }

// Check validity (optimistic)
if (cb.maybe_valid()) { /* ... */ }
```

### Chaining

```cpp
// Chaining (then)
auto task = OnceCallback<void()>([] { /* task A */ })
    .then([] { /* task B */ });
std::move(task).run();
```

---

## Interface Design Decision Analysis

### Why use run() instead of operator()?

Chromium uses `Run()` (Google style requires capitalization). We use `run()` to conform to snake_case naming conventions. The deeper reason is semantic distinction—`operator()` is too generic, any callable object has it; `run()` explicitly expresses the "execute task" semantic. During code review, you can see at a glance that this is consuming a OnceCallback, not just calling a normal function.

### Why must run() be invoked via an rvalue?

This is the most critical point in the entire design. We use deducing this to let the compiler intercept lvalue calls for us—if you write `cb.run()` instead of `std::move(cb).run()`, the compiler will directly error out, and the error message explicitly tells you what to do. This mechanism was explained in detail in the Prerequisites (Part 6).

### Why distinguish between is_cancelled() and maybe_valid()?

The difference lies in the strength of the safety guarantee. `is_cancelled()` provides a definitive answer—can only be called on the sequence where the callback is bound, guaranteeing an accurate result. `maybe_valid()` provides an optimistic estimate—can be called from any thread, but the result might be stale. In Chromium's full implementation, this distinction relates to thread safety guarantees. Our simplified version temporarily makes the semantics of both identical, but retains the interface for future expansion.

### Why does then() consume *this?

The semantic of `then()` is "pass the execution result of the current callback to the next callback". This requires the current callback to be fully captured in the new callback returned by `then()`. If `then()` does not consume `*this`, the same callback would exist in two places simultaneously—violating the move-only semantic constraint. Therefore, `then()` is declared as an rvalue-qualified member function; after invocation, the original callback object enters a consumed state.

---

## Environment Setup

Before writing code, let's confirm the toolchain. OnceCallback relies on `std::move_only_function` and deducing this, both C++23 features.

### Compiler Requirements

GCC 13+ or Clang 17+ fully supports the above features. Add `-std=c++23` when compiling.

### Verification Code

```cpp
// test_env.cpp
#include <functional>

int main() {
    std::move_only_function<void()> f = [] {};
    std::move(f)(); // Compile check
}
```

If this code compiles, the environment is set.

### Minimal CMake Configuration

```cmake
cmake_minimum_required(VERSION 3.20)
project(OnceCallback LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(test_env test_env.cpp)
```

---

## Summary

In this article, starting from motivation, we clarified three things. `std::function` has three major flaws in asynchronous callback scenarios—copyable, repeatedly callable, unable to express consumption semantics—the root cause is that the type system cannot constrain "invoke only once". Chromium's OnceCallback fills this semantic gap through move-only + rvalue-qualified Run() + single-shot consumption. We designed a set of target APIs, covering four core features: construction and invocation, argument binding (`bind`), cancellation checks (`is_cancelled`/`maybe_valid`), and chaining (`then`).

In the next article, we will start building the core skeleton—from template specialization to state management, we will set up the class skeleton for OnceCallback.

## References

- [Chromium Callback Documentation](https://chromium.googlesource.com/chromium/src/+/main/docs/callback.md)
- [cppreference: std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [P0847R7 - Deducing this Proposal](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0847r7.html)
