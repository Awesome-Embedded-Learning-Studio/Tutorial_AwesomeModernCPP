---
chapter: 0
cpp_standard:
- 20
description: We explore the real-world issue where template constructors hijack move
  constructors, and understand how Concepts and `requires` constraints ensure `OnceCallback`
  constructors match correctly.
difficulty: intermediate
order: 4
platform: host
prerequisites:
- OnceCallback 前置知识速查：C++11/14/17 核心特性回顾
- OnceCallback 前置知识（一）：函数类型与模板偏特化
reading_time_minutes: 9
related:
- OnceCallback 实战（二）：核心骨架搭建
- OnceCallback 前置知识（五）：std::move_only_function
tags:
- host
- cpp-modern
- intermediate
- concepts
- 模板
title: 'Prerequisites for OnceCallback (Part 4): Concepts and requires Constraints'
translation:
  source: documents/vol9-open-source-project-learn/chrome/01_once_callback/full/pre-04-once-callback-concepts-and-requires.md
  source_hash: 2d66382298f6e53f170ed119632aa7d08d3cce1abc9f95b1c2396e616e4d68f7
  translated_at: '2026-06-16T04:13:42.814016+00:00'
  engine: anthropic
  token_count: 1749
---
# OnceCallback Prerequisites (Part 4): Concepts and requires Constraints

## Introduction

The `OnceCallback` constructor has a constraint that looks somewhat redundant:

```cpp
template <typename F, typename = std::enable_if_t<
    not_the_same_t<F, OnceCallback>::value>>
OnceCallback(F&& f);
```

You might ask—why not just write `OnceCallback(F&& f)` and be done with it? What exactly is the extra constraint guarding against?

In this post, we will answer this question. The answer involves a lesser-known pitfall in C++ overload resolution: **template constructors can hijack move constructor calls in certain situations**. Concepts and `requires` constraints are the defensive weapons C++20 provides us.

> **Learning Objectives**
>
> - Understand the overload competition between template constructors and move constructors.
> - Master the basic syntax of concepts and the usage of the `requires` clause.
> - Be able to interpret the design intent of `not_the_same_t` and the meaning of every line of code.

---

## Problem Introduction: The "Offside" of Template Constructors

### Scenario Reconstruction

Assume we have a simple wrapper class that accepts any callable object:

```cpp
class Wrapper {
public:
    // Accepts any callable
    template <typename F>
    Wrapper(F&& f) : func(std::forward<F>(f)) {}

    // Move constructor (implicitly generated)
    Wrapper(Wrapper&&) noexcept = default;

    // Copy constructor (deleted)
    Wrapper(const Wrapper&) = delete;
private:
    std::function<void()> func;
};
```

Now we write `Wrapper w2(std::move(w1))`—the intent is obvious: we want to call the move constructor. The compiler has two paths:

1. The implicitly generated move constructor `Wrapper(Wrapper&&)`.
2. The template constructor instantiation `Wrapper<Wrapper>(Wrapper&&)` (where `F = Wrapper`).

Intuitively, we feel the move constructor should take priority—after all, it is "specifically designed for this type." However, C++ overload resolution rules are not that simple. In some cases, the function signature instantiated from a template is a "more exact" match than the implicitly declared special member function—because the template parameter `F` can perfectly match the type of the passed argument (including references), while the move constructor's parameter type is fixed `Wrapper&&`.

When the match quality of two overloads is identical, C++ rules dictate that **non-template functions take precedence over template functions**. So, in most cases, the move constructor does win. But edge cases are subtle—especially when forwarding references and perfect matching are involved, some compiler versions might behave differently. More critically, even if the move constructor wins, if the template constructor is also in the candidate list, certain SFINAE scenarios might lead to unexpected compilation errors.

### Minimal Reproduction

```cpp
#include <utility>

class Test {
public:
    template <typename T>
    Test(T&&) { /* Generic template */ }

    Test(Test&&) noexcept = default; // Move constructor
    Test(const Test&) = delete;      // Copy constructor
};

int main() {
    Test t1;
    // Which constructor is called?
    Test t2(std::move(t1));
}
```

The solution is to add a constraint to the template constructor—make it **not** match the class's own type.

---

## Concept Basic Syntax

C++20 introduced Concepts—a mechanism for naming constraints. You can think of a concept as a "named compile-time boolean condition." If that sounds hard to grasp—personally, I think "concept" lives up to its name: it literally means a concept. Compared to the obscure way we used to express things with `enable_if`, we can now say what it is more easily—it is XXX, and XXX is a concept. It's just that simple.

### Declaring a concept

```cpp
template <typename T>
concept Integral = std::is_integral_v<T>;
```

`Integral` is a concept that checks if `T` is an integer type. `std::is_integral_v<T>` is a compile-time boolean constant. The meaning here is simple—we just want an integer type! With this concept, we can use it in the next step with `requires`.

### Using the requires clause

The `requires` clause can be added after a template declaration to constrain template parameters to satisfy a specific condition:

```cpp
template <typename T>
requires Integral<T>
void foo(T value);
```

### Standard library common concepts

C++20 provides a batch of predefined concepts in the `<concepts>` header file:

```cpp
std::integral<T>       // T is an integral type
std::floating_point<T> // T is a floating point type
std::same_as<T, U>     // T and U are the same type
std::convertible_to<T, U> // T is convertible to U
```

---

