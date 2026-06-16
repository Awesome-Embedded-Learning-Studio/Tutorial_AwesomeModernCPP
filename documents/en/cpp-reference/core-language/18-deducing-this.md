---
chapter: 99
cpp_standard:
- 23
description: 'Explicit object parameter deduction: Deduces the type and value category
  of `*this` from the first parameter of a member function.'
difficulty: intermediate
order: 18
reading_time_minutes: 2
tags:
- host
- cpp-modern
- intermediate
title: Deducing this
translation:
  source: documents/cpp-reference/core-language/18-deducing-this.md
  source_hash: 4390c71b51c4db98a286c470411528823593f770c902c4f3f12ca61e708a26ce
  translated_at: '2026-06-16T03:29:37.878629+00:00'
  engine: anthropic
  token_count: 510
---
<!--
Reference Card Template
For feature cheat sheets under documents/cpp-reference/.
Unlike article-template.md, reference cards use a refined structured format and do not require a narrative style.

Tag usage rules:
1. Must include 1 platform tag (use 'host' for reference cards)
2. Must include 1 difficulty tag
3. Must include at least 1 topic tag
4. Select from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# Deducing this (C++23)

## One-Liner

Write the first parameter of a member function as `this` (or a self-chosen name), and the compiler automatically deduces the value category (lvalue/rvalue/const) of the calling object—eliminating the need for the `const`/non-`const`/rvalue reference overload trio.

## Header

None (language feature)

## Core API Cheat Sheet

| Syntax | Description |
|------|------|
| `this Self&&` | Rvalue reference object parameter |
| `this const Self&` | `const` lvalue reference (read-only) |
| `this Self&` | Non-`const` lvalue reference (mutable) |
| `this auto&&` | Perfect forwarding, one definition covers all value categories |
| With templates | `template<typename Self> this Self&&` templated explicit object parameter |
| CRTP Simplification | Explicit object parameters can directly replace CRTP, reducing base class overhead |

## Minimal Example

```cpp
#include <print>
#include <utility>

struct Widget {
    // Explicit object parameter: deduces `self` type based on value category
    // If called on lvalue: self = Widget&
    // If called on const lvalue: self = const Widget&
    // If called on rvalue: self = Widget&&
    void print(this auto&& self) {
        std::println("Value: {}", self.value);
    }

    int value{42};
};

int main() {
    Widget w;
    w.print();           // Deduces Widget&
    std::move(w).print(); // Deduces Widget&&
}
```

## Embedded Applicability: Moderate

- **Reduces boilerplate**: One explicit object parameter replaces `const`/non-`const`/rvalue overloads.
- **Simplifies CRTP**: Deduce types directly in member functions, eliminating base class indirection overhead.
- **Particularly useful for recursive lambdas and fluent/chaining APIs.**
- **C++23 feature**: Compiler support is still rolling out (GCC 14.1+, Clang 18+, MSVC 19.34+).
- **Embedded toolchains have long upgrade cycles**: Not suitable for projects requiring broad compatibility in the short term.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 14.1 | 18 | 19.34 |

## See Also

- [cppreference: Deducing this](https://en.cppreference.com/w/cpp/language/member_functions#Explicit_object_parameter)

---

*Part of the content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
