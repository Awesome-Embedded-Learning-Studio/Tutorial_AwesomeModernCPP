---
chapter: 99
cpp_standard:
- 17
- 20
- 23
description: 'Compile-time conditional branching: selectively compile code paths at
  compile time based on template parameters'
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
  translated_at: '2026-09-27T01:43:26+00:00'
  engine: anthropic
  token_count: 900
---
<!--
Reference Card Template
Used for feature cheat-sheet pages under documents/cpp-reference/.
Unlike article-template.md, reference cards use a refined, structured format and do not require a narrative style.

Tag usage rules:
1. Must include 1 platform tag (reference cards uniformly use host)
2. Must include 1 difficulty tag
3. Include at least 1 topic tag
4. Select from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# if constexpr (C++17)

## In a Nutshell

Inside a template, selectively compile a branch based on a compile-time condition—the discarded branch does not even need to pass syntax checking. A powerful tool for compile-time polymorphism.

## Header

None (language feature)

## Core API Cheat Sheet

| Syntax Form | Description |
|-------------|-------------|
| `if constexpr (cond) { ... }` | Compiles the `then` branch if `cond` is `true` |
| `if constexpr (cond) { ... } else { ... }` | Compiles exactly one of the two branches |
| `if constexpr (cond1) { ... } else if constexpr (cond2) { ... } else { ... }` | Multi-branch chain |
| `if constexpr` combined with concepts | `if constexpr (std::integral\<T\>)` type trait check |
| `if constexpr` combined with `requires` | (C++20) Concepts-based overloading is generally preferred instead |

## Minimal Example

```cpp
// Standard: C++17
#include <iostream>
#include <type_traits>

template <typename T>
auto print_type(const T& val) {
    if constexpr (std::is_integral_v<T>) {
        std::cout << "integral: " << val << "\n";
    } else if constexpr (std::is_floating_point_v<T>) {
        std::cout << "float: " << val << "\n";
    } else {
        std::cout << "other\n";
    }
}

int main() {
    print_type(42);     // integral: 42
    print_type(3.14);   // float: 3.14
    print_type("hi");   // other
}
```

## Embedded Applicability: High

- Zero runtime overhead: the condition is evaluated at compile time, and branches that do not match generate no code at all
- Replaces SFINAE and tag dispatch, greatly improving the readability of template metaprogramming
- Well suited for choosing different code paths based on compile-time constants such as hardware platform or peripheral type
- Available as of C++17; already supported by GCC 7+ and ARM Clang 6+

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 7 | 3.9 | 19.1 |

## See Also

- [cppreference: if constexpr](https://en.cppreference.com/w/cpp/language/if)

---

*Some content is referenced from [cppreference.com](https://en.cppreference.com/), used under the [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) license*
