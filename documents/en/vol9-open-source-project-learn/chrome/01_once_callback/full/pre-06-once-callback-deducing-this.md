---
title: 'OnceCallback Prerequisites (Part 6): Deducing this (C++23)'
description: A deep dive into how C++23 explicit object parameters (deducing this)
  allow `OnceCallback::run()` to elegantly intercept lvalue calls at compile time,
  replacing Chromium's double-overload hack.
chapter: 0
order: 6
tags:
- host
- cpp-modern
- intermediate
- 模板
difficulty: intermediate
platform: host
cpp_standard:
- 23
reading_time_minutes: 10
prerequisites:
- OnceCallback 前置知识速查：C++11/14/17 核心特性回顾
related:
- OnceCallback 实战（二）：核心骨架搭建
- OnceCallback 前置知识（四）：Concepts 与 requires 约束
translation:
  source: documents/vol9-open-source-project-learn/chrome/01_once_callback/full/pre-06-once-callback-deducing-this.md
  source_hash: d12fb3db3d69c7e5d8d830b169ac971f50fde46b1eb5255baed897fdfe1dbe18
  translated_at: '2026-05-26T12:28:53.615588+00:00'
  engine: anthropic
  token_count: 1664
---
# Prerequisite Knowledge for OnceCallback (Part 6): Deducing this (C++23)

## Introduction

The ``run()`` method of OnceCallback is the soul of the entire component, and it is also the method with the densest concentration of C++23 features. Its declaration looks like this:

```cpp
template<typename Self>
auto run(this Self&& self, FuncArgs&&... args) -> ReturnType;
```

If you have never seen the ``this Self&& self`` syntax—don't panic, this article is dedicated entirely to explaining it. This is the "explicit object parameter" feature introduced in C++23, officially named **deducing this**. It allows OnceCallback to achieve the effect of "compile error on lvalue invocation, normal execution on rvalue invocation" with a single function template, which is much cleaner than Chromium's approach.

> **Learning Objectives**
>
> - Understand the syntax and deduction rules of deducing this
> - Grasp how ``run()`` uses it to implement compile-time lvalue/rvalue interception
> - Understand the role of lazy instantiation in ``static_assert``
> - Compare the applicable scenarios of deducing this and traditional ref-qualifiers

---

## The Problem: How to Make `cb.run()` Fail to Compile

The core semantic of OnceCallback is "can only be called once, and must be called through an rvalue". Expressed in code:

```cpp
OnceCallback<int(int)> cb([](int x) { return x * 2; });

cb.run(5);                  // 应该编译失败：cb 是左值
std::move(cb).run(5);       // 应该编译通过：std::move(cb) 是右值
```

We need a mechanism that allows ``run()`` to distinguish between "called through an lvalue" and "called through an rvalue" at compile time, and to provide a clear error message for lvalue invocations.

### Chromium's Old Approach

Chromium didn't have the benefit of C++23, so it used a rather hacky approach—two overloads:

```cpp
// 右值版本：真正的执行
R Run() && {
    // 执行回调...
}

// 左值版本：编译报错
R Run() const& {
    static_assert(!sizeof(*this),
        "OnceCallback::Run() may only be invoked on a non-const rvalue, "
        "i.e. std::move(callback).Run().");
}
```

Why use ``!sizeof(*this)`` instead of simply writing ``false``? Because prior to C++23, ``static_assert(false, "...")`` in a template would trigger the assertion in all code paths—even if the function was never called. C++23 relaxed this restriction. ``!sizeof(*this)`` leverages the characteristic that ``sizeof`` can only be evaluated on a complete type—it is a dependent expression that is only evaluated during template instantiation, thereby achieving the effect of "only triggering when actually called".

It works, but it is indeed inelegant—it requires two overloaded functions to handle the same thing, and the ``!sizeof`` hack has poor readability.

---

## Syntax and Deduction Rules of deducing this

