---
chapter: 1
cpp_standard:
- 23
description: A line-by-line breakdown of the parameter binding implementation in `bind_once`—from
  the motivation to lambda capture pack expansion, followed by manually expanding
  a complete template instantiation example.
difficulty: beginner
order: 3
platform: host
prerequisites:
- OnceCallback 实战（二）：核心骨架搭建
- OnceCallback 前置知识（二）：std::invoke 与统一调用协议
- OnceCallback 前置知识（三）：Lambda 高级特性
reading_time_minutes: 7
related:
- OnceCallback 实战（四）：取消令牌设计
tags:
- host
- cpp-modern
- beginner
- 回调机制
- 函数对象
- 模板
title: 'OnceCallback in Practice (Part 3): Implementing bind_once'
translation:
  source: documents/vol9-open-source-project-learn/chrome/01_once_callback/full/01-3-once-callback-bind-once.md
  source_hash: d91277bee67e57d70108ec1a132efe8dcdf8b7a51b6a35d1a2d6d9a5971aec54
  translated_at: '2026-06-16T04:13:11.135927+00:00'
  engine: anthropic
  token_count: 1473
---
# OnceCallback in Practice (Part 3): Implementing `bind_once`

## Introduction

The core framework is in place, and `OnceCallback` can now consume callbacks. However, constructing a `OnceCallback` currently requires passing a callable object with a specific signature, where all arguments must be provided at the call site. In reality, we often encounter situations where certain arguments are known at callback creation time, while only a subset of arguments needs to be deferred until the call is made. `bind_once` is designed to solve this problem—it "bakes" the known arguments into the callback, allowing the caller to focus only on the unknown arguments.

In this article, we will deconstruct the implementation of `bind_once` line by line and manually expand a complete template instantiation example to reveal exactly what the compiler does behind the scenes.

> **Learning Objectives**
>
> - Understand what problems argument binding solves.
> - Understand the complete implementation of `bind_once` line by line.
> - Manually expand a specific template instantiation to see what the compiler does.
> - Understand why the signature must be explicitly specified.

---

## What Problem Does Argument Binding Solve?

Let's first look at a scenario without `bind_once`. Suppose you have a function with three parameters, but the first two are determined at the time of binding:

```cpp
void HandleEvent(EventSource* source, int id, const std::string& data);
```

If `source` and `id` are determined at binding time, and only `data` needs to be passed in at call time, we want to obtain a `OnceCallback` that only takes one argument.

Without `bind_once`, you would have to manually write a lambda wrapper:

```cpp
// Manual lambda wrapper
OnceCallback<void(const std::string&)> cb =
    [source, id](const std::string& data) {
        HandleEvent(source, id, data);
    };
```

This works, but if the number of arguments increases or the types become complex (e.g., binding move-only types like `unique_ptr`), manually writing lambdas becomes tedious. `bind_once` automates this "wrap in a lambda" process.

```cpp
// Automated binding
auto cb = bind_once<void(const std::string&)>(&HandleEvent, source, id);
```

---

## Line-by-Line Breakdown of `bind_once` Implementation

Let's examine the source code to understand what `bind_once` does.

```cpp
template <typename Signature, typename Callable, typename... BoundArgs>
auto bind_once(Callable&& callable, BoundArgs&&... bound_args) {
    return OnceCallback<Signature>(
        [callable = std::forward<Callable>(callable),
         ...bound_args = std::forward<BoundArgs>(bound_args)](auto&&... unbound_args) mutable {
            std::invoke(std::move(callable),
                        std::move(bound_args)...,
                        std::forward<decltype(unbound_args)>(unbound_args)...);
        }
    );
}
```

### Template Parameters

`bind_once` has three template parameters. `Signature` is the function signature of the target callback (e.g., `void(int)`), which must be explicitly specified by the caller. `Callable` is the type of the callable object (lambda closure type, function pointer, etc.), deduced by the compiler from the first function argument. `BoundArgs` is the type pack of the bound arguments, also deduced by the compiler.

### Lambda Capture List

The capture list is the most ingenious part of the entire implementation. `callable = std::forward<Callable>(callable)` uses init capture to perfectly forward the callable object into the lambda closure—if an rvalue is passed, it is moved; if an lvalue is passed, it is copied.

`...bound_args = std::forward<BoundArgs>(bound_args)` is a lambda init capture pack expansion introduced in C++20. It generates a corresponding capture variable for each type in `BoundArgs`, each initialized via perfect forwarding. Assuming `BoundArgs` is `int, std::string`, expansion is equivalent to:

```cpp
// Expanded capture list
[callable = std::forward<Callable>(callable),
 bound_args1 = std::forward<BoundArgs1>(bound_args1),
 bound_args2 = std::forward<BoundArgs2>(bound_args2)]
```

### Lambda Parameters and `mutable`

`auto&&... unbound_args` are forwarding references for the generic lambda—arguments passed at runtime are received through them. `auto&&` here is equivalent to `T&&` in template parameters, which are forwarding references.

The `mutable` keyword cannot be omitted—the lambda body needs to call `std::move(callable)` and `std::move(bound_args)...`. These operations modify the captured variables. If the lambda is const, the captured variables are const inside the body, preventing a move from a const object.

