---
chapter: 99
cpp_standard:
- 11
- 14
- 17
- 20
- 23
description: A placeholder that lets the compiler automatically deduce the type of a variable or a function's return value
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
  translated_at: '2026-09-27T01:58:13+00:00'
  engine: anthropic
  token_count: 450
---
# auto (C++11)

## In a Nutshell

When we declare a variable or a function's return type with `auto`, the compiler deduces the concrete type from the initialization expression, sparing us the trouble of writing out lengthy or complicated type names by hand.

## Header

No header required (language keyword)

## Core API Cheat Sheet

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Variable type deduction | `auto x = init;` | Deduces the type of `x` from the initialization expression |
| Deduction with modifiers | `const auto& x = init;` | Deduces the base type and attaches `const` or reference qualifiers |
| Trailing return type | `auto f() -> int;` | Declares a function combined with a trailing return type |
| Return type deduction | `auto f() { return expr; }` | Since C++14; deduces the return type from the `return` statement |
| decltype(auto) | `decltype(auto) f() { return expr; }` | Since C++14; preserves the expression's value category (reference/top-level `const`) |
| Concept-constrained deduction | `Concept auto x = init;` | Since C++20; deduces the type and checks whether it satisfies the concept constraint |
| Function-style cast | `auto(expr)` | Since C++23; equivalent to `static_cast<auto>(expr)` |

## Minimal Example

```cpp
// Standard: C++14
#include <iostream>

auto add(int a, int b) {
    return a + b; // return type deduced as int
}

int main() {
    auto x = 10;        // int
    const auto& r = x;  // const int&
    auto sum = add(x, 5);
    std::cout << sum << "\n";
}
```

## Embedded Applicability: High

- Zero runtime overhead: `auto` is purely compile-time type deduction and generates no extra instructions
- Simplifies register/peripheral type declarations (e.g., `auto reg = reinterpret_cast<volatile uint32_t*>(0x40001000)`), improving readability without any loss of precision
- Combined with templates and STL container iterators, it spares us from hand-writing verbose type names and cuts down on spelling mistakes

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.4 | 2.9 | 10.0 |

## See Also

- [cppreference: Placeholder type specifiers](https://en.cppreference.com/w/cpp/language/auto)

---
*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
