---
chapter: 99
cpp_standard:
- 11
- 14
- 17
- 20
- 23
description: Placeholder for the compiler to automatically deduce variable or function
  return value types
difficulty: beginner
order: 3
reading_time_minutes: 2
tags:
- host
- cpp-modern
- beginner
title: auto
translation:
  source: documents/cpp-reference/core-language/03-auto-decltype.md
  source_hash: 90651dfbcc623bb96ba4e04ffb01a7e9fda0c579f1f85b0fbfb2aa054b3f63f6
  translated_at: '2026-06-16T03:28:48.316323+00:00'
  engine: anthropic
  token_count: 391
---
# auto (C++11)

## In a Nutshell

We use `auto` to declare variables or function return types, allowing the compiler to automatically deduce the specific type from the initialization expression. This saves us the trouble of writing out lengthy or complex types manually.

## Header

No header required (language keyword)

## Core API Cheat Sheet

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Variable type deduction | `auto x = init;` | Deduces the type of `x` based on the initialization expression |
| Deduction with modifiers | `auto&`, `const auto*` | Deduces the base type and attaches reference or `const` qualifiers |
| Trailing return type | `auto foo() -> int` | Declares a function using a trailing return type |
| Return type deduction | `auto foo() { ... }` | Available since C++14; deduces the return type from the `return` statement |
| decltype(auto) | `decltype(auto)` | Available since C++14; preserves the value category (reference/top-level `const`) of the expression |
| Concept-constrained deduction | `std::integral auto` | Available since C++20; deduces the type and checks if it satisfies concept constraints |
| Functional cast | `auto(x)` | Available since C++23; equivalent to `static_cast<decltype(x)>(x)` |

## Minimal Example

```cpp
auto i = 42;                 // int
auto& r = i;                 // int&
const auto* p = &i;          // const int*

// C++14: Return type deduction
auto add(int x, int y) {
    return x + y;            // Returns int
}

// C++14: decltype(auto) preserves references
decltype(auto) get_ref(int& x) {
    return x;                // Returns int&
}

// C++20: Constrained auto
std::integral auto num = 10; // OK, int is integral
// std::integral auto f = 3.14; // Error, double is not integral
```

## Embedded Applicability: High

- Zero runtime overhead. `auto` is purely a compile-time type deduction mechanism and generates no additional instructions.
- Simplifies register/peripheral type declarations (e.g., `auto& reg = *GPIOA->ODR`), improving readability without losing precision.
- When working with templates and STL container iterators, it helps us avoid writing verbose type names and reduces spelling errors.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.4 | 2.9 | 10.0 |

## See Also

- [cppreference: Placeholder type specifiers](https://en.cppreference.com/w/cpp/language/auto)

---
*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
