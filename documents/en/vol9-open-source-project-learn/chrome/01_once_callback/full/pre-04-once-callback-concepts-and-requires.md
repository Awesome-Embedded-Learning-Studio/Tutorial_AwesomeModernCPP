---
title: 'Prerequisites for OnceCallback (Part 4): Concepts and requires Constraints'
description: Starting from the real problem of move constructors being hijacked by
  template constructors, understand how Concepts and requires constraints protect
  the constructors of OnceCallback from being incorrectly matched.
chapter: 0
order: 4
tags:
- host
- cpp-modern
- intermediate
- concepts
- 模板
difficulty: intermediate
platform: host
cpp_standard:
- 20
reading_time_minutes: 11
prerequisites:
- OnceCallback 前置知识速查：C++11/14/17 核心特性回顾
- OnceCallback 前置知识（一）：函数类型与模板偏特化
related:
- OnceCallback 实战（二）：核心骨架搭建
- OnceCallback 前置知识（五）：std::move_only_function
---
# Prerequisite Knowledge for OnceCallback (Part 4): Concepts and requires Constraints

## Introduction

The constructor of `OnceCallback` has this seemingly redundant constraint:

```cpp
template<typename Functor>
    requires not_the_same_t<Functor, OnceCallback>
explicit OnceCallback(Functor&& function);
```

You might ask—why not just write `template <typename F>` and call it a day? What is the extra `requires` guarding against?

In this post, we answer that question. The answer involves a lesser-known pitfall in C++ overload resolution: **a template constructor can hijack move constructor calls in certain situations**. Concepts and `requires` constraints are the defensive weapons C++20 gives us.

> **Learning Objectives**
>
> - Understand the overload competition between template constructors and move constructors
> - Master the basic syntax of concepts and the usage of `requires` clauses
> - Be able to interpret the design intent of `not_the_same_t` and the meaning of each line of code

---

## Introducing the Problem: The Template Constructor "Offside"

### Reconstructing the Scenario

Suppose we have a simple wrapper class that accepts any callable object:

```cpp
template<typename FuncSignature>
class Callback;

template<typename R, typename... Args>
class Callback<R(Args...)> {
public:
    // 模板构造函数：接受任意可调用对象
    template<typename Functor>
    explicit Callback(Functor&& f) {
        // 用 f 初始化内部存储...
    }

    // 编译器隐式生成的移动构造函数
    // Callback(Callback&& other) noexcept;
};
```

Now we write `Wrapper w2 = std::move(w1);`—our intent is obvious: we want to call the move constructor. The compiler has two paths:

1. The implicitly generated move constructor `Wrapper(Wrapper&&)`
2. The instantiated template constructor `Wrapper<F>(F&&)` (where `F = Wrapper`)

Intuitively, we might feel the move constructor should win—after all, it is "specifically designed for this type." But C++ overload resolution rules are not that simple. In some cases, a function signature instantiated from a template is a "more exact" match than an implicitly declared special member function—because the template parameter `F` can perfectly match the type of the passed argument (including `Wrapper&&`), whereas the move constructor's parameter type is fixed as `Wrapper&&`.

When two overloads are equally good matches, C++ rules dictate that **non-template functions are preferred over template functions**. So in most cases, the move constructor does win. But edge cases are subtle—especially when forwarding references and exact matches are involved, where some compiler versions might behave differently. More critically, even if the move constructor wins, if the template constructor is also in the candidate list, certain SFINAE scenarios can lead to unexpected compilation errors.

### Minimal Reproduction

```cpp
struct Wrapper {
    // 模板构造函数：接受任何类型
    template<typename T>
    Wrapper(T&& x) {
        std::cout << "template constructor\n";
    }

    // 移动构造函数（编译器隐式生成或显式声明）
    Wrapper(Wrapper&& other) noexcept {
        std::cout << "move constructor\n";
    }
};

Wrapper a;
Wrapper b = std::move(a);  // 你期望输出 "move constructor"
                            // 在某些情况下可能输出 "template constructor"
```

The solution is to add a constraint to the template constructor—making it **not** match the `Wrapper` type itself.

---

## Concept Basic Syntax

C++20 introduced Concepts—a mechanism for naming constraints. You can think of a concept as a "named compile-time Boolean condition." If that feels hard to grasp—the author believes that a concept is exactly what its name suggests: it is a concept. Compared to the obscure way we used to express things with `enable_if`, we can now more easily state what something is—it is XXX, and XXX is a concept. It is that simple.

### Declaring a Concept

```cpp
template<typename T>
concept Integral = std::is_integral_v<T>;
```

`Integral` is a concept that checks whether `T` is an integer type. `std::is_integral_v<T>` is a compile-time Boolean constant. What we express here is very simple—we just want an integer type! Armed with this concept, we can use it with `requires` in the next step.

### Using requires Clauses

A `requires` clause can be placed after a template declaration to constrain template parameters to satisfy a certain condition:

```cpp
template<typename T>
    requires Integral<T>
void foo(T x) {
    // 只有 T 是整数类型时，这个函数才会被实例化
}

foo(42);    // OK：int 是整数
foo(3.14);  // 编译错误：double 不满足 Integral
```

### Common Standard Library Concepts

C++20 provides a batch of predefined concepts in the `<concepts>` header file:

```cpp
#include <concepts>

// std::invocable<F, Args...>：F 是否可以用 Args... 调用
static_assert(std::invocable<int(*)(int), int>);

// std::same_as<A, B>：A 和 B 是否是同一类型
static_assert(std::same_as<int, int>);

// std::convertible_to<From, To>：From 是否能隐式转换到 To
static_assert(std::convertible_to<int, double>);
```

