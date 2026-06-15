---
chapter: 99
cpp_standard:
- 11
- 14
- 17
- 20
- 23
description: A placeholder that lets the compiler automatically deduce the type of
  a variable or function return value
difficulty: beginner
order: 3
reading_time_minutes: 2
tags:
- host
- cpp-modern
- beginner
title: auto
translation:
  engine: anthropic
  source: documents/cpp-reference/core-language/03-auto-decltype.md
  source_hash: 75554e72bb849a782dce5de0a3a97f52d930df7ea621293962e910b358a46882
  token_count: 387
  translated_at: '2026-05-26T10:15:05.751748+00:00'
---
# auto (C++11)

## In a nutshell

We use `auto` to declare variables or function return types, letting the compiler deduce the concrete type from the initialization expression, saving us the trouble of writing out lengthy or complex types by hand.

## Header

None needed (language keyword)

## Core API Quick Reference

| Operation | Signature | Description |
|------|------|------|
| Variable type deduction | `auto x = init;` | Deduces the type of `x` based on the initialization expression |
| Deduction with modifiers | `const auto& x = init;` | Deduces the base type and attaches `const` or reference qualifiers |
| Trailing return type | `auto f() -> int;` | Used with a trailing return type to declare a function |
| Return type deduction | `auto f() { return expr; }` | Starting in C++14, deduces the return type from the return statement |
| decltype(auto) | `decltype(auto) f() { return expr; }` | Starting in C++14, preserves the value category of the expression (reference/top-level const)|
| Concept-constrained deduction | `Concept auto x = init;` | Starting in C++20, deduces the type and checks whether it satisfies concept constraints |
| Functional-style cast | `auto(expr)` | Starting in C++23, equivalent to `static_cast<auto>(expr)` |

## Minimal Example

```cpp
// Standard: C++14
#include <iostream>

auto add(int a, int b) {
    return a + b; // 返回类型推导为 int
}

int main() {
    auto x = 10;        // int
    const auto& r = x;  // const int&
    auto sum = add(x, 5);
    std::cout << sum << "\n";
}
```

## Embedded Applicability: High

- Zero runtime overhead; `auto` is purely compile-time type deduction and generates no extra instructions
- Simplifies register/peripheral type declarations (e.g., `auto reg = reinterpret_cast<volatile uint32_t*>(0x40001000)`), improving readability without losing precision
- When used with templates and STL container iterators, it avoids writing lengthy type names and reduces typos

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.4 | 2.9 | 10.0 |

## See Also

- [cppreference: Placeholder type specifiers](https://en.cppreference.com/w/cpp/language/auto)

---
*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
