---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: Deep dive into what the function type `int(int,int)` is, and the template
  partial specialization techniques behind `OnceCallback<R(Args...)>`—how the compiler
  deconstructs function signatures through pattern matching.
difficulty: intermediate
order: 1
platform: host
prerequisites:
- OnceCallback 前置知识速查：C++11/14/17 核心特性回顾
reading_time_minutes: 8
related:
- OnceCallback 前置知识（五）：std::move_only_function
- OnceCallback 实战（二）：核心骨架搭建
tags:
- host
- cpp-modern
- intermediate
- 模板
- 泛型
title: 'OnceCallback Prerequisites (Part 1): Function Types and Template Partial Specialization'
translation:
  source: documents/vol9-open-source-project-learn/chrome/01_once_callback/full/pre-01-once-callback-function-type-and-specialization.md
  source_hash: f8ae704aefb39b88443c304d6f73c02c766d7b1e1f2fb677bac6e6bbd05de009
  translated_at: '2026-06-16T04:13:36.480668+00:00'
  engine: anthropic
  token_count: 1503
---
# OnceCallback Prerequisites (Part 1): Function Types and Template Partial Specialization

## Introduction

If this is your first time seeing `OnceCallback<void(int, double)>`, you might find it a bit strange—`void(int, double)` looks like a function declaration, yet it appears in a template parameter position. What exactly is this thing? How does the compiler break down `int(int, int)` into information like "returns int, accepts two int parameters"?

In this post, we will break down this seemingly quirky but actually very elegant technique. Once you understand it, you will be able to see why the template signatures of `std::is_function`, `std::coroutine_traits`, and our `OnceCallback` look the way they do.

> **Learning Objectives**
>
> - Understand that a function type is a valid type in C++.
> - Master the recurring template design pattern of "primary template + partial specialization".
> - Be able to implement a minimal version of a function signature extraction tool.

---

## Function Types: An Easily Overlooked Type in C++

Let's start with a basic question: Is `int(int, int)` a type in C++?

The answer is: Yes. `int(int, int)` is something called a **function type**. It describes "a function that accepts two `int` parameters and returns an `int`". Note that it is not a function pointer `int(*)(int, int)`, nor a function reference `int(&)(int, int)`—the function type is a more fundamental concept than function pointers.

We can verify this with `using`:

```cpp
using Func = int(int, int); // Define Func as a function type
```

Function types appear in actual code more often than you might think. When you write a function declaration:

```cpp
int add(int a, int b);
```

The type of `add` is `int(int, int)`. You can think of it as a "signature"—it completely describes what parameters the function accepts and what type it returns, without involving where the function itself is stored.

There is an implicit conversion between function types and function pointers: in most expressions, a function name automatically decays into a pointer to itself. This is similar to how an array name decays into a pointer—`add` in `add(1, 2)` becomes a pointer in most contexts, just as `arr` in `arr[0]` becomes a pointer.

However, when passed as a **template argument**, the function type does not decay—the compiler receives this type exactly as is. This is the prerequisite that allows us to deconstruct it using template partial specialization.

---

## Primary Template + Partial Specialization: The Pattern for Deconstructing Function Types

Now let's look at how `std::is_function`'s template declaration is written. It uses a two-step design: first, declare a primary template that accepts only one type parameter, then provide a partially specialized version for the case where "that type parameter happens to be a function type".

### Step 1: Primary Template Declaration

```cpp
template <typename T>
struct std::is_function : std::false_type {};
```

The primary template intentionally provides no implementation. This isn't an oversight, but a design choice—if someone accidentally writes a usage like `is_function<int>` (passing a plain `int` type instead of a function signature), the compiler will error during instantiation because it can't find a definition. This is a compile-time safety net.

### Step 2: Partial Specialization Version

```cpp
template <typename Ret, typename... Args>
struct std::is_function<Ret(Args...)> : std::true_type {};
```

The template parameter list for this partial specialization is `template <typename Ret, typename... Args>`, while the `<Ret(Args...)>` following the class name is the **pattern matching condition** for the partial specialization. It says: "When `T` can be deconstructed into the form `Ret(Args...)`, use this version."

### The Compiler's Matching Process

When you write `is_function<int(int, int)>::value`, the compiler does the following:

First, it sees you are instantiating `is_function`, with the template argument being `int(int, int)`. Then it looks at the primary template `is_function<T>`, binding `T` to the `int(int, int)` type as a whole. Next, it checks if a partial specialization can be used—the partial specialization requires `T` to match the pattern `Ret(Args...)`. `int(int, int)` can恰好 be broken down into `Ret=int` and `Args=int, int`, so the match succeeds! The partial specialization is selected.

