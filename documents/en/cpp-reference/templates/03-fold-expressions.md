---
chapter: 99
cpp_standard:
- 17
- 20
- 23
description: Fold the parameter pack over a binary operator, replacing recursive template
  expansion.
difficulty: intermediate
order: 3
reading_time_minutes: 1
tags:
- host
- cpp-modern
- intermediate
title: Fold Expression
translation:
  source: documents/cpp-reference/templates/03-fold-expressions.md
  source_hash: bb701c6711523abb1071ff016b6234e625553a35d2ba79ce3c516e527fc49c54
  translated_at: '2026-06-16T03:30:04.642715+00:00'
  engine: anthropic
  token_count: 410
---
# Fold Expressions (C++17)

## In a Nutshell

We fold a parameter pack from a variadic template into a single expression using a specified operator, eliminating the need to manually write recursive termination conditions.

## Header

No header required (language feature)

## Core API Quick Reference

| Operation | Syntax | Description |
|------|------|------|
| Unary Right Fold | `( ... op pack )` | Expands to `((p1 op p2) op ...) op pN` |
| Unary Left Fold | `( pack op ... )` | Expands to `p1 op (... (pN-1 op pN))` |
| Binary Right Fold | `( init op ... op pack )` | Right fold with an initial value |
| Binary Left Fold | `( pack op ... op init )` | Left fold with an initial value |
| Empty Pack Fold (`&&`) | `( ... && pack )` | Result is `true` if the pack is empty |
| Empty Pack Fold (`\|\|`) | `( ... \|\| pack )` | Result is `false` if the pack is empty |
| Empty Pack Fold (`,`) | `( pack , ... )` | Result is `void` if the pack is empty |

> **Note:** Supports 32 binary operators: `+ - * / % ^ & \| = < > << >> += -= *= /= %= ^= &= |= <<= >>= == != <= >= && , .* ->*`

## Minimal Example

```cpp
#include <iostream>
#include <string>
#include <type_traits>

// 1. Basic usage: Sum all arguments
template<typename... Args>
auto sum(Args... args) {
    return (args + ...); // Unary right fold: (a1 + a2) + ...
}

// 2. Compile-time check: Are all types integral?
template<typename... Args>
constexpr bool are_all_integral = (std::is_integral_v<Args> && ...);

// 3. Comma fold: Execute multiple statements
template<typename... Args>
void print_all(Args&&... args) {
    // Binary left fold with init: std::cout << args1, then std::cout << args2, ...
    (std::cout << ... << args) << '\n';
}

int main() {
    // Usage 1: Sum
    std::cout << "Sum: " << sum(1, 2, 3, 4) << '\n'; // Output: 10

    // Usage 2: Type check
    static_assert(are_all_integral<int, short, char>);
    // static_assert(are_all_integral<int, double>); // Error: double is not integral

    // Usage 3: Print all
    print_all("Hello", " ", "World", 2023); // Output: Hello World 2023
}
```

## Embedded Applicability: Medium

- Compile-time pure calculations (such as conditional checks in `static_assert`) have zero runtime overhead, making them highly suitable.
- Useful for replacing recursive template instantiation, which can reduce compile-time memory usage and compilation time.
- Avoid using complex fold expressions in frequently called hot paths to prevent code bloat that increases Flash usage.
- When using comma folds to expand multiple statements, ensure the overhead of each statement is within acceptable limits.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 6.0 | 3.6 | 19.1 |

## See Also

- [cppreference: Fold expressions](https://en.cppreference.com/w/cpp/language/fold)

---

*Part of the content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
