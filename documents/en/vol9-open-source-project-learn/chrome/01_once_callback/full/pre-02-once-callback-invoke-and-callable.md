---
chapter: 0
cpp_standard:
- 17
description: Gain a deep understanding of how `std::invoke` unifies the calling conventions
  for function pointers, member function pointers, lambda expressions, and functors,
  as well as the role of `std::invoke_result_t` in type deduction within `OnceCallback`.
difficulty: intermediate
order: 2
platform: host
prerequisites:
- OnceCallback 前置知识速查：C++11/14/17 核心特性回顾
- OnceCallback 前置知识（一）：函数类型与模板偏特化
reading_time_minutes: 8
related:
- OnceCallback 实战（三）：bind_once 实现
- OnceCallback 实战（五）：then 链式组合
tags:
- host
- cpp-modern
- intermediate
- 函数对象
- std_invoke
title: 'OnceCallback Prerequisites (Part 2): std::invoke and the Uniform Calling Convention'
translation:
  source: documents/vol9-open-source-project-learn/chrome/01_once_callback/full/pre-02-once-callback-invoke-and-callable.md
  source_hash: 6565cf61f22ee2e502e620b0d180df665832123aeb4edcf9cb0555f57898ff12
  translated_at: '2026-06-16T04:13:39.844416+00:00'
  engine: anthropic
  token_count: 1598
---
# OnceCallback Prerequisites (Part 2): std::invoke and the Uniform Calling Convention

## Introduction

Suppose you are writing a callback system—just like the OnceCallback we are building. Your system needs to accept various "callable objects": ordinary function pointers, lambdas, functors (class objects with overloaded `operator()`), and even member function pointers. The problem is that the calling syntax for these callable objects varies. Ordinary functions are called directly via `func()`, while member function pointers must be written as `(obj.*func)()`. If your code contains ten different callable objects, do you really need to write ten `if` branches to handle them separately?

`std::invoke` (C++17) was born to eliminate this fragmentation. It provides a uniform calling syntax, allowing all callable objects to be invoked in the same way. OnceCallback's `bind_once` and `then()` methods rely entirely on it to achieve the requirement of "correctly calling whatever callable object is passed in."

> **Learning Objectives**
>
> - Understand why a uniform calling convention is needed—the differences in calling syntax for various callable objects.
> - Master the complete dispatch rules of `std::invoke`.
> - Learn to use `std::invoke_result_t` to deduce the return type of a call at compile time.

---

## Problem: The Fragmentation of Callable Object Syntax

In C++, there are at least four common callable objects, each with its own calling syntax. Let's examine them one by one.

### Ordinary Function Pointers

```cpp
void free_func(int x) { /* ... */ }

void (*ptr)(int) = &free_func;
ptr(42);        // Direct call
```

### Lambda / Functor

```cpp
auto lambda = [](int x) { /* ... */ };
lambda(42);    // Direct call
```

### Member Function Pointers

Here, the syntax starts to get weird. Member function pointers cannot be called directly like ordinary functions—you must have an object instance and use the `.*` or `->*` operators to invoke them.

```cpp
struct Widget {
    void func(int x);
};

void (Widget::*mem_ptr)(int) = &Widget::func;

Widget w;
(w.*mem_ptr)(42);  // Call via object
```

### Pointer to Data Member

Yes, C++ allows you to get a "pointer" to a data member—it's actually an offset. Access is also done via the `.*` operator.

```cpp
struct Widget {
    int value;
};

int Widget::*mem_ptr = &Widget::value;

Widget w;
w.*mem_ptr = 42;    // Access via object
```

The problem is clear: if you are writing a template function that needs to call a "callable object of an unknown type," you cannot write a single calling syntax—because you don't know if it's an ordinary function or a member function pointer. `std::invoke` is the solution to this problem.

---

## Dispatch Rules of std::invoke

The job of `std::invoke` is to select the correct calling syntax based on the specific types of the callable object and the arguments. The standard defines the following scenarios (referred to in the C++ standard as INVOKE expressions):

### Case 1: Member Function Pointer + Object

When `Callable` is a pointer to a member function, and the first element of `Args` is an object (or a reference to an object, or a pointer to an object), `std::invoke` expands to calling the member function via the object.

```cpp
struct Widget {
    void func(int);
};

Widget w;
Widget* ptr = &w;
void (Widget::*mem_func)(int) = &Widget::func;

// std::invoke handles both reference and pointer automatically
std::invoke(mem_func, w, 10);      // Equivalent to (w.*mem_func)(10)
std::invoke(mem_func, ptr, 10);   // Equivalent to ((*ptr).*mem_func)(10)
```

Note the second case—when the first argument is a pointer (`ptr`), `std::invoke` automatically dereferences the pointer. This behavior is crucial when binding member functions in `bind_once`.

### Case 2: Pointer to Data Member + Object

When `Callable` is a pointer to a data member, `std::invoke` expands to accessing the data member via the object.

```cpp
struct Widget {
    int value;
};

Widget w;
int Widget::*mem_data = &Widget::value;

// Returns a reference to w.value
int& res = std::invoke(mem_data, w);
```