### Lambda Body

```cpp
std::invoke(std::move(callable),
            std::move(bound_args)...,
            std::forward<decltype(unbound_args)>(unbound_args)...);
```

`std::invoke` uniformly handles all types of callable objects—as discussed in the previous article. `std::move(callable)` forwards the callable object as an rvalue. `std::move(bound_args)...` forwards all bound arguments as rvalues (since captured variables inside the lambda are lvalues, `std::move` is needed to cast them to rvalues). `std::forward<decltype(unbound_args)>(unbound_args)...` perfectly forwards the runtime arguments.

Bound arguments come first (`std::move(bound_args)...`), and runtime arguments come last (`std::forward...`). This order is crucial—it determines which arguments are "pre-bound" and which are deferred until the call.

---

## Manually Expanding a Concrete Example

Let's use a concrete call example to manually expand the code after template instantiation. Suppose:

```cpp
class Printer {
public:
    void Print(int id, const std::string& msg);
};

Printer* p = new Printer();
auto cb = bind_once<void(const std::string&)>(&Printer::Print, p, 5);
```

### Template Parameter Deduction

`Signature = void(const std::string&)` (explicitly specified), `Callable = void(Printer::*)(int, const std::string&)` (member function pointer type), `BoundArgs = Printer*, int` (object pointer + first argument).

### Lambda Capture Expansion

```cpp
// Expanded lambda capture
[callable = std::forward<void(Printer::*)(int, const std::string&)>(callable),
 bound_args1 = std::forward<Printer*>(bound_args1), // captures p
 bound_args2 = std::forward<int>(bound_args2)]     // captures 5
(auto&&... unbound_args) mutable { ... }
```

`callable` captures the member function pointer, `bound_args1` captures the object pointer `p`, and `bound_args2` captures the bound integer `5`.

### `std::invoke` Expansion Inside Lambda

When `cb` is called, `cb("hello")` is executed. `std::invoke` receives:

```cpp
// Arguments received by std::invoke
std::invoke(
    std::move(callable),           // &Printer::Print
    std::move(bound_args1),        // p
    std::move(bound_args2),        // 5
    std::forward<const std::string&>("hello") // "hello"
);
```

Which is:

```cpp
// Equivalent call
std::invoke(&Printer::Print, p, 5, "hello");
```

`std::invoke` detects that the first argument is a member function pointer and the second is a pointer to an object, so it expands to:

```cpp
// Expanded member function call
(p->*(&Printer::Print))(5, "hello");
```

Equivalent to `p->Print(5, "hello")`, resulting in the member function being called with the bound arguments and the runtime argument.

### Lifetime Trap

Note that `bound_args1` captures a raw pointer `p`. `OnceCallback` does not manage the lifetime of `p`. If `p` is destroyed before the callback is invoked, the lambda holds a dangling pointer. `std::invoke` accessing freed memory via a dangling pointer results in undefined behavior.

Chromium uses `raw_ptr` to explicitly mark raw pointer safety, `std::unique_ptr` to take ownership, and `WeakPtr` to automatically cancel the callback when the object is destroyed. Our simplified version temporarily delegates safety responsibilities to the caller.

---

## Why Must the Signature Be Explicitly Specified?

You may have noticed that the `Signature` in `bind_once<Signature>` must be written manually. Ideally, the compiler should automatically deduce the remaining signature from the callable's signature and the number of bound arguments. However, this is more difficult in C++ than it seems.

For a function pointer `void(*)(int, float)`, one can extract the parameter list via template partial specialization and then use compile-time "type list slicing" to remove the first N types. For functors with a definite signature, one can extract the signature using `decltype(&Functor::operator())`. But for **generic lambdas** (`[](auto x){}`), its `operator()` is itself a template, so there is no unique signature—the compiler cannot obtain information about "what arguments this lambda accepts" at the type level.

Chromium wrote hundreds of lines of template metaprogramming code to handle various edge cases. For teaching purposes, asking the caller to write one extra template parameter `Signature` is a more pragmatic choice.

---

## Summary

In this article, we deconstructed the implementation of `bind_once` line by line. It uses C++20's lambda capture pack expansion to expand bound arguments into the lambda's capture list, uses `std::invoke` to uniformly handle various callable objects (especially member function pointers), and uses the `mutable` keyword to allow modification of captured variables inside the lambda. We manually expanded a complete template instantiation for member function binding to see how `std::invoke` unwraps a member function pointer and object pointer into a normal member function call. Finally, we discussed why the signature must be explicitly specified—the existence of generic lambdas makes automatic deduction extremely complex.

In the next article, we will look at the design of cancellation tokens—a lightweight cancellation mechanism implemented using `std::shared_ptr` and `std::weak_ptr`.

## References

- [Chromium bind_internal.h source](https://chromium.googlesource.com/chromium/src/+/HEAD/base/functional/bind_internal.h)
- [cppreference: std::invoke](https://en.cppreference.com/w/cpp/utility/functional/invoke)
- [P0780R2 - Pack Expansion in Lambda Capture](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0780r2.html)
