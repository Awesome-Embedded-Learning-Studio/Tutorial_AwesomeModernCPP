---
chapter: 1
cpp_standard:
- 23
description: Starting from Chromium's OnceCallback, we design a C++23 move-only, single-shot
  callback component — Part one focuses on motivation analysis and API design
difficulty: advanced
order: 1
platform: host
prerequisites:
- std::function、std::invoke 与可调用对象
- 移动语义与完美转发
reading_time_minutes: 19
related:
- OnceCallback 与 RepeatingCallback
- bind_once / bind_repeating 与参数绑定
tags:
- host
- cpp-modern
- advanced
- 回调机制
- 函数对象
title: 'once_callback Design Guide (Part 1): Motivation and Interface Design'
translation:
  source: documents/vol9-open-source-project-learn/chrome/01_once_callback/hands_on/01-once-callback-design.md
  source_hash: f805de2373d582e728b8d0bc5f6e8ef4cfca177210cda0095c5d3031685afe07
  translated_at: '2026-06-16T04:14:25.190373+00:00'
  engine: anthropic
  token_count: 3060
---
# Design Guide for `once_callback` (Part 1): Motivation and Interface Design

## Introduction

Honestly, the most common pitfall I've encountered in asynchronous programming is callbacks being invoked multiple times. The scenario is classic: you register a callback for file I/O completion, expecting it to run once and be done. But due to a logic slip-up somewhere, it triggers an extra time. The resources released inside the callback are accessed a second time, leading straight to a segmentation fault. A major characteristic of this type of bug is that it is very hard to reproduce in tests, because normal asynchronous paths often only trigger the callback once. The real trigger is often some race condition or an error retry path.

`std::function` can't help us here. It allows multiple invocations, allows copy propagation, and callback objects can end up flying everywhere. In Volume 2, we already dissected the internal mechanisms of `std::function` (type erasure + SBO) and a simplified `small_function` implementation—that version solved the type erasure overhead problem but didn't touch the semantic issue of "how many times a callback should be invoked" at all.

When the Chromium team designed `OnceCallback`, they provided a very elegant answer: **Let the callback's type system itself constrain the invocation semantics.** `OnceCallback` is a move-only type; its `Run` method can only be invoked via an rvalue reference (`&&`). After one call, the callback object is consumed, and any subsequent call is a no-op or an assertion failure. This design has been fully validated in Chrome, where billions of tasks are posted daily.

Our goal in this series is not to copy Chromium's implementation (which is very complex, involving hand-written reference counting, `__attribute__` annotations, and function pointer dispatch tables), but to leverage new C++23 features—specifically `std::move_only_function` and deducing this—to implement a `once_callback` component that retains the essence of Chromium's design while keeping the codebase manageable.

> **Learning Objectives**
>
> - Understand why "move-only + one-time consumption" is the correct semantic constraint for callbacks.
> - Design the complete public interface for `once_callback`.
> - Analyze the internal architecture of Chromium `OnceCallback` to understand the reasoning behind each design decision.

---

## Our Problem: The Three Major Drawbacks of `std::function` in Asynchronous Scenarios

Before we start designing, let's clarify the problem. As a generic callable object container, `std::function` is a design success—but in the specific context of asynchronous callbacks, it has three issues that raise my blood pressure.

**First, it is copyable.** `std::function` natively supports copying, which means a callback can be copied to any number of places. In an asynchronous system, this equates to allowing multiple execution paths to hold copies of the same callback simultaneously. If the callback captures move-only resources (like `std::unique_ptr`), copying fails at compile time. If it captures raw pointers or references, multiple copies executing simultaneously creates a data race. The Chromium team's approach is straightforward: since asynchronous task callbacks fundamentally shouldn't be copied, make them non-copyable at the type level.

**Second, it is repeatable.** `std::function` places no constraints on the number of invocations. You can invoke the same `std::function` a thousand times, and it will run every time. However, in asynchronous callback scenarios, invoking a file-read completion callback twice is a logic error—it might trigger double resource release, double state transitions, or double message sending. This error is completely undetectable in the type system; we can only rely on runtime assertions (if they exist) or—more commonly—discovering it at the crime scene (the bug report).

