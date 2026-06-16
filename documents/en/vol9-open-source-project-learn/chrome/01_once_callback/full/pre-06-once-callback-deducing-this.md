---
chapter: 0
cpp_standard:
- 23
description: Deep dive into how C++23 explicit object parameters (deducing this) allow
  `OnceCallback::run()` to elegantly intercept lvalue calls at compile time, replacing
  Chromium's double-overload hack.
difficulty: intermediate
order: 6
platform: host
prerequisites:
- OnceCallback 前置知识速查：C++11/14/17 核心特性回顾
reading_time_minutes: 8
related:
- OnceCallback 实战（二）：核心骨架搭建
- OnceCallback 前置知识（四）：Concepts 与 requires 约束
tags:
- host
- cpp-modern
- intermediate
- 模板
title: 'OnceCallback Prerequisites (Part 6): Deducing this (C++23)'
translation:
  source: documents/vol9-open-source-project-learn/chrome/01_once_callback/full/pre-06-once-callback-deducing-this.md
  source_hash: 953b2e8cc557c7021354212b0dda82e5d95ec9dcb024ad800bb9f1173d768016
  translated_at: '2026-06-16T04:14:03.100115+00:00'
  engine: anthropic
  token_count: 1658
---
# Prerequisites for OnceCallback (Part 6): Deducing this (C++23)

## Introduction

The `OnceCallback::run` method is the soul of the entire component and is the most feature-dense method in terms of C++23 features. Its declaration looks like this:

```cpp
template <typename... Args>
auto operator()(Args&&... args) -> decltype(auto) requires /* ... */;
```

If you haven't seen the syntax like `this auto&& self`—don't panic, this post is specifically about it. This is the "explicit object parameter" feature introduced in C++23, officially known as **deducing this**. It allows `OnceCallback` to use a single function template to achieve the effect of "compile-time error for lvalue calls, normal execution for rvalue calls," which is much cleaner than Chromium's approach.

> **Learning Objectives**
>
> - Understand the syntax and deduction rules of deducing this
> - Master how `OnceCallback::run` uses it to implement compile-time lvalue/rvalue interception
> - Understand the role of lazy instantiation in `static_assert`
> - Compare the applicable scenarios of deducing this and traditional ref-qualifiers

---

## Problem: How to make `cb.run()` fail to compile

The core semantic of `OnceCallback` is "can only be called once, and must be called via an rvalue." Expressed in code:

```cpp
OnceCallback cb = ...;
// cb.run();       // Compile error: lvalue call
std::move(cb).run(); // OK: rvalue call
```

We need a mechanism that allows `run()` to distinguish between "called via an lvalue" and "called via an rvalue" at compile time, and provides a clear error message for lvalue calls.

### Chromium's Old Approach

Chromium didn't have the benefit of C++23, so it used a rather hacky approach—two overloads:

```cpp
// Rvalue overload (actual implementation)
ReturnType operator()(Args&&... args) &&;

// Lvalue overload (deleted to prevent calls)
template <typename... Args>
void operator()(Args&&... args) & = delete;
```

Why use `= delete` instead of directly writing `static_assert(false)`? Because prior to C++23, `static_assert(false)` in a template would trigger the assertion on all code paths—even if the function was never called. C++23 relaxed this restriction. The `= delete` approach leverages the fact that `sizeof` must be evaluated on a complete type—it is a dependent expression that is only evaluated during template instantiation, thus achieving the effect of "triggering only when actually called."

It works, but it's certainly not elegant—requiring two overloaded functions to handle the same thing, and the `sizeof` hack has poor readability.

---

## Syntax and Deduction Rules of deducing this

C++23's deducing this allows us to explicitly write the implicit `this` object parameter as the first parameter of a member function, and use a template parameter to deduce its type and value category.

### Basic Syntax

```cpp
struct Widget {
    void name(this auto&& self);
};
```

`this auto&& self` is the declaration of the explicit object parameter. The keyword `this` appearing before the type tells the compiler "this is not a normal parameter, but an explicit object parameter." `auto&&` is the deduction placeholder—the compiler will deduce the specific type of `self` based on the value category of the object at the call site.

### Deduction Rules

The type deduction rules for `self` are exactly the same as for forwarding references—because the deduction context of `auto&&` is equivalent to a template parameter:

- **Lvalue call** `w.name()`:
  - The type of `self` is deduced as `Widget&` (lvalue reference)
- **Rvalue call** `std::move(w).name()` or `Widget{}.name()`:
  - The type of `self` is deduced as `Widget` (non-reference, pure type)
- **const lvalue call** (assuming `Widget` is const):
  - The type of `self` is deduced as `const Widget&`

### Verifying Deduction Results

```cpp
struct Test {
    void check(this auto&& self) {
        std::cout << __PRETTY_FUNCTION__ << '\n';
    }
};

int main() {
    Test t;
    t.check();       // Deduced as: void check(Test &)
    std::move(t).check(); // Deduced as: void check(Test)
    const Test ct;
    ct.check();      // Deduced as: void check(const Test &)
}
```

---

## Application in `OnceCallback::run`

Now let's look at the full implementation of `OnceCallback::run` to understand how it uses deducing this to intercept lvalue calls.

```cpp
template <typename R, typename... Args>
class OnceCallback<R(Args...)> {
public:
    // ...
    template <typename Self>
    decltype(auto) operator()(this Self&& self, Args&&... args) {
        // 1. Intercept lvalue calls
        static_assert(
            !std::is_lvalue_reference_v<Self>,
            "OnceCallback::run() must be called on an rvalue. "
            "Use std::move(cb).run(...) instead."
        );

        // 2. Forward to impl_run
        return std::forward<Self>(self).impl_run(
            std::forward<Args>(args)...
        );
    }
};
```

