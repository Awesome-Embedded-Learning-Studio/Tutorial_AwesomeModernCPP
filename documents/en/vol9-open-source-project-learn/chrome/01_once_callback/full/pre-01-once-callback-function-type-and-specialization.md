---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: Gain a deep understanding of what the function type `int(int,int)` is,
  along with the template partial specialization techniques behind `OnceCallback<R(Args...)>`
  — how the compiler decomposes function signatures through pattern matching.
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
  engine: anthropic
  source: documents/vol9-open-source-project-learn/chrome/01_once_callback/full/pre-01-once-callback-function-type-and-specialization.md
  source_hash: c9b4c0d34cab59017adaa538c027529fb0e62ad5924b8f0d4f8f68c651e064e3
  token_count: 1509
  translated_at: '2026-05-26T12:27:27.491366+00:00'
---
# Prerequisites for OnceCallback (Part 1): Function Types and Template Partial Specialization

## Introduction

If this is your first time seeing the `OnceCallback<int(int, int)>` syntax, you'll probably find it a bit odd—`int(int, int)` looks like a function declaration, but it appears in a template parameter position. What exactly is this? How does the compiler break `int(int, int)` down into information like "returns int, takes two int parameters"?

In this post, we will break down this seemingly bizarre but actually elegant technique. Once you understand it, you will be able to make sense of why the template signatures of `std::function`, `std::move_only_function`, and our `OnceCallback` look the way they do.

> **Learning Objectives**
>
> - Understand that function types are valid types in C++
> - Master the "primary template + partial specialization" pattern, a recurring template design pattern
> - Be able to implement a minimal function signature decomposition tool yourself

---

## Function Types: An Easily Overlooked Type in C++

Let's start with a fundamental question: Is `int(int, int)` a type in C++?

The answer is yes. `int(int, int)` is something called a **function type**—it describes "a function that takes two int parameters and returns an int." Note that it is not a function pointer `int(*)(int, int)`, nor a function reference `int(&)(int, int)`—a function type is a lower-level concept than a function pointer.

We can use `static_assert` to verify this:

```cpp
#include <type_traits>

static_assert(std::is_function_v<int(int, int)>);           // 通过：是函数类型
static_assert(!std::is_pointer_v<int(int, int)>);           // 通过：不是指针
static_assert(std::is_pointer_v<int(*)(int, int)>);         // 通过：这是函数指针
```

Function types appear in actual code more often than you might think. When you write a function declaration:

```cpp
int add(int a, int b);
```

The type of `add` is `int(int, int)`. You can think of it as a "signature"—it fully describes what parameters the function accepts and what type it returns, without concerning itself with where the function itself is stored.

There is an implicit conversion between function types and function pointers: in most expressions, a function name automatically decays into a pointer to itself. This is just like an array name decaying into a pointer—`arr` in `int arr[5]` becomes `int*` in most contexts, and `add` in `int add(int, int)` becomes `int(*)(int, int)`.

However, when passed as a **template argument**, the function type does not decay—the compiler receives the type exactly as-is. This is the very prerequisite that allows us to decompose it using template partial specialization.

---

## Primary Template + Partial Specialization: The Pattern for Decomposing Function Types

Now let's look at how the template declaration for `OnceCallback` is written. It uses a two-step design: first, it declares a primary template that accepts only one type parameter, and then it provides a partially specialized version for the case where "that type parameter happens to be a function type."

### Step 1: Primary Template Declaration

```cpp
template<typename FuncSignature>
class OnceCallback;  // 主模板：只有声明，没有定义
```

The primary template intentionally provides no implementation. This is not an oversight, but a design choice—if someone accidentally writes a usage like `OnceCallback<int>` (passing a plain int type instead of a function signature), the compiler will error during template instantiation because it cannot find a definition. This acts as a compile-time safety net.

### Step 2: Partially Specialized Version

```cpp
template<typename ReturnType, typename... FuncArgs>
class OnceCallback<ReturnType(FuncArgs...)> {
    // 所有真正的代码都在这里
};
```

The template parameter list of this partially specialized version is `<typename ReturnType, typename... FuncArgs>`, while the `OnceCallback<ReturnType(FuncArgs...)>` following the class name is the **pattern matching condition** for the partial specialization—it says: "When `FuncSignature` can be decomposed into the form `ReturnType(FuncArgs...)`, use this version."

### The Compiler's Matching Process

When you write `OnceCallback<int(int, int)>`, the compiler does the following:

First, it sees you are instantiating `OnceCallback` with the template argument `int(int, int)`. Then it looks at the primary template `template<typename FuncSignature> class OnceCallback` and binds `FuncSignature` to the overall type `int(int, int)`. Next, it checks if a partial specialization can be used—the partial specialization requires `FuncSignature` to match the pattern `ReturnType(FuncArgs...)`. `int(int, int)` can be exactly decomposed into `ReturnType = int` and `FuncArgs = {int, int}`, so the match succeeds! The partially specialized version is selected.

