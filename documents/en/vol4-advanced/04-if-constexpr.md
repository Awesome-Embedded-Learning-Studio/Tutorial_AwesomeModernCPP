---
chapter: 4
cpp_standard:
- 17
- 20
description: Detailed Explanation of if constexpr Applications
difficulty: intermediate
order: 4
platform: host
prerequisites:
- 'Chapter 2: 零开支抽象'
reading_time_minutes: 4
tags:
- cpp-modern
- host
- intermediate
title: compile-time condition with if constexpr
---
# `if constexpr`: Making Compile-Time Branches Read Like Comments — A Practical Engineering Guide

I have always believed that between modern C++ and classical C++98, most template programming techniques are written to combine for specific purposes, and this complexity is not always what we want. For example, in the template programming we will study later, many template-dependent `enable_if`, specializations, and SFINAE tricks are essentially just to achieve specific compile-time matching. Fortunately, we now have `if constexpr` to simplify the vast majority of these scenarios.

`if constexpr` is a small yet powerful tool brought by C++17: it hands "branches that we can decide at compile time" directly to the compiler. The result is cleaner code and more readable templates, both of which can be simplified into a crisp `if constexpr`.

------

## Why Should We Care

- **Reduce the number of template specializations/overloads**: We can place behavior for different types within the same template body, forking via compile-time conditions. This centralizes the logic and makes maintenance easier.
- **Improve readability**: When reading code, seeing `if constexpr` immediately tells us this is a "type/constant-driven" branch, eliminating the need to hunt down other specialization implementations.
- **Avoid unnecessary instantiation errors**: Discarded branches are not instantiated, so expressions that are invalid for a certain type will not cause compilation failures.
- **Clear performance**: The compiler can eliminate unneeded branches at compile time, and the generated code is just as efficient as if we had written multiple specializations.

------

## How to Use `if constexpr (cond)`?

1. `if constexpr (cond)` requires `cond` to be determinable at compile time (a constant expression).
2. If `cond` is `true`, the compiler only compiles the `then` branch. The `else` (if present) is discarded and does not participate in template instantiation (therefore, it may contain code that is illegal for the current type).
3. The reverse applies as well.
4. If `cond` is not a compile-time constant, an error will occur (because the semantics of `if constexpr` require compile-time evaluation). (PS: It can now sometimes gradually degrade to runtime evaluation; this is a newer feature, so behavior varies slightly across different versions.)

------

## How Does It Differ from a Regular `if`?

A regular `if` is a runtime check, and both branches must be valid. `if constexpr` is a compile-time check, and only the selected branch needs to be valid (this is especially important for template-dependent conditions).

------

### Case: Selecting an Implementation Based on Type

Scenario: Printing values of different types (commonly used in engineering for log formatting, serialization branching, etc.).

```cpp
#include <type_traits>
#include <iostream>

template <typename T>
void printValue(const T& v) {
    if constexpr (std::is_integral_v<T>) {
        std::cout << "整型: " << v << "\n";
    } else if constexpr (std::is_floating_point_v<T>) {
        std::cout << "浮点: " << v << "\n";
    } else {
        std::cout << "其他类型\n";
    }
}

// 使用
// printValue(42);      // 输出 整型: 42
// printValue(3.14);    // 输出 浮点: 3.14

```

Advantage: We only need one template to handle multiple types. The logic is centralized, and adding new branches is straightforward.

------

### Case: Replacing SFINAE / enable_if (Cleaning Up Complex Specializations)

Scenario: Using `.size()` for types that support `T::size()`, falling back to other implementations otherwise.

```cpp
#include <type_traits>
#include <utility>

// 检测有无 size()（简单版）
template <typename T, typename = void>
constexpr bool has_size_v = false;

template <typename T>
constexpr bool has_size_v<T, std::void_t<decltype(std::declval<T>().size())>> = true;

template <typename T>
auto getSizeIfPossible(const T& t) {
    if constexpr (has_size_v<T>) {
        return t.size(); // 只有在 T 有 size() 时才编译
    } else {
        return std::size_t{0}; // 备用实现
    }
}

```

Explanation: With traditional SFINAE or `enable_if`, we would need to write multiple overloads or specializations, increasing both the code volume and maintenance costs.

------

### Case: Compile-Time Recursion (`constexpr` + `if constexpr`)

Usage: Compile-time computation (e.g., metaprogramming or constant generation).

```cpp
constexpr uint64_t factorial(uint64_t n) {
    if constexpr (n <= 1) {
        return 1;
    } else {
        return n * factorial(n - 1);
    }
}

// constexpr auto f6 = factorial(6); // 720，编译期已计算

```

Note: The `if constexpr` here is paired with the `constexpr` function, and the result is evaluated at compile time (if the usage context allows).