**Third, it cannot express consumption semantics.** In Chrome's task posting model, once a `OnceCallback` is called, it should not be used again—its ownership has been transferred to the task system. `std::function`'s `operator()` is `const`-qualified; calling it does not change the state of the `std::function` object itself, so you cannot express the "call consumes" semantic through the calling interface.

These three issues boil down to one point: `std::function`'s interface design cannot express the constraint that "this callback can only be invoked once and becomes invalid afterward." Chrome's `OnceCallback` is designed specifically to fill this semantic gap.

---

## Chromium's Answer: `OnceCallback` Design Philosophy

Chrome's callback system is built on a core principle: **Message passing beats locking, serialization beats threading.** Under this principle, every callback posted to the task system (called a `task` in Chrome) is an independent, one-time message. Once posted, ownership of the callback transfers from the caller to the task system; once executed, the callback is destroyed. No sharing, no reuse, no ambiguity.

This philosophy is directly reflected in the type design of `OnceCallback`:

- **Move-only**: `OnceCallback` deletes the copy constructor and copy assignment, retaining only move operations. This guarantees at the type level that the callback has only one owner at any given moment.
- **Rvalue-qualified `Run`**: `Run` can only be invoked via an rvalue reference (`&&`). Lvalue invocation triggers a `static_assert`, producing a clear compile error. This reminds the caller at the syntax level: "You are consuming this callback; don't use it again."
- **Single consumption**: Internally, `OnceCallback` destroys the bound state via a reference counting mechanism (or similar logic) after the first call, making any subsequent access to the same object a safe no-op.

Chrome actually also has `RepeatingCallback`—a copyable, repeatable version. The two callback classes share the same internal `CallbackBase` implementation; the difference lies only in the value category qualification of `Run` and the ownership semantics. This design allows the same binding infrastructure to serve two distinct usage patterns: "one-shot tasks" and "repeating listeners."

### Overview of Chromium's Internal Implementation

We don't need to dive into every line of Chromium's source code, but we need to understand its core architecture because our `once_callback` will borrow a similar layered approach, using C++23 standard facilities to simplify the implementation.

Chromium's callback system consists of three layers, from bottom to top:

**Bottom Layer: `CallbackBase`**—The type-erased base class. It carries a reference count but, interestingly, **does not use virtual functions**. Instead, it uses three function pointer members: `Invoke` (handles invocation), `Destroy` (handles destruction), and `IsCancelled` (handles cancellation queries). The Chrome team chose function pointers over virtual functions to reduce binary bloat. Virtual functions generate a separate vtable for each template instantiation; if a project has 100 different `BindState` instantiations, there are 100 vtables. The function pointer approach allows reuse of the same static functions, differing only in pointer values, without generating additional code segments.

