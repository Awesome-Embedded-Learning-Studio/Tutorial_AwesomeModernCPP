---
title: 'once_callback Design Guide (Part 1): Motivation and Interface Design'
description: Starting from Chromium's OnceCallback, designing a C++23 move-only, single-use
  callback component—Part 1 focuses on motivation analysis and API design
chapter: 1
order: 1
tags:
- host
- cpp-modern
- advanced
- 回调机制
- 函数对象
difficulty: advanced
platform: host
cpp_standard:
- 23
reading_time_minutes: 20
prerequisites:
- std::function、std::invoke 与可调用对象
- 移动语义与完美转发
related:
- OnceCallback 与 RepeatingCallback
- bind_once / bind_repeating 与参数绑定
---
# once_callback Design Guide (Part 1): Motivation and Interface Design

## Introduction

Honestly, the most common pitfall I've encountered in async programming is callbacks being invoked multiple times. The scenario is classic: you register a callback for file I/O completion, expecting it to run exactly once, but a slip of logic triggers it an extra time. The resources freed inside the callback get accessed a second time, and you are promptly rewarded with a segfault. A major characteristic of this kind of bug is that it is extremely hard to reproduce in tests, because the normal async path typically invokes the callback only once; the real trigger is some race condition or error retry path.

`std::function` can't help us here. It allows multiple invocations, permits copy propagation, and callback objects can end up scattered everywhere. In Volume 2, we already dissected the internal mechanisms of `std::function` (type erasure + SBO) and its `LightCallback` simplified implementation—that version solved the type erasure overhead problem, but it completely missed the semantic question of "how many times should a callback be invoked."

When the Chromium team designed `base::OnceCallback`, they provided a beautifully elegant answer: **let the callback's type system itself constrain the invocation semantics**. `OnceCallback` is a move-only type, and its `Run()` method can only be called through an rvalue reference (`std::move(cb).Run()`). After being invoked once, the callback object is consumed, and any subsequent invocation is either a no-op or triggers an assertion failure. This design has been thoroughly validated through the tens of billions of task dispatches the Chrome browser handles every day.

The goal of this series is not to blindly copy Chromium's implementation (which is extremely complex, involving hand-rolled reference counting, `TRIVIAL_ABI` annotations, and function pointer dispatch tables). Instead, we will leverage new C++23 features—particularly `std::move_only_function` and deducing this—to implement a `OnceCallback` component that retains the essence of Chromium's design while keeping the codebase manageable.

> **Learning Objectives**
>
> - Understand why "move-only + single-use consumption" is the correct semantic constraint for callbacks
> - Design the complete public interface of `OnceCallback<R(Args...)>`
> - Analyze the internal architecture of Chromium's `OnceCallback` and understand the reasoning behind each design decision

---

## Our Problem: Three Major Flaws of `std::function` in Async Scenarios

Before we start designing, let's clearly break down the problem. As a general-purpose callable object wrapper, `std::function` is a design success—but in the specific scenario of async callbacks, it has three issues that will send your blood pressure through the roof.

**First, it is copyable.** `std::function` inherently supports copying, meaning a single callback can be duplicated to arbitrary locations. In an async system, this equates to allowing multiple execution paths to simultaneously hold copies of the same callback. If the callback captures move-only resources (like a `std::unique_ptr`), copying fails at compile time; if it captures raw pointers or references, multiple copies executing simultaneously will introduce data races. The Chrome team's approach is very straightforward: since async task callbacks fundamentally should not be copied, make them non-copyable at the type level.

**Second, it is repeatedly invocable.** `std::function::operator()` places no constraints on the number of invocations. You can call the same `std::function` a thousand times, and it will happily execute every time. But in async callback scenarios, invoking a file-read-completion callback twice is a logic error—it might trigger double resource releases, double state transitions, or double message sends. This kind of error is completely undetectable by the type system, relying solely on runtime assertions (if they exist) or—more commonly—being discovered at the bug crime scene.

**Third, it cannot express consumption semantics.** In Chrome's task dispatch model, once a `PostTask(FROM_HERE, callback)` is invoked, the `callback` should no longer be used—its ownership has been transferred to the task system. `std::function`'s `operator()` is `const`-qualified, so invoking it does not alter the state of the `std::function` object itself, meaning you cannot use the invocation interface to express the "invoke-to-consume" semantic.

