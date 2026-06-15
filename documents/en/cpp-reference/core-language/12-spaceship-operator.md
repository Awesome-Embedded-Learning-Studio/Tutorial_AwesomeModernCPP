---
chapter: 99
cpp_standard:
- 20
- 23
description: A C++20 language feature that automatically generates all six comparison
  operators from a single definition.
difficulty: intermediate
order: 12
reading_time_minutes: 2
tags:
- host
- cpp-modern
- intermediate
title: Three-way comparison operator (<=>)
translation:
  engine: anthropic
  source: documents/cpp-reference/core-language/12-spaceship-operator.md
  source_hash: d03fa35b82ab836a64c86a85a4be50a942b81f56abe5f67e35aac9a54dc17b6c
  token_count: 523
  translated_at: '2026-05-26T10:16:16.396663+00:00'
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

# Spaceship Operator <=> (C++20)

## In a Nutshell

Defining `operator<=>` lets the compiler automatically generate all six comparison operators: `<`, `<=`, `>`, `>=`, `==`, and `!=`. Say goodbye to boilerplate comparison code.

## Header

`#include <compare>` (when using predefined comparison categories)

## Core API Cheat Sheet

| Operation | Signature | Description |
|------|------|------|
| Three-way comparison | `auto operator<=>(const T&) const = default;` | Compiler auto-generates comparison logic |
| Manual three-way comparison | `std::strong_ordering operator<=>(const T& rhs) const;` | Custom comparison semantics |
| Strong ordering | `std::strong_ordering` | Equivalent elements are indistinguishable (e.g., `int`) |
| Weak ordering | `std::weak_ordering` | Equivalent elements are distinguishable but compare equal (e.g., case-insensitive strings) |
| Partial ordering | `std::partial_ordering` | Incomparable cases exist (e.g., NaN) |
| Equality operator | `bool operator==(const T&) const = default;` | Defaulting it alone auto-generates `!=` |

## Minimal Example

```cpp
// Standard: C++20
#include <compare>
#include <iostream>

struct Point {
    int x, y;
    auto operator<=>(const Point&) const = default;
};

int main() {
    Point a{1, 2}, b{1, 3};
    std::cout << (a < b)  << "\n"; // true  (自动生成)
    std::cout << (a == b) << "\n"; // false (自动生成)
    std::cout << (a != b) << "\n"; // true  (自动生成)

    auto cmp = a <=> b;
    std::cout << (cmp < 0) << "\n"; // true (strong_ordering::less)
}
```

## Embedded Applicability: Medium

- Compile-time feature with zero runtime overhead — default-generated comparison code is equivalent to hand-written code
- Suitable for structs requiring lexicographical comparison, such as sensor data and protocol headers
- Requires C++20 support (GCC 10+); some embedded toolchains are not fully ready yet
- Comparison categories (strong/weak/partial) are abstract concepts that require team-wide alignment

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 10 | 10 | 19.20 |

## See Also

- [cppreference: Default comparisons](https://en.cppreference.com/w/cpp/language/default_comparisons)
- [cppreference: std::strong_ordering](https://en.cppreference.com/w/cpp/utility/compare/strong_ordering)

---

*Some content adapted from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