**Middle Layer: `BindState`**—A templated concrete class inheriting from `CallbackBase`. It stores the actual callable object (`Functor`) and arguments bound via `std::forward` (Args...). You can think of it as a "box containing everything": the box holds your lambda, bound arguments, and the function pointers required by the base class. Instances of this class manage their lifecycles via `scoped_refptr` (Chromium's own intrusive reference-counted smart pointer)—`scoped_refptr` releases references in `Destroy`, and keeps a reference during each `Invoke`.

**Top Layer: `OnceCallback` and `RepeatingCallback`**—The types users directly interact with. They are essentially thin wrappers around `scoped_refptr<CallbackBase>`, and `CallbackBase` is just a `__attribute__((packed))` annotated pointer. `__attribute__((packed))` is a Clang extension attribute telling the compiler "this type can be passed in a register like an `int`," making the actual size of `OnceCallback` just one pointer (8 bytes). Move operations are simply copying a pointer—extremely lightweight.

The relationship between these three layers can be summarized in one sentence: **The top-level callback object is just a pointer to the middle-layer box, and the box holds the function pointers required by the bottom layer and the actual data.** In our next design, `once_callback` will retain this "outer interface + middle storage + type erasure" layering, but we will use `std::move_only_function` to replace Chromium's hand-written `CallbackBase` + `BindState` combo, and deducing this to replace the `&` overload + `delete` hack.

---

## Environment Setup

First, let's confirm our toolchain. `once_callback` relies on the following C++23 features:

- **`std::move_only_function`** (`<functional>`): A move-only type-erased callable wrapper introduced in C++23, this is our core building block.
- **Deducing this** (Explicit object parameter `this`): A C++23 feature allowing deduction of the value category of `this` in member functions.
- **`if constexpr`**: Compile-time conditional judgment (may be used in some implementations).

In terms of compiler requirements, GCC 12+ or Clang 16+ fully supports the above features. Just add `-std=c++23` at compile time. You can quickly verify your environment with the following code snippet:

```cpp
// test_env.cpp
#include <utility>
#include <functional>

int main() {
    // Test 1: std::move_only_function
    std::move_only_function<void()> f = [] {};
    std::move_only_function<void()> f2 = std::move(f);

    // Test 2: Deducing this
    struct Test {
        void operator()(this auto&& self) {
            // If this compiles, deducing this is supported
        }
    };
    Test{}();

    return 0;
}
```

If this code compiles, your environment is good to go. However, honestly, as of the time of writing, some compilers' `std::move_only_function` implementations still have bugs (e.g., early versions of GCC 12 fail to compile in certain SFINAE scenarios), so I recommend using the latest stable versions of GCC 13+ or Clang 17+.

### Prerequisites

We assume the reader is already familiar with the following (covered in the corresponding Volume 2 articles):

- **Move Semantics and Perfect Forwarding**: `once_callback` is move-only at its core; if you aren't familiar with the principles of `std::move` and `std::forward`, the implementation process will be very painful. Corresponding article: Vol 2 ch00 Move Semantics series.
- **`std::function`'s Type Erasure and SBO**: We build directly on top of `std::move_only_function`, so you need to understand the basic principles of type erasure and what Small Buffer Optimization is and why it matters. Corresponding article: Vol 2 ch03 `std::function` and Callable Objects.
- **`std::invoke` and Uniform Calling Convention**: `std::move_only_function` uses `std::invoke` internally to uniformly handle function pointers, member function pointers, functors, and other callable types. Corresponding article: Ibid.
- **Variadic Templates and Parameter Pack Expansion**: Template specialization of `once_callback` and argument binding in `Bind` both require familiarity with parameter pack syntax. Corresponding article: Vol 2 ch00 Perfect Forwarding, Vol 4 Template Basics.

---

## Designing the Interface: What API Do We Want?

Let's settle on the target API first, then discuss each design decision. This is how engineers work—figure out "what I want" first, then "how to do it."

### Core Usage

```cpp
// Basic usage: create and run
auto cb = once_callback<void(int)>{ [](int x) { std::cout << x << '\n'; } };
std::move(cb).Run(42);  // OK: consumes the callback
// cb.Run(42);           // Error: lvalue call is disabled
```

### Argument Binding

```cpp
// Binding arguments (partial application)
void ProcessData(int id, const std::string& data) {
    // ...
}

// Bind 'id' in advance, leave 'data' for later
auto cb = BindOnce(ProcessData, 100);
std::move(cb).Run("hello");  // Calls ProcessData(100, "hello")
```

### Cancellation Checking

```cpp
// Check if the callback is still valid
auto cb = BindOnce(Task);
if (cb) {
    std::move(cb).Run();
}

// Or explicitly check
if (!cb.IsCancelled()) {
    std::move(cb).Run();
}
```

### Chained Composition

```cpp
// Chaining: pass the result of one callback to the next
auto cb1 = BindOnce(FetchData);
auto cb2 = std::move(cb1).Then([](const Data& d) {
    return Process(d);
});
std::move(cb2).Run();  // Executes FetchData -> Process
```

### Analysis of Interface Design Decisions

Now let's discuss the design decisions behind these APIs one by one.

**Why `Run` instead of `operator()`?**

Chromium uses `Run` (Google C++ style requires capitalization). We use `Run` to conform to snake_case naming conventions. But a deeper reason is semantic distinction: `operator()` is too generic; any callable object has it. `Run` explicitly expresses the semantics of "executing a task," making it immediately obvious during code review that this is consuming a `once_callback`, not just calling a generic callable object.

**Why must `Run` be called via an rvalue?**

This is the most critical point of the design. We need a mechanism where `cb.Run()` (lvalue call) fails to compile, but `std::move(cb).Run()` (rvalue call) succeeds. Chromium's implementation achieves this via two overloads: one `Run` is the actual execution version, and the other `&Run` contains a `static_assert` to produce a compile error. While effective, this hack is ugly.

We can do this more elegantly using C++23's **deducing this** (explicit object parameter). Simply put, deducing this allows us to write `this` explicitly as a template parameter in a member function, and the compiler deduces this parameter's type based on whether the object is an lvalue or rvalue when called. Using this feature, `Run` distinguishes between lvalue and rvalue calls by deducing the value category of `self`, intercepting illegal usage at compile time:

```cpp
struct once_callback {
    // ...
    void Run(this auto&& self) requires std::is_rvalue_reference_v<decltype(self)> {
        // Actual invocation logic
    }
};
```

When the caller writes `cb.Run()`, `self` is deduced as `once_callback&` (lvalue reference), the `requires` clause triggers, and the error message tells the caller exactly what to do. When writing `std::move(cb).Run()`, `self` is deduced as `once_callback&&` (rvalue), and compilation passes. We will expand on the specific working mechanism of deducing this and a detailed comparison with the Chromium approach in the next implementation article.

**Why distinguish between `IsCancelled` and `IsCancelled`?**

This design comes directly from Chromium's `Callback`. The difference lies in the strength of the safety guarantee. `IsCancelled` provides a definitive answer—it can only be called on the sequence where the callback is bound, guaranteeing an accurate result. `MayBeCancelled` provides an optimistic estimate—it can be called from any thread, but the result might be stale. In practice, `IsCancelled` is used for "checking if it still makes sense before posting," while `MayBeCancelled` is used for the optimization path of "quickly checking if it's worth posting across threads."

In our simplified implementation, both methods query via `std::move_only_function::operator bool`—`IsCancelled` checks if the state is valid and the token is still valid, `MayBeCancelled` is just a simple wrapper of `IsCancelled`. If finer-grained thread-safe semantics are needed later, we can distinguish between these two methods.

**Why does `Then` consume `once_callback`?**

The semantics of `Then` are "pass the result of the current callback to the next callback." This requires the current callback to be fully captured in the new callback returned by `Then`. If `Then` didn't consume `once_callback`, it would lead to the same callback existing in two places simultaneously—the original location and the new callback returned by `Then`—violating the move-only semantic constraint. Therefore, `Then` is declared as an rvalue-qualified member function (`&&`), and the original callback object enters a consumed state after the call.

---

## Internal Mechanism: The Two-Layer Architecture of Type Erasure

With the interface designed, let's look at how to organize the internals. Chromium used the `CallbackBase` + `BindState` + function pointer table combo to implement type erasure, which works well but results in a staggering amount of code. Our strategy is to use `std::move_only_function` to handle the dirty work of type erasure and small object optimization, allowing us to focus on the interesting parts: consumption semantics, argument binding, and chaining.

### Why Choose `std::move_only_function`

`std::move_only_function` was introduced in C++23, positioned as the "move-only version of `std::function`." It implements type erasure and SBO internally, behaving similarly to `std::function` but with copy operations deleted.

You may have noticed syntax like `std::move_only_function<R(Args...)>`—`R(Args...)` looks like a function declaration, but in the context of template parameters, it is a **function type**. `R(Args...)` describes "a function accepting arguments `Args...` and returning `R`," and it is a legal C++ type. We deconstruct this type via template partial specialization—we'll explain this technique in detail in the next article.

Using `std::move_only_function` for internal storage has several benefits. It saves us from hand-writing type erasion—recall in Volume 2 we spent a whole chapter hand-writing function pointer tables, SBO buffers, and move/destructor operations for `small_function`, whereas `std::move_only_function` encapsulates all of this for us. It natively supports move-only callables—if our callback captures a `std::unique_ptr`, `std::function` would fail to compile due to copy semantics requirements, but `std::move_only_function` handles this fine. Moreover, its SBO implementation has been carefully tuned by standard library authors, so in the vast majority of cases it doesn't require heap allocation—for lambdas capturing few arguments, performance is perfectly adequate.

### Three-State Management

After introducing `std::move_only_function`, there is a design problem to solve: how to distinguish between a "null callback" and a "consumed callback"?

`std::move_only_function` itself can be null (default constructed or constructed from `nullptr`), but "null" and "already consumed by `Run`" are two different states. A null callback means "never assigned," and calling it should trigger a clear error ("callback is null"). A consumed callback means "once had a value, but has already been invoked," and calling it should also trigger an error ("callback already consumed"), but the error message is different, which is helpful for debugging.

So our internal state needs three states:

```cpp
enum class State {
    Null,       // Never assigned
    Active,     // Assigned, not yet run
    Consumed    // Already run
};
```

Combined with `std::move_only_function`, our internal storage structure looks roughly like this:

```cpp
class once_callback {
    State state_ = State::Null;
    std::move_only_function<R(Args...)> func_;
    // Optional: CancellationToken* token_
};
```

On move construction, `state_` and `func_` are moved together, and the source object's state is set to `State::Null`. When `Run` executes, it first checks if `state_` is `State::Active`, and after execution sets `func_` to null and `state_` to `State::Consumed`. This way, precise error messages can be given based on the value of `state_` during debugging.

### Trade-offs Compared to Chromium's Original Version

Using `std::move_only_function` as the underlying storage gives us a simple implementation, but we also sacrifice some things. Chromium's `OnceCallback` is only the size of one pointer (8 bytes), thanks to the `__attribute__((packed))` annotation and reference-counted `CallbackBase`—the callback object itself is just a pointer to a heap-allocated `BindState`. Our `once_callback` wraps `std::move_only_function` (typically 32 bytes) plus a `State` enum and an optional `CancelToken*` pointer (16 bytes), totaling roughly 56-64 bytes.

Another difference is reference counting. Chromium's `CallbackBase` is reference-counted, allowing multiple callbacks to share the same bound state (necessary for `RepeatingCallback`'s copy semantics). In our implementation, `once_callback` has exclusive ownership and does not support sharing. For `once_callback`'s move-only semantics, this isn't an issue, but when implementing `repeating_callback` later, this design will need to be reconsidered.