These three issues boil down to a single point: `std::function`'s interface design cannot express the constraint that "this callback can only be invoked once, and becomes invalid afterward." Chrome's `OnceCallback` was designed precisely to fill this semantic gap.

---

## Chromium's Answer: The `OnceCallback` Design Philosophy

Chrome's callback system is built on a core principle: **message passing over locks, serialization over threads**. Under this principle, every callback posted to the task system (called a task in Chrome) is an independent, one-time message. Once posted, the callback's ownership transfers from the caller to the task system; once executed, the callback is destroyed. No sharing, no reuse, no ambiguity.

This philosophy is directly reflected in `OnceCallback`'s type design:

- **Move-only**: `OnceCallback` deletes copy construction and copy assignment, retaining only move operations. This guarantees at the type level that the callback has exactly one owner at any given time.
- **Rvalue-qualified `Run()`**: `OnceCallback::Run()` can only be invoked through an rvalue reference (`std::move(cb).Run(args...)`). Lvalue invocation triggers `static_assert`, producing a clear compile-time error. This serves as a syntactic reminder to the caller: "You are consuming this callback; don't use it afterward."
- **Single-use consumption**: Internally, `Run()` uses a reference counting mechanism to destroy `BindState`, making any subsequent access to the same object a safe no-op.

Chrome actually also has `RepeatingCallback`—a copyable, repeatedly invocable version. The two callback classes share the same `BindState` internal implementation, differing only in the value category qualification of `Run()` and the ownership semantics of `BindState`. This design allows the same binding infrastructure to simultaneously serve two fundamentally different usage patterns: "one-time tasks" and "repeating listeners."

### Overview of Chromium's Internal Implementation

We don't need to dive into every line of Chromium's source code, but we do need to understand its core architecture, because our `OnceCallback` will borrow the same layered approach, simply using C++23 standard facilities to simplify the implementation.

Chromium's callback system consists of three layers, from bottom to top:

**Bottom layer: `BindStateBase`**—the type-erased base class. It carries a reference count, but interestingly, it **does not use virtual functions**. Instead, it has three function pointer members: `polymorphic_invoke_` (responsible for invocation), `destructor_` (responsible for destruction), and `query_cancellation_traits_` (responsible for cancellation queries). The Chrome team chose function pointers over virtual functions to reduce binary bloat. Virtual functions generate a separate vtable for each template instantiation; if a project has 100 different `BindState<Functor, BoundArgs...>` instantiations, it will have 100 vtables. The function pointer approach can reuse the same static functions, differing only in the pointer values, without generating additional code sections.