C++23's deducing this allows us to explicitly write ``this`` as the first parameter of a member function, and use a template parameter to deduce its type and value category.

### Basic Syntax

```cpp
struct MyStruct {
    void f(this auto&& self) {
        // self 就是 this——但它的类型是推导出来的
    }
};
```

``this auto&& self`` is the declaration of the explicit object parameter. The keyword ``this`` appears before the type, telling the compiler "this is not a normal parameter, but an explicit object parameter". ``auto&&`` is the deduction placeholder—the compiler will deduce the concrete type of ``self`` based on the value category of the object at the call site.

### Deduction Rules

The type deduction rules for ``self`` are exactly the same as those for forwarding references—because the deduction context of ``self`` is equivalent to a template parameter:

- **Lvalue call** ``obj.f()``: The type of ``self`` is deduced as ``MyStruct&`` (lvalue reference)
- **Rvalue call** ``std::move(obj).f()`` or ``MyStruct{}.f()``: The type of ``self`` is deduced as ``MyStruct`` (non-reference, plain type)
- **const lvalue call** ``std::as_const(obj).f()``: The type of ``self`` is deduced as ``const MyStruct&``

### Verifying the Deduction Results

```cpp
#include <iostream>
#include <type_traits>

struct Check {
    void test(this auto&& self) {
        using Self = decltype(self);
        if constexpr (std::is_lvalue_reference_v<Self>) {
            std::cout << "lvalue reference\n";
        } else {
            std::cout << "rvalue (not a reference)\n";
        }
    }
};

int main() {
    Check c;
    c.test();                  // 输出：lvalue reference
    std::move(c).test();       // 输出：rvalue (not a reference)
    std::as_const(c).test();   // 输出：lvalue reference (const)
}
```

---

## Application in `OnceCallback::run()`

Now let's look at the complete implementation of ``run()`` to understand how it leverages deducing this to intercept lvalue calls.

```cpp
template<typename Self>
auto run(this Self&& self, FuncArgs&&... args) -> ReturnType {
    static_assert(!std::is_lvalue_reference_v<Self>,
        "OnceCallback::run() must be called on an rvalue. "
        "Use std::move(cb).run(...) instead.");
    return std::forward<Self>(self).impl_run(std::forward<FuncArgs>(args)...);
}
```

This code does three things, which we will break down one by one.

### Intercepting Lvalue Calls

``std::is_lvalue_reference_v<Self>`` checks whether ``Self`` is an lvalue reference type. When the caller writes ``cb.run(args)``, ``cb`` is an lvalue, and ``Self`` is deduced as ``OnceCallback&``—this is an lvalue reference type, ``is_lvalue_reference_v`` returns ``true``, which becomes ``false`` after negation, ``static_assert`` fails, and the compiler reports our custom error message: "OnceCallback::run() must be called on an rvalue. Use std::move(cb).run(...) instead."

When the caller writes ``std::move(cb).run(args)``, ``std::move(cb)`` is an rvalue (strictly speaking, an xvalue), and ``Self`` is deduced as ``OnceCallback``—not a reference type, ``is_lvalue_reference_v`` returns ``false``, which becomes ``true`` after negation, ``static_assert`` passes, and code execution continues.

### Forwarding to `impl_run`

``std::forward<Self>(self)`` decides whether to return an lvalue reference or an rvalue reference based on the type of ``Self``. Since ``static_assert`` has already ruled out the lvalue case, the ``Self`` that reaches this point must be a non-reference type (an rvalue), so ``std::forward<Self>(self)`` returns an rvalue reference—ensuring that ``impl_run`` is called on an rvalue.

### Lazy Instantiation

There is a fascinating detail here—the condition of ``static_assert`` depends on the template parameter ``Self``, so it is only evaluated upon template instantiation. This means:

- If ``run()`` is never called, ``static_assert`` will not trigger—regardless of whether the ``OnceCallback`` object itself is an lvalue or an rvalue
- Only at a specific call site, when the compiler needs to instantiate this template, will the concrete type of ``Self`` be determined, and ``static_assert`` will be evaluated

