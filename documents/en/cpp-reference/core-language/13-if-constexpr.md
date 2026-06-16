---
chapter: 99
cpp_standard:
- 17
- 20
- 23
description: Compile-time conditional branching, selectively compiling code paths
  at compile time based on template parameters.
difficulty: intermediate
order: 13
reading_time_minutes: 2
tags:
- host
- cpp-modern
- intermediate
- if_constexpr
title: if constexpr
translation:
  source: documents/cpp-reference/core-language/13-if-constexpr.md
  source_hash: 4cdd84329ae7e6fba7dab0ea967917b81762ed907660997df3e07f8513376605
  translated_at: '2026-06-16T03:29:21.303888+00:00'
  engine: anthropic
  token_count: 486
---
<!--
Reference Card Template
Used for feature cheat sheets under documents/cpp-reference/.
Unlike article-template.md, reference cards use a refined structured format and do not require a narrative style.

Tag usage rules:
1. Must include 1 platform tag (use 'host' for reference cards)
2. Must include 1 difficulty tag
3. Must include at least 1 topic tag
4. Select from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# if constexpr (C++17)

## One-Liner

Selectively compiles a branch based on compile-time conditions within templates; discarded branches are not even syntactically checked—a powerful tool for compile-time polymorphism.

## Header

None (language feature)

## Core API Cheat Sheet

| Syntax Form | Description |
|-------------|-------------|
| `if constexpr ( condition )` | Compiles the `then` branch if `condition` is `true` |
| `if constexpr ( condition ) statement else statement` | Compile-time binary selection |
| `if constexpr` chain | Multi-branch chain |
| `if constexpr` with Concepts | `requires` type trait checking |
| `if constexpr` with `auto` | (C++20) Concepts overloading is preferred instead |

## Minimal Example

```cpp
template <typename T>
auto get_value(T t) {
    if constexpr (std::is_pointer_v<T>) {
        return *t; // Deduces return type to underlying type
    } else {
        return t;  // Deduces return type to T
    }
}

void usage() {
    int x = 10;
    get_value(x);   // Instantiates with T=int
    get_value(&x);  // Instantiates with T=int*
}
```

## Embedded Applicability: High

- Zero runtime overhead: conditions are evaluated at compile time, and unmet branches generate no code.
- Replaces SFINAE and tag dispatching, significantly improving template metaprogramming readability.
- Ideal for selecting different code paths based on compile-time constants like hardware platforms or peripheral types.
- Available since C++17; supported by GCC 7+ and ARM Clang 6+.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 7   | 3.9   | 19.1 |

## See Also

- [cppreference: if constexpr](https://en.cppreference.com/w/cpp/language/if)

---

*Part of the content references [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
