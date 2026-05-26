---
title: if constexpr
description: Compile-time conditional branching that selectively compiles code paths
  based on template parameters.
chapter: 99
order: 13
tags:
- host
- cpp-modern
- intermediate
- if_constexpr
difficulty: intermediate
cpp_standard:
- 17
- 20
- 23
translation:
  source: documents/cpp-reference/core-language/13-if-constexpr.md
  source_hash: b9d65858e0b0e11f0c5703f6edda87e6d191cf5cb61b1f3ecf4f69cb48b28ce5
  translated_at: '2026-05-26T10:16:40.695003+00:00'
  engine: anthropic
  token_count: 483
---
<!--
Reference Card Template
Used for feature quick-reference pages under documents/cpp-reference/.
Unlike article-template.md, reference cards use a concise, structured format without a narrative style.

Tag usage rules:
1. Must include exactly 1 platform tag (reference cards uniformly use host)
2. Must include exactly 1 difficulty tag
3. Must include at least 1 topic tag
4. Selected from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# if constexpr (C++17)

## In a Nutshell

Selectively compiles a branch within a template based on a compile-time condition. Discarded branches do not even need to pass syntax checking — a powerful tool for compile-time polymorphism.

## Header

None (language feature)

## Core API Quick Reference

| Syntax Form | Description |
|-------------|-------------|
| `if constexpr (cond) { ... }` | If `cond` is `true`, compiles the then branch |
| `if constexpr (cond) { ... } else { ... }` | Compiles one of two branches |
| `if constexpr (cond1) { ... } else if constexpr (cond2) { ... } else { ... }` | Multi-branch chain |
| `if constexpr` with concepts | `if constexpr (std::integral\<T\>)` type traits check |
| `if constexpr` with `requires` | (C++20) Concepts-based overloading is preferred instead |

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

- Zero runtime overhead: the condition is evaluated at compile time, and unmet branches generate no code at all
- Replaces SFINAE and tag dispatch, significantly improving the readability of template metaprogramming
- Ideal for selecting different code paths based on compile-time constants such as hardware platform or peripheral type
- Available since C++17, supported by GCC 7+ and ARM Clang 6+

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 7 | 3.9 | 19.1 |

## See Also

- [cppreference: if constexpr](https://en.cppreference.com/w/cpp/language/if)

---

*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
