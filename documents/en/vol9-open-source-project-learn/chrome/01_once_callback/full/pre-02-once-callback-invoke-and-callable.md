---
title: 'OnceCallback Prerequisites (Part 2): std::invoke and the Unified Call Protocol'
description: A deep dive into how `std::invoke` unifies the calling conventions of
  function pointers, member function pointers, lambda expressions, and functors, and
  the role of `std::invoke_result_t` in type deduction for `OnceCallback`.
chapter: 0
order: 2
tags:
- host
- cpp-modern
- intermediate
- 函数对象
- std_invoke
difficulty: intermediate
platform: host
cpp_standard:
- 17
reading_time_minutes: 10
prerequisites:
- OnceCallback 前置知识速查：C++11/14/17 核心特性回顾
- OnceCallback 前置知识（一）：函数类型与模板偏特化
related:
- OnceCallback 实战（三）：bind_once 实现
- OnceCallback 实战（五）：then 链式组合
translation:
  source: documents/vol9-open-source-project-learn/chrome/01_once_callback/full/pre-02-once-callback-invoke-and-callable.md
  source_hash: 138cb6dff4c2a4b0cbec4c16902d1d2a1375c8acf794d66bcd43ff239d6f55c0
  translated_at: '2026-05-26T12:27:42.281653+00:00'
  engine: anthropic
  token_count: 1605
---
# OnceCallback Prerequisites (Part 2): `std::invoke` and the Uniform Calling Convention

## Introduction

Suppose you are writing a callback system—just like the `OnceCallback` we are building. Your system needs to accept various kinds of "callable objects": plain function pointers, lambdas, functors (class objects with an overloaded `operator()`), and even member function pointers. The problem is that the calling syntax for these callable objects differs. Plain functions are called directly with `f(args)`, while member function pointers must be written as `(obj.*pmf)(args)`. If your code handles ten different callable objects, do you really need to write ten `if` branches to handle each one separately?

`std::invoke` (C++17) was born to eliminate this fragmentation. It provides a uniform calling syntax, allowing all callable objects to be invoked in the exact same way. Inside `OnceCallback`, both `bind_once` and `then()` rely entirely on it to fulfill the requirement of "correctly invoking whatever callable object is passed in."

> **Learning Objectives**
>
> - Understand why we need a uniform calling convention—the differences in calling syntax across various callable objects
> - Master the complete dispatch rules of `std::invoke`
> - Learn to use `std::invoke_result_t` to deduce the return type of a call at compile time

---

## The Problem: Fragmented Calling Syntax for Callable Objects

There are at least four common callable objects in C++, each with a different calling syntax. Let's look at them one by one.

### Plain Function Pointers

```cpp
int add(int a, int b) { return a + b; }
int (*fp)(int, int) = &add;

int result = fp(3, 4);       // 直接调用
int result2 = (*fp)(3, 4);   // 解引用后调用（等价）
```

### Lambdas / Functors

```cpp
auto lam = [](int a, int b) { return a + b; };
int result = lam(3, 4);  // 通过 operator() 调用

struct Adder {
    int operator()(int a, int b) { return a + b; }
};
Adder fn;
int result2 = fn(3, 4);  // 同样通过 operator() 调用
```

### Member Function Pointers

This is where the syntax starts getting weird. A member function pointer cannot be called directly like a plain function—you must have an object instance and use the `.*` or `->*` operators to invoke it.

```cpp
struct Calculator {
    int multiply(int a, int b) { return a * b; }
};

Calculator calc;
int (Calculator::*pmf)(int, int) = &Calculator::multiply;

// 必须用 .* 运算符
int result = (calc.*pmf)(3, 4);  // result == 12
```

### Pointers to Data Members

Yes, C++ allows you to take a "pointer" to a data member—which is really just an offset. You access it through the `.*` or `->*` operator as well.

```cpp
struct Point {
    double x, y;
};

Point p{1.0, 2.0};
double Point::*pmx = &Point::x;

double val = p.*pmx;  // val == 1.0
```

The problem is clear: if you are writing a template function that needs to invoke a "callable object of an unknown type," you cannot write a unified calling syntax—because you don't know whether it is a plain function or a member function pointer. `std::invoke` exists to solve this exact problem.

---

## The Dispatch Rules of `std::invoke`

The job of `std::invoke` is to select the correct calling syntax based on the specific types of `Callable` and `Args`. The standard defines the following cases (referred to as the INVOKE expression in C++ standard terminology):

### Case 1: Member Function Pointer + Object

When `Callable` is a pointer to a member function, and the first element of `Args` is an object (or a reference to an object, or a pointer to an object), `std::invoke` expands to invoking the member function through that object.

```cpp
struct Calculator {
    int multiply(int a, int b) { return a * b; }
};

Calculator calc;

// 通过引用
std::invoke(&Calculator::multiply, calc, 3, 4);        // (calc.*multiply)(3, 4)
// 通过指针
std::invoke(&Calculator::multiply, &calc, 3, 4);       // ((*ptr).*multiply)(3, 4)
```

Note the second case—when the first argument is a pointer (`ptr`), `std::invoke` automatically dereferences it. This behavior is crucial when `bind_once` binds a member function.

### Case 2: Pointer to Data Member + Object

When `Callable` is a pointer to a data member, `std::invoke` expands to accessing that data member through the object.

```cpp
struct Point { double x, y; };
Point p{1.0, 2.0};

double val = std::invoke(&Point::x, p);    // p.*&Point::x == p.x
```