---

## not_the_same_t: Line-by-Line Breakdown

Now let us look at this concept in `OnceCallback`:

```cpp
template<typename F, typename T>
concept not_the_same_t = !std::is_same_v<std::decay_t<F>, T>;
```

What it does in one sentence is: **the decayed type of F is not T**. We break down the three key components one by one.

### std::decay_t\<F\>: Decaying References and cv-Qualifiers

`std::decay_t` does three things to a type: removes references (`int&` → `int`), removes top-level const/volatile (`const int` → `int`), and decays array and function types (`int[5]` → `int*`, `void(int)` → `void(*)(int)`).

In the `OnceCallback` scenario, the most critical part is removing references. When we write `OnceCallback(cb)`, `F` is deduced as `CallableType` (not `CallableType&&`, because the deduction rules for forwarding references deduce rvalues as non-reference types). But if it is `OnceCallback(cb_lvalue)` (even though copy is deleted, this is just an example), `F` would be deduced as `OnceCallback&`. `std::decay_t` guarantees that no matter what reference form `F` deduces to, after decay it is always `OnceCallback`, which we then compare against `T`.

### std::is_same_v<...>: Comparing Two Types

`std::is_same_v<A, B>` returns `true` when `A` and `B` are exactly the same type. Note that "exactly the same" is very strict—`int` and `const int` are different, and `int&` and `int` are also different. This is why we need `std::decay_t` to unify the form first.

### Negation `!`: The Constraint Passes When F Is Not T

The value of the entire concept is `!std::is_same_v<...>`—the negation means that when the decayed `F` is the same as `T`, the constraint fails (the template is excluded), and when they are different, the constraint passes (the template participates in overload resolution).

### Effect After Adding the Constraint

```cpp
template<typename Functor>
    requires not_the_same_t<Functor, OnceCallback>
explicit OnceCallback(Functor&& f) : status_(Status::kValid), func_(std::move(f)) {}
```

When what is passed in is the `OnceCallback` itself (such as in a move construction scenario), `not_the_same_t<F, T>` evaluates to `false`, the constraint is not satisfied, the template is excluded from the candidate list, and the compiler can only choose the move constructor. When what is passed in is a lambda, a function pointer, or other types, the constraint is satisfied, the template normally participates in overload resolution, and is selected as the constructor.

---

## Application of This Pattern in the Standard Library

This is not just a special requirement for `OnceCallback`. `std::function`'s own implementation has an almost identical constraint—except that the standard library uses the standard concept `std::invocable` paired with `!std::same_as`. Any move-only type-erased wrapper needs this defense—as long as your class simultaneously has "a template constructor that accepts any type" and "a compiler-generated move constructor," you must add a constraint to prevent the two from competing.

```text
模式总结：
模板构造函数 + requires 排除自身类型 = 保护移动语义的正确匹配
```

If you write similar components in the future—such as your own `unique_function`, `move_only_function`, or other move-only wrappers—remember this pattern; it is a general defensive technique.

---

## Pitfall Warnings

### If You Forget std::decay_t

If you only write `std::is_same_v` without adding `std::decay_t`, the problem is that the deduced result of `F` might or might not carry a reference, depending on the calling context. Consider the following scenario:

```cpp
OnceCallback cb1([](int x) { return x; });

// 场景 A：std::move(cb1) 是右值
// Functor 推导为 OnceCallback（不带引用）
// is_same_v<OnceCallback, OnceCallback> == true → 约束失败 ✓ 正确

// 场景 B：const OnceCallback& ref = cb1;
// 如果有人写了 OnceCallback cb2(ref);
// Functor 推导为 const OnceCallback&
// is_same_v<const OnceCallback&, OnceCallback> == false → 约束通过 ✗ 错误！
```

In scenario B, without `std::decay_t`, `F` and `T` are not the same, the constraint passes, and the template constructor is selected—but semantically we expect either a compilation error (copy is deleted) or at least not the template constructor. After adding `std::decay_t`, `F` decays to `OnceCallback`, which is the same as `T`, and the constraint correctly fails.

### The static_assert(false) Trap

Prior to C++23, `static_assert(false)` in a template would cause all instantiations to trigger the assertion failure—even if this template is never called. This is because the C++ standard prior to C++23 required `static_assert` to be immediately evaluated at template definition time. Chromium uses `static_assert(sizeof(F) == 0)` to work around this limitation (`sizeof(F)` is never zero, but it depends on the type of `F`, so it is a dependent expression and is not evaluated at definition time). C++23 relaxed this rule, but if you compile with C++20, you still need to be aware of this issue.

---

## Summary

In this post, we clarified that seemingly redundant `requires` constraint on the `OnceCallback` constructor. Its purpose is to prevent the template constructor from hijacking move constructor calls in scenarios like `OnceCallback cb2 = std::move(cb1);`. `not_the_same_t` uses `std::decay_t` to strip references and const qualifiers from `F`, compares it against `T`, and negates the result to ensure the template is excluded when the type itself is passed in. This pattern is used in all move-only type-erased wrappers—`std::function` has a similar constraint.

In the next post, we will look at `std::move_only_function`—it is the core storage type for `OnceCallback`, and it is the key to us replacing Chromium's hand-written `BindState` with standard library facilities.

## Reference Resources

- [cppreference: Constraints and concepts](https://en.cppreference.com/w/cpp/language/constraints)
- [cppreference: std::decay](https://en.cppreference.com/w/cpp/types/decay)
- [Stack Overflow: Generic constructor template called instead of copy/move constructor](https://stackoverflow.com/questions/70267685/generic-constructor-template-called-instead-of-copy-move-constructor)
