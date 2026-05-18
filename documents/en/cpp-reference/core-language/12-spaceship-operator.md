---
title: "Three-Way Comparison Operator (<=>)"
description: "Define operator<=> once and let the compiler generate all six comparison operators automatically"
chapter: 99
order: 12
tags:
  - host
  - cpp-modern
  - intermediate
difficulty: intermediate
cpp_standard: [20, 23]
---

<!--
Reference Card Template
Used for feature quick-reference pages under documents/cpp-reference/.
Unlike article-template.md, reference cards use a concise, structured format and do not require a narrative style.

Tag usage rules:
1. Must include exactly 1 platform tag (reference cards uniformly use host)
2. Must include exactly 1 difficulty tag
3. Must include at least 1 topic tag
4. Selected from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# Three-Way Comparison Operator <=> (C++20)

## In a Nutshell

Define `operator<=>` and the compiler auto-generates all six comparison operators (`<`, `<=`, `>`, `>=`, `==`, `!=`) — goodbye boilerplate comparison code.

## Header

`#include <compare>` (when using predefined comparison categories)

## Core API Quick Reference

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Three-way comparison | `auto operator<=>(const T&) const = default;` | Compiler auto-generates comparison logic |
| Custom three-way | `std::strong_ordering operator<=>(const T& rhs) const;` | Custom comparison semantics |
| Strong ordering | `std::strong_ordering` | Equivalent elements are indistinguishable (e.g., int) |
| Weak ordering | `std::weak_ordering` | Equivalent elements are distinguishable but compare equal (e.g., case-insensitive strings) |
| Partial ordering | `std::partial_ordering` | Incomparable values exist (e.g., NaN) |
| Equality operator | `bool operator==(const T&) const = default;` | Defaulting == alone auto-generates != |

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
    std::cout << (a < b)  << "\n"; // true  (auto-generated)
    std::cout << (a == b) << "\n"; // false (auto-generated)
    std::cout << (a != b) << "\n"; // true  (auto-generated)

    auto cmp = a <=> b;
    std::cout << (cmp < 0) << "\n"; // true (strong_ordering::less)
}
```

## Embedded Applicability: Medium

- Compile-time feature with zero runtime overhead — default-generated comparisons are equivalent to hand-written code
- Suitable for structs requiring lexicographic comparison (sensor data, protocol headers)
- Requires C++20 support (GCC 10+); some embedded toolchains are not yet fully ready
- Comparison categories (strong/weak/partial) are conceptually abstract; teams need shared understanding

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 10 | 10 | 19.20 |

## See Also

- [cppreference: Default comparisons](https://en.cppreference.com/w/cpp/language/default_comparisons)
- [cppreference: std::strong_ordering](https://en.cppreference.com/w/cpp/utility/compare/strong_ordering)

---

*Some content adapted from [cppreference.com](https://en.cppreference.com/) under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) license*