You can think of this process as pattern matching at the type level—just as the regular expression `(\w+)\((\w+(?:,\s*\w+)*)\)` can extract the return value and parameter list from the string `int(int, int)`, template partial specialization extracts the return type and parameter pack from the type `int(int, int)`.

### It Uses the Exact Same Technique as `std::function`

If you look at the standard library implementation of `std::function`, you will find it uses the exact same pattern:

```cpp
// std::function 的简化实现
template<typename> class function; // 主模板

template<typename R, typename... Args>
class function<R(Args...)> {        // 偏特化
    // ...
};
```

`std::move_only_function` (C++23) is the same. This "primary template + function type partial specialization" pattern appears three times in the standard library, making it a thoroughly validated design.

---

## Hands-on Practice: Implementing a FuncTraits

Reading without practicing is easy to forget. Let's implement a minimal function signature decomposition tool ourselves to solidify our understanding. The goal is: given a function type `R(Args...)`, we can extract the return type `R` and the parameter pack `Args...`.

```cpp
#include <type_traits>

// 主模板：对非函数类型不提供定义
template<typename T>
struct FuncTraits;

// 偏特化：拆解函数类型 R(Args...)
template<typename R, typename... Args>
struct FuncTraits<R(Args...)> {
    using ReturnType = R;
    using ArgsTuple = std::tuple<Args...>;

    static constexpr std::size_t kArity = sizeof...(Args);
};

// 验证
static_assert(std::is_same_v<FuncTraits<int(double, char)>::ReturnType, int>);
static_assert(std::is_same_v<FuncTraits<void()>::ReturnType, void>);
static_assert(FuncTraits<int(int, int, int)>::kArity == 3);
```

`FuncTraits` and `OnceCallback` use the exact same partial specialization pattern. The only difference is that `FuncTraits` stores the decomposed types as the `using` alias and the `static constexpr` constant, while `OnceCallback` directly uses these types inside the partially specialized class to define data members and methods.

Try compiling and running this example—if all `static_assert` pass (with no compilation errors), it means the partial specialization correctly decomposed the function type. You can try adding some more complex types to test:

```cpp
// 更复杂的验证
static_assert(std::is_same_v<
    FuncTraits<std::string(const std::string&, int)>::ReturnType,
    std::string>);
static_assert(std::is_same_v<
    FuncTraits<void(int&&)>::ArgsTuple,
    std::tuple<int&&>>);
```

---

## Why Not Use OnceCallback<R, Args...>?

You might wonder: since the goal is to get the return type and parameter list, why not just write it in the form `OnceCallback<R, Args...>`? Like this:

```cpp
template<typename R, typename... Args>
class OnceCallback {
    // ...
};

// 使用：OnceCallback<int, int, int> cb([](int a, int b) { return a + b; });
```

This approach is technically feasible, but the user experience takes a hit. Let's compare the two ways of calling it:

```cpp
// 签名式：一个模板参数，看起来像函数签名
OnceCallback<int(int, int)> cb1([](int a, int b) { return a + b; });

// 参数罗列式：返回类型和参数分开写
OnceCallback<int, int, int> cb2([](int a, int b) { return a + b; });
```

The first one is more natural—`int(int, int)` is a complete function signature, and it reads clearly at a glance. The second requires you to mentally interpret the first `int` as the return type and the subsequent `int, int` as the parameter list, which increases cognitive load. The standard library also chose the signature style—`std::function<int(int, int)>` rather than `std::function<int, int, int>`.

The signature style has another subtle benefit: it is more consistent with the C++ type system. `int(int, int)` is a real type, whereas "a return type plus a set of parameter types" is not a type—it is just a listing of several types. Using a function type as a template parameter operates at the level of the type system, rather than at the level of syntactic sugar.

Of course, the signature style also has a drawback—the compiler cannot automatically deduce the complete signature from a callable object. This is why the first template parameter `Signature` of `bind_once` must be manually specified. We will discuss this trade-off in detail in the upcoming `bind_once` implementation post.

---

## Summary

In this post, we clarified three things. A function type `int(int, int)` is a valid type in C++ that fully describes a function's signature; it is neither a function pointer nor a function reference. The "primary template + partial specialization" pattern decomposes a function type into a return type and a parameter pack through pattern matching, and `std::function`, `std::move_only_function`, and our `OnceCallback` all use this same technique. The signature style `OnceCallback<R(Args...)>` is more natural and better aligned with the design philosophy of the C++ type system than the parameter-list style `OnceCallback<R, Args...>`.

In the next post, we will look at `std::invoke`—the key tool that enables `bind_once` to uniformly handle function pointers, member function pointers, and lambdas.

## References

- [cppreference: Function types](https://en.cppreference.com/w/cpp/language/function)
- [cppreference: Template partial specialization](https://en.cppreference.com/w/cpp/language/template_specialization)
- [cppreference: std::is_function](https://en.cppreference.com/w/cpp/types/is_function)