You can imagine this process as a type-level pattern matching—just like the regex `(\w+)\((\w+(?:,\s*\w+)*)\)` can extract return values and parameter lists from a string, template partial specialization extracts the return type and parameter pack from the type `int(int, int)`.

### Uses the Exact Same Technology as `std::coroutine_traits`

If you look at the standard library implementation of `std::coroutine_traits`, you will find it uses the exact same pattern:

```cpp
template <typename Ret, typename... Args>
struct coroutine_traits<Ret, Args...> {
    using promise_type = /* ... */;
};
```

`std::is_function` (C++23) is the same. This "primary template + function type partial specialization" pattern appears three times in the standard library and is a well-validated design.

---

## Hands-on Practice: Implementing a FuncTraits

Reading without practicing makes it hard to remember. Let's implement a minimal function signature extraction tool ourselves to solidify our understanding. The goal is: given a function type `F`, extract the return type `Ret` and the parameter pack `Args`.

```cpp
template <typename T>
struct FuncTraits; // Primary template, intentionally undefined

// Partial specialization for function types
template <typename Ret, typename... Args>
struct FuncTraits<Ret(Args...)> {
    using ReturnType = Ret;
    using ArgTypes = std::tuple<Args...>;
    static constexpr std::size_t ArgCount = sizeof...(Args);
};
```

`FuncTraits` and `OnceCallback` use the exact same partial specialization pattern. The only difference is that `FuncTraits` stores the extracted types as a `ReturnType` alias and an `ArgCount` constant, while `OnceCallback` directly uses these types inside the partial specialization class to define data members and methods.

Try compiling and running this example—if all `static_assert`s pass (no compilation errors), it means the partial specialization correctly deconstructed the function type. You can try adding some more complex types to test:

```cpp
// Complex function type: returns a pointer to a function taking double and returning int
using ComplexFunc = int(*)(double);
using TestType = ComplexFunc(double);

static_assert(std::is_same_v<FuncTraits<TestType>::ReturnType, ComplexFunc>);
static_assert(FuncTraits<TestType>::ArgCount == 1);
```

---

## Why Not Use `OnceCallback<R, Args...>`?

You might wonder, since the goal is to get the return type and parameter list, why not write it directly in the form `OnceCallback<R, Args...>`? Like this:

```cpp
template <typename R, typename... Args>
class OnceCallback {
    // ...
};
```

This approach is technically feasible, but the user experience is not as good. Let's compare the two ways of calling it:

```cpp
// Signature style (current approach)
OnceCallback<int(int, int)> cb1;

// Parameter list style (alternative approach)
OnceCallback<int, int, int> cb2;
```

The first one is more natural—`int(int, int)` is a complete function signature, which is clear at a glance. The second requires you to mentally interpret the first `int` as the return type and the subsequent `int, int` as the parameter list, which increases cognitive load. The standard library also chooses the signature style—`std::function<R(Args...)>` rather than `std::function<R, Args...>`.

The signature style also has a subtle benefit: it is more consistent with the C++ type system. `int(int, int)` is a real type, whereas "a return type plus a set of parameter types" is not a type—it is just a list of several types. Using a function type as a template parameter is operating at the level of the type system, not at the level of syntactic sugar.

Of course, the signature style has one drawback—the compiler cannot automatically deduce the complete signature from a callable object. This is why the first template parameter of `OnceCallback` must be specified manually. We will discuss this trade-off in detail in the upcoming `OnceCallback` implementation post.

---

## Summary

In this post, we figured out three things. The function type `int(int, int)` is a valid type in C++ that completely describes a function's signature; it is not a function pointer nor a function reference. The "primary template + partial specialization" pattern deconstructs the function type into a return type and a parameter pack through pattern matching. `std::is_function`, `std::coroutine_traits`, and our `OnceCallback` all use the same technique. The signature style `OnceCallback<R(Args...)>` is more natural and aligns better with C++ type system design philosophy than the parameter list style `OnceCallback<R, Args...>`.

In the next post, we will look at `std::mem_fn`—it is the key tool that allows `OnceCallback` to uniformly handle function pointers, member function pointers, and lambdas.

## Reference Resources

- [cppreference: Function type](https://en.cppreference.com/w/cpp/language/function)
- [cppreference: Template partial specialization](https://en.cppreference.com/w/cpp/language/template_specialization)
- [cppreference: std::is_function](https://en.cppreference.com/w/cpp/types/is_function)
