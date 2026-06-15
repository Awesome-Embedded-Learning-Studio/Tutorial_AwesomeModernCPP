---
chapter: 99
cpp_standard:
- 23
description: 'Explicit object parameter deduction: allows the first parameter of a
  member function to be automatically deduced as the type and value category of *this'
difficulty: intermediate
order: 18
reading_time_minutes: 2
tags:
- host
- cpp-modern
- intermediate
title: Deducing this
translation:
  engine: anthropic
  source: documents/cpp-reference/core-language/18-deducing-this.md
  source_hash: f7d3a4a262494cd9dcdc50df0f44dcbc94acd965bd6a197ca712541ed92ef8d2
  token_count: 507
  translated_at: '2026-05-26T10:17:09.858331+00:00'
---
<!--
Reference Card Template
Used for feature cheat sheets under documents/cpp-reference/.
Unlike article-template.md, reference cards use a concise, structured format without a narrative style.

Tag usage rules:
1. Must include exactly 1 platform tag (reference cards uniformly use host)
2. Must include exactly 1 difficulty tag
3. Must include at least 1 topic tag
4. Selected from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# Deducing this (C++23)

## In a Nutshell

Write `this` or `self` as the first parameter of a member function, and the compiler automatically deduces the value category (lvalue/rvalue/const) of the calling object—eliminating the overload triplet of `const`/non-`const`/rvalue reference.

## Header

None (language feature)

## Core API Cheat Sheet

| Syntax | Description |
|--------|-------------|
| `this auto&&` | Rvalue reference object parameter |
| `this const auto&` | Const lvalue reference (read-only) |
| `this auto&` | Non-const lvalue reference (mutable) |
| `this auto&&` | Perfect forwarding, one definition covers all value categories |
| With templates | `template <typename Self> this Self&&` templated explicit object parameter |
| CRTP simplification | Explicit object parameters can directly replace CRTP, reducing base class overhead |

## Minimal Example

```cpp
// Standard: C++23
#include <iostream>
#include <utility>

struct Wrapper {
    int value;

    // 一个函数覆盖 const/非 const/右值三种场景
    template <typename Self>
    auto&& get(this Self&& self) {
        return std::forward<Self>(self).value;
    }
};

int main() {
    Wrapper w{42};
    const Wrapper cw{99};

    std::cout << w.get() << "\n";   // 42 (非 const 左值)
    std::cout << cw.get() << "\n";  // 99 (const 左值)
    std::cout << Wrapper{7}.get() << "\n"; // 7 (右值)
}
```

## Embedded Applicability: Medium

- Reduces boilerplate: one explicit object parameter replaces `const`/non-`const`/rvalue overloads
- Simplifies CRTP: deduces types directly in member functions, eliminating base class indirection overhead
- Especially useful for recursive lambda expressions and chained call APIs
- C++23 feature; compiler support is still progressing (GCC 14.1+, Clang 18+, MSVC 19.34+)
- Embedded toolchain upgrade cycles are long, making it unsuitable for projects requiring broad compatibility in the short term

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 14.1 | 18 | 19.34 |

## See Also

- [cppreference: Deducing this](https://en.cppreference.com/w/cpp/language/member_functions#Explicit_object_parameter)

---

*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