### Case 3: Other Callable Objects

When `Callable` is a function pointer, lambda, functor, or other "directly callable thing," `std::invoke` is simply `Callable(Args...)`.

```cpp
auto lambda = [](int x) { return x + 1; };
std::invoke(lambda, 42);  // Equivalent to lambda(42)
```

### Unified Interface

The key is that no matter which of the above cases `Callable` falls into, the calling syntax is always `std::invoke(Callable, Args...)`. In your template code, you don't need to know the specific type of `Callable`—`std::invoke` internally dispatches to the correct calling syntax for you.

---

## std::invoke_result_t: Compile-Time Return Type Deduction

Uniform calling alone isn't enough—sometimes you also need to know at compile time what the return type of `std::invoke` will be. For example, in the implementation of `then()`, we need to deduce "what type is returned when passing the previous callback's return value to the next callback."

`std::invoke_result_t` is designed for this. Given a callable object type `Callable` and argument types `Args`, it calculates the return type of `Callable(Args...)` at compile time.

```cpp
template<typename Callable, typename... Args>
using return_type_t = typename std::invoke_result_t<Callable, Args...>;
```

### Usage in OnceCallback

The implementation of `then()` uses `std::invoke_result_t` to deduce the return type of the new callback in the chain. Specifically, when `then()` accepts a subsequent callback `NextCallback`, it needs to know what type `NextCallback` will return:

```cpp
// Deduce the return type of the subsequent callback
using NextRet = std::invoke_result_t<NextCallback, PrevRet>;
```

In the `void` branch, the subsequent callback accepts no arguments:

```cpp
using NextRet = std::invoke_result_t<NextCallback>;
```

---

## Specific Usage in OnceCallback Source Code

Let's look at the actual source code to see the two usage scenarios of `std::invoke` in OnceCallback.

### std::invoke in bind_once

```cpp
template <typename F, typename... Args>
OnceCallback bind_once(F&& f, Args&&... args) {
    return [f = std::forward<F>(f), ...args = std::forward<Args>(args)]() mutable {
        std::invoke(f, args...); // <--- Uniform call here
    };
}
```

Here, `f` can be any callable object—an ordinary lambda, a member function pointer, or even a pointer to a data member. If we didn't use `std::invoke` and wrote `f(args...)` directly, compilation would fail when `f` is a member function pointer—because member function pointers cannot be called directly with `operator()`.

### std::invoke in then()

```cpp
template <typename NextCallback>
auto then(NextCallback&& next) && {
    // ... (implementation details)
    next(std::move(prev_ret)); // <--- Uniform call here
}
```

`next` (the subsequent callback) is designed as an ordinary callable object (usually a lambda) in `then()`, not a `OnceCallback`. So theoretically, a direct `next(...)` would work—and in most cases, it does. However, using `std::invoke` is a form of defensive programming: if someone passes a member function pointer as the subsequent callback, the direct call syntax fails, but `std::invoke` won't. Uniformly using `std::invoke` ensures that whatever callable object is passed will work correctly without extra code to handle special types.

---

## Trap Warning: The Lifetime Trap of Member Function Binding

While `std::invoke` can uniformly handle member function pointers, it doesn't manage object lifetimes for you. When you bind a member function in `bind_once`:

```cpp
class Manager {
 public:
  void OnData(int) { /* ... */ }
};

Manager* mgr = new Manager;
// Capture raw pointer
auto cb = base::bind_once(&Manager::OnData, mgr, 42);
```

`mgr` is a raw pointer, and `bind_once` stores it in the lambda's capture list. If `mgr` is destroyed before the callback is invoked, the lambda holds a dangling pointer. `std::invoke` accessing freed memory through a dangling pointer results in undefined behavior, likely causing a segmentation fault.

Chromium uses `base::Unretained` to explicitly mark "I know this raw pointer's lifetime is safe," `base::Owned` to take ownership of the object, and `base::WeakPtr` to automatically cancel the callback when the object is destroyed. Our simplified version doesn't provide these protection mechanisms yet—safety is the caller's responsibility. This is an important design trade-off that we will mention again in the practical section.

---

## Summary

In this post, we clarified the context of `std::invoke`. The core motivation is that the calling syntax for various callable objects differs—ordinary functions are called directly via `()`, member function pointers require `.*`, and data member pointers require `.*` as well. `std::invoke` unifies all of these into a single syntax: `std::invoke(Callable, Args...)`. Combined with `std::invoke_result_t`, we can deduce the return type of a call at compile time. In OnceCallback, both `bind_once` and `then()` rely on it to achieve a generic design that "doesn't care about the specific type of the callable object, as long as it can be called."

In the next post, we will look at advanced Lambda features—specifically the lambda init capture pack expansion introduced in C++20, which is the key to the concise implementation of `bind_once`.

## Reference Resources

- [cppreference: std::invoke](https://en.cppreference.com/w/cpp/utility/functional/invoke)
- [cppreference: std::invoke_result](https://en.cppreference.com/w/cpp/types/result_of)
- [cppreference: Callable](https://en.cppreference.com/w/cpp/named_req/Callable)
