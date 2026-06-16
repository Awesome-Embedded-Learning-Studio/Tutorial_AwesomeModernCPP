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
  translated_at: '2026-06-16T03:28:39.765530+00:00'
  engine: anthropic
  token_count: 375
---
# constexpr (C++11)

## In a Nutshell

Tells the compiler "this value or function has the ability to be evaluated at compile time," thereby moving runtime calculations to compile time and achieving zero-overhead complex logic.

## Header

None (language keyword)

## Core API Cheat Sheet

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Compile-time variable | `constexpr T var = expr;` | Requires `expr` to be a constant expression; variable is implicitly `const` |
| Compile-time function | `constexpr T func(args);` | If arguments are constants, it is evaluated at compile time; otherwise, it falls back to a normal function |
| Compile-time construction | `constexpr T::T(args);` | Allows constructing literal type objects in constant expressions |
| Compile-time destruction | `constexpr T::~T();` | (C++20) Allows destroying objects in constant expressions |
| Feature test macro | `__cpp_constexpr` | Detects the current compiler's level of support for constexpr |

## Minimal Example

```cpp
// Compile-time calculation
constexpr int fib(int n) {
    return (n <= 1) ? n : (fib(n - 1) + fib(n - 2));
}

int main() {
    // Calculated at compile time, result is embedded in the binary
    constexpr int result = fib(10);
    static_assert(result == 55, "fib(10) should be 55");
    return 0;
}
```

## Embedded Applicability: High

- Moves calculations like table lookups, CRC checks, and protocol parsing to compile time, consuming no Flash/RAM space.
- Values calculated at compile time can be used directly as template parameters (e.g., array sizes), satisfying static configuration needs in bare-metal environments.
- Offers better code readability and debugging experience compared to C macros and template metaprogramming.
- Note that C++11 has many restrictions (single `return` statement); we recommend using at least the C++14 standard for embedded projects.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.6 | 3.1 | 19.0 |

## See Also

- [cppreference: constexpr specifier](https://en.cppreference.com/w/cpp/language/constexpr)

---
*Part of the content references [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