### Case 3: Other Callable Objects

When `Callable` is a function pointer, a lambda, a functor, or anything else that "can be called directly," `std::invoke` simply performs `f(args...)`.

```cpp
std::invoke([](int a, int b) { return a + b; }, 3, 4);  // lambda(3, 4)
```

### The Unified Interface

The key point is that no matter which case `Callable` falls into above, the calling syntax is always `std::invoke(f, args...)`. In your template code, you do not need to know the exact type of `f`—`std::invoke` internally dispatches to the correct calling syntax for you.

---

## `std::invoke_result_t`: Deducing the Return Type at Compile Time

A unified calling syntax alone is not enough—sometimes you also need to know the return type of `std::invoke` at compile time. For example, in the implementation of `then()`, we need to deduce "what type is returned when we pass the return value of the previous callback to the next callback."

`std::invoke_result_t` does exactly this. Given a callable object type `F` and argument types `Args...`, it computes the return type of `std::invoke` at compile time.

```cpp
#include <type_traits>
#include <functional>

auto add(int a, int b) -> int { return a + b; }

// 编译期推导 add(1, 2) 的返回类型
using R = std::invoke_result_t<decltype(add), int, int>;
static_assert(std::is_same_v<R, int>);

// 对 lambda 也能推导
auto lam = [](double x) { return std::to_string(x); };
using R2 = std::invoke_result_t<decltype(lam), double>;
static_assert(std::is_same_v<R2, std::string>);
```

### Usage in `OnceCallback`

The implementation of `then()` uses `std::invoke_result_t` to deduce the return type of the new callback in the chain. Specifically, when `then()` accepts a subsequent callback `F`, it needs to know what type `F` will return:

```cpp
// 在 then() 的非 void 分支中
using NextRet = std::invoke_result_t<NextType, ReturnType>;
// NextRet 就是"把 ReturnType 类型的值传给 next，返回什么类型"
```

In the `void` branch, the subsequent callback takes no arguments:

```cpp
// 在 then() 的 void 分支中
using NextRet = std::invoke_result_t<NextType>;
// next 不接受参数，直接调用
```

---

## Specific Usage in the `OnceCallback` Source Code

Let's look at the actual source code to see the two usage scenarios of `std::invoke` in `OnceCallback`.

### `std::invoke` in `bind_once`

```cpp
// bind_once 的 lambda 内部
return std::invoke(
    std::move(f),
    std::move(bound)...,
    std::forward<decltype(call_args)>(call_args)...
);
```

Here, `f` could be any callable object—a plain lambda, a member function pointer, or even a pointer to a data member. If we wrote `f(args...)` directly instead of using `std::invoke`, it would fail to compile when `f` is a member function pointer—because a member function pointer cannot be called directly with `()`.

### `std::invoke` in `then()`

```cpp
// then() 的非 void 分支
auto mid = std::move(self).run(std::forward<FuncArgs>(args)...);
return std::invoke(std::move(cont), std::move(mid));
```

`F` (the subsequent callback) is a plain callable object (usually a lambda) in the design of `then()`, not a `OnceCallback`. So theoretically, calling it directly with `f(value)` would also work—and in most cases, it does. However, using `std::invoke` is a form of defensive programming: if someone passes a member function pointer as the subsequent callback, the direct calling syntax will fail, but `std::invoke` will not. Uniformly using `std::invoke` ensures correct behavior regardless of what callable object is passed in, without needing extra code to handle special types.

---

## Pitfall Warning: The Lifetime Trap of Member Function Binding

While `std::invoke` can uniformly handle member function pointers, it does not manage object lifetimes for you. When you bind a member function in `bind_once`:

```cpp
struct Calculator {
    int multiply(int a, int b) { return a * b; }
};

Calculator calc;
auto bound = bind_once<int(int)>(&Calculator::multiply, &calc, 5);
```

`obj` is a raw pointer, and the lambda will store it in its capture list. If `obj` is destroyed before the callback is invoked, the lambda holds a dangling pointer. `std::invoke` then accesses freed memory through that dangling pointer—undefined behavior (UB), most likely a segmentation fault.

Chromium uses `base::Unretained` to explicitly mark "I know this raw pointer's lifetime is safe," `base::Owned` to take ownership of the object, and `base::WeakPtr` to automatically cancel the callback when the object is destructed. Our simplified version does not provide these safety mechanisms for now—the responsibility for safety lies with the caller. This is an important design trade-off, and we will revisit it in the hands-on chapters.

---

## Summary

In this post, we clarified the origins and mechanics of `std::invoke`. The core motivation is that the calling syntax for various callable objects differs—plain functions are called directly with `f(args)`, member function pointers require `(obj.*pmf)(args)`, and pointers to data members require `obj.*pm`. `std::invoke` unifies all of these into a single `std::invoke(f, args...)` syntax, and paired with `std::invoke_result_t`, we can deduce the return type of the call at compile time. In `OnceCallback`, both `bind_once` and `then()` rely on it to achieve a generic design where "we don't care about the specific type of the callable object, as long as it can be invoked."

In the next post, we will look at advanced lambda features—specifically the lambda init capture pack expansion introduced in C++20, which is the key to the concise implementation of `bind_once`.

## Reference Resources

- [cppreference: std::invoke](https://en.cppreference.com/w/cpp/utility/functional/invoke)
- [cppreference: std::invoke_result](https://en.cppreference.com/w/cpp/types/result_of)
- [cppreference: Callable](https://en.cppreference.com/w/cpp/named_req/Callable)