This code does three things; let's break them down one by one.

### Intercepting Lvalue Calls

`std::is_lvalue_reference_v<Self>` checks whether `Self` is an lvalue reference type. When the caller writes `cb.run()`, `cb` is an lvalue, `Self` is deduced as `OnceCallback&`—this is an lvalue reference type, `is_lvalue_reference` returns `true`, negated it becomes `false`, `static_assert` fails, and the compiler reports the error message we wrote: "OnceCallback::run() must be called on an rvalue. Use std::move(cb).run(...) instead."

When the caller writes `std::move(cb).run()`, `cb` is an rvalue (strictly speaking, an xvalue), `Self` is deduced as `OnceCallback`—not a reference type, `is_lvalue_reference` returns `false`, negated it becomes `true`, `static_assert` passes, and code execution continues.

### Forwarding to `impl_run`

`std::forward<Self>(self)` determines whether to return an lvalue reference or an rvalue reference based on the type of `Self`. Since the `static_assert` has already excluded the lvalue case, `Self` reaching this point must be a non-reference type (rvalue), so `std::forward<Self>(self)` returns an rvalue reference—ensuring `impl_run` is called on an rvalue.

### Lazy Instantiation

There is a nuanced detail here—the condition of `static_assert` depends on the template parameter `Self`, so it is only evaluated when the template is instantiated. This means:

- If `run()` is never called, `static_assert` will not trigger—regardless of whether the `OnceCallback` object itself is an lvalue or an rvalue.
- Only at a specific call point, when the compiler needs to instantiate this template, is the specific type of `Self` determined, and `static_assert` evaluated.

This is called "lazy instantiation," a fundamental characteristic of C++ templates. Function templates are only instantiated when used—no usage means no instantiation and no checks. This is why Chromium had to use `= delete` instead of directly writing `static_assert(false)`—prior to C++23, `static_assert(false)` did not depend on template parameters and would trigger at template definition time, rather than waiting for instantiation.

---

## Comparison with Traditional Ref-qualifiers

In `OnceCallback`, there are two methods that express the "callable only via rvalue" semantic—`run` uses deducing this, while `then` uses the traditional ref-qualifier `&&`. Why not unify the approach?

### `then()` uses Ref-qualifier

```cpp
template <typename F>
auto then(F&& f) && -> OnceCallback<...>;
```

`then`'s requirement is simple—it only accepts rvalues, rejects lvalues, and doesn't need to distinguish between them to give different error messages. If the caller writes `cb.then(...)` (lvalue call), the compiler directly reports "no matching overloaded function." Although the error message is less instructive than deducing this, it is sufficient. The ref-qualifier is also more concise to write—just one `&&` and you're done.

### `run()` uses Deducing this

`run`'s requirement is more refined—it not only needs to reject lvalue calls, but also needs to provide an **instructive error message**, telling the caller "you should use `std::move(cb).run(...)` instead." Deducing this makes this requirement natural—`static_assert` can output our custom error message instead of the compiler's default "no matching function."

### Selection Strategy

To summarize: If you only need the constraint of "accept rvalues only," the `&&` qualifier is more concise. If you also need to provide a custom error message for lvalue calls, deducing this combined with `static_assert` is more appropriate.

---

## Pitfall Warning

### Explicit Object Parameters Cannot Coexist with cv-qualifiers or Ref-qualifiers

Member functions with an explicit object parameter cannot simultaneously be declared `const`, `volatile`, or with a ref-qualifier (`&`/`&&`). This is because the explicit object parameter has already taken over the deduction of the object type and value category—`const`/`volatile` and ref-qualifiers become redundant or even contradictory.

```cpp
struct Bad {
    void foo(this auto&& self) const; // Error: conflicts with explicit object parameter
    void bar(this auto&& self) &;     // Error: conflicts with explicit object parameter
};
```

### Explicit Object Parameters Cannot Be Static Functions

Explicit object parameter functions are not static functions—they still require an object instance to invoke. The `self` parameter is deduced by the compiler from the call expression, not manually passed by the caller.

### Compiler Support

Deducing this is a C++23 feature. GCC 14+, Clang 18+, and MSVC 19.34+ support this feature. If your compiler does not support it, you must fall back to Chromium's double overload approach.

---

## Summary

In this post, we clarified the ins and outs of deducing this. It allows `OnceCallback::run` to use a single function template to implement compile-time lvalue/rvalue interception—by judging whether the caller passed an lvalue or rvalue based on the deduced type of `Self`, combined with `static_assert` to provide an instructive error message. Compared to Chromium's two overloads + `sizeof` hack, the deducing this solution is more concise and aligns better with C++ design philosophy. Since `then()` does not need custom error messages, the traditional `&&` qualifier is more concise.

At this point, all prerequisites have been covered. In the next post, we will officially enter the practical implementation of OnceCallback—starting with motivation analysis and designing our target API.

## References

- [P0847R7 - Deducing this Proposal](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0847r7.html)
- [C++23's Deducing this (Microsoft C++ Blog)](https://devblogs.microsoft.com/cppblog/cpp23-deducing-this/)
- [cppreference: Explicit object parameter](https://en.cppreference.com/w/cpp/language/member_functions#Explicit_object_parameter)