## not_the_same_t: Line-by-Line Breakdown

Now let's look at this concept in `OnceCallback`:

```cpp
template <typename F, typename T>
concept not_the_same_t = !std::is_same_v<std::decay_t<F>, T>;
```

What it does, in one sentence, is: **The decayed type of F is not T**. Let's break down the three key components one by one.

### std::decay_t\<F\>: Decay references and cv-qualifiers

`std::decay_t<F>` does three things to a type: removes references (`int&` → `int`), removes top-level const/volatile (`const int` → `int`), and decays array and function types (`int[3]` → `int*`, `void(int)` → `void(*)(int)`).

In the `OnceCallback` scenario, the most critical part is removing references. When we write `OnceCallback(F&& f)`, `F` is deduced as `OnceCallback` (not `OnceCallback&&`, because forwarding reference deduction rules deduce rvalues as non-reference types). But if it were `OnceCallback(OnceCallback&)` (even though copy is deleted, this is just an example), `F` would be deduced as `OnceCallback&`. `std::decay_t` ensures that no matter what reference form `F` deduces to, after decay it is `OnceCallback`, which is compared with `T`.

### std::is_same_v<...>: Compare two types

`std::is_same_v<A, B>` returns `true` when `A` and `B` are identical. Note that "identical" is very strict—`int` and `const int` are different, `int&` and `int` are also different. That's why we need `std::decay_t` to unify the form first.

### Negation `!`: Constraint passes when F is not T

The value of the entire concept is `!std::is_same_v<...>`—negation means that when `F`'s decayed type is the same as `T`, the constraint fails (the template is excluded), and when they are different, the constraint passes (the template participates in overload resolution).

### Effect after adding the constraint

```cpp
template <typename F>
requires not_the_same_t<F, OnceCallback>
OnceCallback(F&& f);
```

When what is passed in is `OnceCallback` itself (like in a move constructor scenario), `not_the_same_t<F, OnceCallback>` evaluates to `false`, the constraint is not satisfied, and the template is removed from the candidate list. The compiler can only choose the move constructor. When a lambda, function pointer, or other type is passed, the constraint is satisfied, the template participates in overload resolution normally, and is selected as the constructor.

---

## Application of this Pattern in the Standard Library

This is not just a special requirement for `OnceCallback`. The standard library's own `std::function` implementation has almost identical constraints—except the standard library uses the standard concept `!std::same_as` combined with `std::type_identity`. Any move-only type-erasing wrapper needs this defense—as long as your class has both a "template constructor accepting any type" and a "compiler-generated move constructor", you must add a constraint to prevent competition between the two.

```cpp
// Standard library style (simplified)
template <typename F>
requires (!std::same_as<std::decay_t<F>, MyType>)
MyType(F&& f);
```

If you write similar components in the future—like your own `unique_function`, `move_only_function` or other move-only wrappers—remember this pattern; it is a general defensive measure.

---

## Pitfall Warning

### If you forget std::decay_t

If you only write `!std::is_same_v<F, T>` without adding `std::decay_t`, the problem is that the deduction result of `F` might carry a reference or might not, depending on the calling context. Consider the following scenario:

```cpp
// Scenario A: Move
OnceCallback cb1;
OnceCallback cb2(std::move(cb1)); // F deduced as OnceCallback

// Scenario B: Lvalue reference (hypothetically)
// OnceCallback cb3(cb1); // F deduced as OnceCallback&
```

In Scenario B, without `std::decay_t`, `F` (`OnceCallback&`) and `T` (`OnceCallback`) are not the same, the constraint passes, and the template constructor is selected—but semantically we expect a compilation error (copy is deleted) or at least not the template constructor. With `std::decay_t`, `F` decays to `OnceCallback`, which is the same as `T`, and the constraint correctly fails.

### The trap of static_assert(false)

Before C++23, `static_assert(false)` in a template causes all instantiations to trigger assertion failure—even if this template is never called. This is because the C++ standard prior to C++23 required `static_assert` to be evaluated immediately when the template is defined. Chromium uses `static_assert(sizeof(T) != 0)` to bypass this limit (`sizeof(T)` is never 0, but it depends on the type of `T`, so it is a type-dependent expression and won't be evaluated at definition time). C++23 relaxed this rule, but if you compile with C++20, you still need to be aware of this issue.

---

## Summary

In this post, we cleared up the seemingly redundant `requires not_the_same_t<F, OnceCallback>` constraint on the `OnceCallback` constructor. Its existence is to prevent the template constructor from hijacking move constructor calls in scenarios like `OnceCallback cb2(std::move(cb1))`. `not_the_same_t` uses `std::decay_t` to strip references and const qualifiers from `F` before comparing with `T`, and the negation ensures the template is excluded when passing its own type. This pattern is used in all move-only type-erasing wrappers—`std::function` has similar constraints.

In the next post, we will look at `std::move_only_function`—it is the core storage type of `OnceCallback` and the key to us using standard library facilities to replace Chromium's hand-written `BindState`.

## Reference Resources

- [cppreference: Constraints and concepts](https://en.cppreference.com/w/cpp/language/constraints)
- [cppreference: std::decay](https://en.cppreference.com/w/cpp/types/decay)
- [Stack Overflow: Generic constructor template called instead of copy/move constructor](https://stackoverflow.com/questions/70267685/generic-constructor-template-called-instead-of-copy-move-constructor)