These trade-offs are reasonable—we traded size and the flexibility of reference counting for a significantly lower implementation complexity. In actual use, a 56-64 byte callback object is not a bottleneck in the vast majority of scenarios, and the clear code structure makes maintenance and extension much cheaper.

---

## Summary

In this article, we completed the design foundation for `once_callback`. Key takeaways:

- `std::function` has three major drawbacks in asynchronous callback scenarios: copyable, repeatable, and unable to express consumption semantics.
- Chromium's `OnceCallback` constrains callback semantics via move-only + rvalue-qualified `Run` + single consumption.
- Our `once_callback` uses `std::move_only_function` for underlying type erasure and deducing this to implement rvalue-qualified `Run`.
- Internally, we use three-state management (`Null` / `Active` / `Consumed`) to distinguish between null and consumed callbacks.

In the next article, we will enter the implementation phase: starting with the core skeleton `once_callback`, and gradually adding `Bind`, cancellation checks, and `Then` chaining.

## Reference Resources

- [Chromium Callback Documentation](https://chromium.googlesource.com/chromium/src/+/main/docs/callback.md)
- [Chromium callback.h Source Code](https://chromium.googlesource.com/chromium/src/+/HEAD/base/functional/callback.h)
- [cppreference: std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [P0847R7 - Deducing this Proposal](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0847r7.html)