**Middle layer: `BindState<Functor, BoundArgs...>`**—the templated concrete class, inheriting from `BindStateBase`. It stores the actual callable object (`Functor`) and the arguments bound via `BindOnce` (`BoundArgs...`). You can think of it as a "box that holds everything": inside the box are your lambda, the bound arguments, and the function pointers required by the base class. Instances of this class have their lifetimes managed through `scoped_refptr` (Chromium's own intrusive reference-counted smart pointer)—`OnceCallback` releases the reference on `Run()`, while `RepeatingCallback` retains the reference on each `Run()`.

**Top layer: `OnceCallback<Signature>` and `RepeatingCallback<Signature>`**—the types users directly interact with. They are essentially thin wrappers around `BindStateHolder`, and `BindStateHolder` is simply a `scoped_refptr<BindStateBase>` with a `TRIVIAL_ABI` annotation. `TRIVIAL_ABI` is a Clang extension attribute that tells the compiler "this type can be passed in a register just like an int," making the actual size of a `OnceCallback` only one pointer (8 bytes), where move operations are merely copying a pointer—extremely lightweight.

The relationship between these three layers can be summarized in one sentence: **the top-layer callback object is just a pointer to the middle-layer box, and the box holds the function pointers required by the bottom layer and the actual data**. The `OnceCallback` we design next will retain this "outer interface + middle storage + type erasure" layered approach, but we will use `std::move_only_function` to replace Chromium's hand-rolled `BindState` + `scoped_refptr` combination, and use deducing this to replace the `const&` overload + `static_assert` hack.

---

## Environment Setup

First, let's confirm our toolchain. `OnceCallback` depends on the following C++23 features:

- **`std::move_only_function`** (`<functional>`): A move-only type-erased callable wrapper introduced in C++23, serving as our core building block
- **Deducing this** (explicit object parameter `this auto&& self`): A C++23 feature that allows deducing the value category of `this` in member functions
- **`if consteval`**: Compile-time conditional logic (may be used in some implementations)

In terms of compiler requirements, GCC 12+ or Clang 16+ fully supports the above features. Simply add `-std=c++23` when compiling. You can use the following code to quickly verify your environment:

```cpp
#include <functional>

// 验证 std::move_only_function 可用
static_assert(__cpp_lib_move_only_function >= 202110L);

// 验证 deducing this 可用（编译通过即说明支持）
struct Check {
    void test(this auto&& self) {}
};

int main() {
    Check c;
    c.test();
    return 0;
}
```

If this code compiles successfully, your environment is good to go. That said, as of the time of writing, some compilers' `std::move_only_function` implementations still have bugs (for example, early versions of GCC 12 fail to compile in certain SFINAE scenarios), so I recommend using the latest stable versions of GCC 13+ or Clang 17+.

### Prerequisites

We assume readers are already familiar with the following topics (covered in the corresponding Volume 2 articles):

- **Move semantics and perfect forwarding**: The core of `OnceCallback` is move-only; if you aren't familiar with the principles of `std::move` and `std::forward`, the implementation process will be quite painful. Corresponding article: Volume 2 ch00 Move Semantics series.
- **Type erasure and SBO in `std::function`**: We build directly on top of `std::move_only_function`, so you need to understand the basic principles of type erasure and what small buffer optimization is and why it matters. Corresponding article: Volume 2 ch03 `std::function` and Callable Objects.
- **`std::invoke` and the uniform call protocol**: Internally, `bind_once` uses `std::invoke` to uniformly handle different types of callable objects, such as function pointers, member function pointers, and functors. Corresponding article: Ibid.
- **Variadic templates and parameter pack expansion**: Template specialization of `OnceCallback<R(Args...)>` and argument binding in `bind_once` both require familiarity with parameter pack syntax. Corresponding articles: Volume 2 ch00 Perfect Forwarding, Volume 4 Template Basics.
- **`std::invoke` and the uniform call protocol**: Internally, `bind_once` uses `std::invoke` to uniformly handle different types of callable objects, such as function pointers, member function pointers, and functors. Corresponding article: Ibid.
- **Variadic templates and parameter pack expansion**: Template specialization of `OnceCallback<R(Args...)>` and argument binding in `bind_once` both require familiarity with parameter pack syntax. Corresponding articles: Volume 2 ch00 Perfect Forwarding, Volume 4 Template Basics.

---

## Designing the Interface: What API Do We Want?

Let's nail down the target API first, and then circle back to discuss each design decision. This is how engineers work—first figure out "what I want," then figure out "how to do it."

### Core Usage

```cpp
#include "once_callback/once_callback.hpp"

// 1. 构造：从 lambda 创建
using namespace tamcpp::chrome;
auto cb = OnceCallback<int(int, int)>([](int a, int b) {
    return a + b;
});

// 2. 调用：必须通过右值（std::move）
int result = std::move(cb).run(3, 4);  // result == 7

// 3. 调用后，cb 被消费
// std::move(cb).run(1, 2);  // 运行时断言失败：callback already consumed
```

### Argument Binding

```cpp
// bind_once：预绑定部分参数，返回一个 OnceCallback
using namespace tamcpp::chrome;
auto bound = bind_once<int(int)>(
    [](int x, int y, int z) { return x + y + z; },
    10, 20  // 预绑定前两个参数
);

int r = std::move(bound).run(30);  // r == 60
```

### Cancellation Checks

```cpp
using namespace tamcpp::chrome;
auto cb = OnceCallback<void(int)>([](int x) { /* ... */ });

// 检查回调是否仍然有效
if (!cb.is_cancelled()) {
    std::move(cb).run(42);
}

// maybe_valid：乐观检查，适用于跨序列场景
if (cb.maybe_valid()) {
    // "可能"有效，不保证
    std::move(cb).run(42);
}
```

### Chained Composition

```cpp
using namespace tamcpp::chrome;
// then()：将当前回调的返回值传给下一个回调
auto pipeline = OnceCallback<int(int, int)>([](int a, int b) {
    return a + b;
}).then([](int sum) {
    return sum * 2;
});

int final_result = std::move(pipeline).run(3, 4);
// final_result == 14  (3+4)*2
```

### Interface Design Decision Analysis

Now let's discuss the design decisions behind these APIs one by one.

**Why `run()` instead of `operator()`?**

Chromium uses `Run()` (Google C++ style requires capitalized names). We use `run()` to conform to snake_case naming conventions. But the deeper reason is semantic distinction: `operator()` is too generic; any callable object has a `operator()`. `run()` explicitly expresses the "execute task" semantics, making it immediately obvious during code review that you are consuming a `OnceCallback`, rather than invoking an ordinary callable object.

**Why must `run()` be called through an rvalue?**

This is the most critical point of the entire design. We need a mechanism that causes `cb.run(args)` (lvalue invocation) to fail at compile time, while `std::move(cb).run(args)` (rvalue invocation) compiles successfully. Chromium's implementation achieves this through two overloads: one `Run() &&` is the actual execution version, and a `Run() const&` internally contains a `static_assert(!sizeof(*this))` to produce a compile-time error. This hack works but is ugly.

We can do this more elegantly using C++23's **deducing this** (explicit object parameter). Simply put, deducing this allows us to explicitly write `this` as a template parameter in a member function, and the compiler deduces this parameter's type based on whether the object is an lvalue or rvalue at the call site. Leveraging this feature, `run(this auto&& self, Args... args)` distinguishes between lvalue and rvalue invocations by deducing the value category of `self`, intercepting illegal usage at compile time:

```cpp
template<typename Self>
auto run(this Self&& self, FuncArgs&&... args) -> ReturnType {
    static_assert(!std::is_lvalue_reference_v<Self>,
        "OnceCallback::run() must be called on an rvalue. "
        "Use std::move(cb).run(...) instead.");
    // ... 实际调用逻辑
}
```

When the caller writes `cb.run(args)`, `Self` is deduced as `OnceCallback&` (an lvalue reference), `static_assert` triggers, and the error message directly tells the caller what to do. When writing `std::move(cb).run(args)`, `Self` is deduced as `OnceCallback` (an rvalue), and compilation succeeds. We will dive into the specific working mechanism of deducing this and a detailed comparison with Chromium's approach in the next implementation article.

**Why distinguish between `is_cancelled()` and `maybe_valid()`?**

This design comes directly from Chromium's `CancellationQueryMode`. The difference lies in the strength of the safety guarantees. `is_cancelled()` provides a deterministic answer—it can only be called on the sequence to which the callback is bound, guaranteeing an accurate result. `maybe_valid()` provides an optimistic estimate—it can be called from any thread, but the result may be stale. In practice, `is_cancelled()` is used for "checking whether it still makes sense before posting," while `maybe_valid()` is used for the optimization path of "quickly checking cross-thread whether it's worth posting."

In our simplified implementation, both methods query through `CancelableToken`—`is_cancelled()` checks whether the state is valid and whether the token is still valid, while `maybe_valid()` is simply a thin wrapper around `!is_cancelled()`. If more fine-grained thread-safety semantics are needed later, we can differentiate between these two methods.

**Why does `then()` consume `*this`?**

The semantics of `then()` are "pass the execution result of the current callback to the next callback." This requires the current callback to be fully captured inside the new callback returned by `then()`. If `then()` did not consume `*this`, it would lead to the same callback existing in two places simultaneously—the original location and the new callback returned by `then()`—which violates the move-only semantic constraint. Therefore, `then()` is declared as an rvalue-qualified member function (`then(...) &&`), and after invocation, the original callback object enters a consumed state.

---

## Internal Mechanisms: The Two-Layer Architecture of Type Erasure

With the interface designed, let's look at how to organize the internals. Chromium uses the combination of `BindStateBase` + `scoped_refptr` + function pointer tables to implement type erasure, which works great but results in an astonishing amount of code. Our strategy is to let `std::move_only_function` handle the dirty work of type erasure and small object optimization, allowing us to focus on the interesting parts: consumption semantics, argument binding, and chained composition.

### Why Choose `std::move_only_function`

`std::move_only_function<R(Args...)>` was introduced in C++23, positioned as the "move-only version of `std::function`". It internally implements type erasure and SBO, behaving similarly to `std::function` but with copy operations deleted.

You may have already noticed the syntax `OnceCallback<R(Args...)>`—`R(Args...)` looks like a function declaration, but in the context of a template parameter, it is a **function type**. `int(int, int)` describes "a function that takes two int parameters and returns an int," and it is a valid C++ type. We deconstruct this type through template partial specialization—a technique we will explain in detail in the next article.

Using `std::move_only_function` for internal storage has several benefits. It saves us from hand-writing type erasure—recall that in Volume 2's `LightCallback`, we spent an entire chapter hand-writing function pointer tables, SBO buffers, and move/destruction operations, whereas `std::move_only_function` encapsulates all of this, ready to use out of the box. It also natively supports move-only callable objects—if our callback captures a `std::unique_ptr`, `std::function` will fail to compile directly due to its copy semantics requirements, but `std::move_only_function` has no such issue. Furthermore, its SBO implementation has been carefully tuned by standard library authors, eliminating the need for heap allocation in the vast majority of cases—for lambdas capturing a small number of arguments, the performance is more than adequate.

### Three-State Management

After introducing `std::move_only_function`, there is a design problem to solve: how do we distinguish between a "null callback" and a "consumed callback"?

A `std::move_only_function` can inherently be empty (default-constructed or constructed from `nullptr`), but "empty" and "already consumed by `run()`" are two different states. A null callback means "it was never assigned," and invoking it should trigger a clear error ("callback is null"). A consumed callback means "it once held a value, but has already been invoked," and invoking it should also trigger an error ("callback already consumed"), but the error message is different, which is very helpful for debugging.

Therefore, our internal state needs three states:

```cpp
enum class Status : uint8_t {
    kEmpty,     // 默认构造，从未被赋值
    kValid,     // 持有有效的可调用对象
    kConsumed   // 已被 run() 消费
};
```

Combined with `std::move_only_function`, our internal storage structure looks roughly like this:

```cpp
template<typename ReturnType, typename... FuncArgs>
class OnceCallback<ReturnType(FuncArgs...)> {
    std::move_only_function<FuncSig> func_;
    Status status_ = Status::kEmpty;

    // 取消令牌（可选）
    std::shared_ptr<CancelableToken> token_;
};
```

Upon move construction, `func_` and `status_` are moved together, and the source object's state is set to `kEmpty`. When `run()` executes, it first checks whether `status_` is `kValid`, and after execution, it nullifies `func_` and sets `status_` to `kConsumed`. This way, during debugging, we can provide precise error messages based on the value of `status_`.

### Trade-offs Compared to the Original Chromium Version

By using `std::move_only_function` for the underlying storage, we gain a concise implementation, but we also sacrifice some things. Chromium's `OnceCallback` is only one pointer in size (8 bytes), thanks to the `TRIVIAL_ABI` annotation and the reference-counted `BindState`—the callback object itself is merely a pointer to a heap-allocated `BindState`. Our `OnceCallback` wraps a `std::move_only_function` (typically 32 bytes) plus a `Status` enum and an optional `CancelableToken` pointer (16 bytes), totaling around 56-64 bytes.

Another difference is reference counting. Chromium's `BindState` is reference-counted, allowing multiple callbacks to share the same bound state (which is necessary for the copy semantics of `RepeatingCallback`). In our implementation, `std::move_only_function` itself has exclusive ownership and does not support sharing. For the move-only semantics of `OnceCallback`, this is not a problem, but when implementing `RepeatingCallback` later on, we will need to rethink this design.

These trade-offs are reasonable—we exchanged size and reference counting flexibility for a significantly reduced implementation complexity. In practice, a 56-64 byte callback object is not a bottleneck in the vast majority of scenarios, and the clean code structure makes maintenance and extension much cheaper.

---

## Summary

In this article, we established the design foundation for `once_callback`. The key takeaways are:

- `std::function` has three major flaws in async callback scenarios: it is copyable, it is repeatedly invocable, and it cannot express consumption semantics
- Chromium's `OnceCallback` constrains callback semantics through move-only + rvalue-qualified `Run()` + single-use consumption
- Our `OnceCallback` uses `std::move_only_function` for underlying type erasure and deducing this to implement the rvalue-qualified `run()`
- Internally, we use three-state management (`kEmpty` / `kValid` / `kConsumed`) to distinguish between null callbacks and consumed callbacks

In the next article, we will enter the implementation phase: starting from the core skeleton `run()`, and progressively adding `bind_once`, cancellation checks, and `then()` chained composition.

## References

- [Chromium Callback Documentation](https://chromium.googlesource.com/chromium/src/+/main/docs/callback.md)
- [Chromium callback.h Source Code](https://chromium.googlesource.com/chromium/src/+/HEAD/base/functional/callback.h)
- [cppreference: std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [P0847R7 - Deducing this Proposal](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0847r7.html)
