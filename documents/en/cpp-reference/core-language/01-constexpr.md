---
chapter: 99
cpp_standard:
- 11
- 14
- 17
- 20
- 23
description: Keyword indicating that the value of a variable or function can be evaluated
  at compile time
difficulty: intermediate
order: 1
reading_time_minutes: 2
tags:
- host
- cpp-modern
- intermediate
title: constexpr
translation:
  source: documents/cpp-reference/core-language/01-constexpr.md
  source_hash: 20f317af5e1e6a4d16cf8cc1641cc54d6feddc797f675493f822b067b4a6048f
  translated_at: '2026-09-27T01:29:27+00:00'
  engine: anthropic
  token_count: 500
---
# constexpr (C++11)

## In a nutshell

Tells the compiler "this value or function is capable of being computed at compile time," which moves runtime computations to compile time and gives you complex logic with zero runtime overhead.

## Header file

None (language keyword)

## Core API Cheat Sheet

| Operation | Signature | Description |
|------|------|------|
| Compile-time variable | `constexpr T var = expr;` | Requires `expr` to be a constant expression; the variable is implicitly `const` |
| Compile-time function | `constexpr T func(params);` | Evaluated at compile time when given constant arguments; otherwise degrades to an ordinary function |
| Compile-time constructor | `constexpr T::T(params);` | Allows constructing objects of literal types inside constant expressions |
| Compile-time destructor | `constexpr T::~T();` | (C++20) Allows destroying objects inside constant expressions |
| Feature-test macro | `__cpp_constexpr` | Detects the current compiler's level of constexpr support |

## Minimal Example

```cpp
// Standard: C++14
#include <iostream>

constexpr int factorial(int n) {
    int res = 1;
    while (n > 1) res *= n--;
    return res;
}

int main() {
    constexpr int val = factorial(5); // Compile-time evaluation
    std::cout << val << '\n';         // Output: 120
    int k = 4;
    std::cout << factorial(k) << '\n';// Runtime evaluation: 24
}
```

## Embedded Applicability: High

- Moves computations such as lookup tables, CRC checks, and protocol parsing to compile time, consuming no Flash/RAM space
- Values computed at compile time can be used directly as template arguments (e.g., array sizes), meeting the static-configuration needs of bare-metal environments
- More readable and easier to debug than C macros or template metaprogramming
- Note that C++11 imposes many restrictions (single `return` statement); for embedded projects we recommend at least the C++14 standard

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.6 | 3.1 | 19.0 |

## See Also

- [cppreference: constexpr specifier](https://en.cppreference.com/w/cpp/language/constexpr)

---
*Part of the content is referenced from [cppreference.com](https://en.cppreference.com/) and is licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