This is called "lazy instantiation", a fundamental characteristic of C++ templates. Function templates are only instantiated when used—if not used, they are not instantiated, and no checks are performed. This is why Chromium had to use ``!sizeof(*this)`` instead of simply writing ``false``—prior to C++23, ``static_assert(false)`` does not depend on a template parameter, so it would trigger at template definition time rather than waiting for instantiation.

---

## Comparison with Traditional ref-qualifiers

OnceCallback has two methods that express the "can only be called through an rvalue" semantic—``run()`` uses deducing this, while ``then()`` uses the traditional ref-qualifier ``&&``. Why not unify the approach?

### `then()` Uses a ref-qualifier

```cpp
template<typename Next>
auto then(Next&& next) && -> OnceCallback<...>;
```

The requirement for ``then()`` is simple—it only accepts rvalues, rejects lvalues, and does not need to distinguish between them to provide different error messages. If the caller writes ``cb.then(next)`` (an lvalue call), the compiler directly reports "no matching overloaded function". Although this error message is not as instructive as the one from deducing this, it is sufficient. The ref-qualifier is also more concise to write—a single ``&&`` does the job.

### `run()` Uses deducing this

The requirement for ``run()`` is more refined—it not only needs to reject lvalue calls, but also needs to provide an **instructive error message** telling the caller "you should use ``std::move(cb).run(...)`` instead of ``cb.run(...)``". Deducing this makes this requirement natural—``static_assert`` can output our custom error message, rather than the compiler's default "no matching function".

### Selection Strategy

To summarize: if you only need the constraint of "accept rvalues only", using the ``&&`` qualifier is more concise. If you also need to provide a custom error message for lvalue calls, using deducing this paired with ``static_assert`` is more appropriate.

---

## Pitfall Warnings

### Explicit Object Parameters Cannot Coexist with cv-qualifiers or ref-qualifiers

A member function with an explicit object parameter cannot simultaneously be declared as ``const``, ``volatile``, or have a ref-qualifier (``&``/``&&``). This is because the explicit object parameter has already taken over the deduction of the object's type and value category—making ``const`` and ``&&`` qualifiers redundant or even contradictory.

```cpp
struct Bad {
    void f(this auto&& self) const;   // 编译错误：不能同时有显式对象参数和 const
    void g(this auto&& self) &&;      // 编译错误：不能同时有显式对象参数和 &&
};
```

### Explicit Object Parameter Functions Cannot Be Static

An explicit object parameter function is not a static function—it still requires an object instance to be called. The ``this`` parameter is deduced by the compiler from the call expression, not manually passed in by the caller.

### Compiler Support

Deducing this is a C++23 feature. GCC 14+, Clang 18+, and MSVC 19.34+ support this feature. If your compiler does not support it, you will have to fall back to Chromium's double-overload approach.

---

## Summary

In this article, we thoroughly understood the ins and outs of deducing this. It allows ``run()`` to achieve compile-time lvalue/rvalue interception with a single function template—by checking the deduced type of ``Self`` to determine whether the caller passed an lvalue or an rvalue, paired with ``static_assert`` to provide an instructive error message. Compared to Chromium's two overloads + ``!sizeof`` hack, the deducing this approach is more concise and better aligns with C++'s design philosophy. Meanwhile, since ``then()`` does not need a custom error message, using the traditional ``&&`` qualifier is more concise.

At this point, all prerequisite knowledge has been covered. In the next article, we will officially enter the practical implementation phase of OnceCallback—starting from a motivation analysis to design our target API.

## Reference Resources

- [P0847R7 - Deducing this Proposal](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0847r7.html)
- [C++23's Deducing this (Microsoft C++ Blog)](https://devblogs.microsoft.com/cppblog/cpp23-deducing-this/)
- [cppreference: Explicit object parameter](https://en.cppreference.com/w/cpp/language/member_functions#Explicit_object_parameter)
