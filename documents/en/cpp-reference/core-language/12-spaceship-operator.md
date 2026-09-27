---
chapter: 99
cpp_standard:
- 20
- 23
description: A C++20 language feature that auto-generates all six comparison operators
  from a single definition
difficulty: intermediate
order: 12
reading_time_minutes: 2
tags:
- host
- cpp-modern
- intermediate
title: Three-Way Comparison Operator (<=>)
translation:
  source: documents/cpp-reference/core-language/12-spaceship-operator.md
  source_hash: 8c37ae14058b22b1bd43e8f33a489597996c81c26ca0abf88a5dc7a623ad473d
  translated_at: '2026-09-27T01:46:13+00:00'
  engine: anthropic
  token_count: 600
---
<!--
Reference Card Template
For feature cheat sheets under documents/cpp-reference/.
Unlike article-template.md, reference cards use a concise, structured format with no narrative style required.

Tag usage rules:
1. Must include 1 platform tag (reference cards consistently use host)
2. Must include 1 difficulty tag
3. Must include at least 1 topic tag
4. Select from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# Three-Way Comparison Operator <=> (C++20)

## In a Nutshell

Define `operator<=>` once, and the compiler automatically generates all six comparison operators — `<`, `<=`, `>`, `>=`, `==`, and `!=`. Say goodbye to hand-written comparison code.

## Header

`#include <compare>` (when using the predefined comparison categories)

## Core API Cheat Sheet

| Operation | Signature | Description |
|------|------|------|
| Three-way comparison | `auto operator<=>(const T&) const = default;` | The compiler generates the comparison logic automatically |
| Hand-written three-way comparison | `std::strong_ordering operator<=>(const T& rhs) const;` | Custom comparison semantics |
| Strong ordering | `std::strong_ordering` | Equivalent elements are indistinguishable (e.g., `int`) |
| Weak ordering | `std::weak_ordering` | Equivalent elements are distinguishable but compare equal (e.g., case-insensitive strings) |
| Partial ordering | `std::partial_ordering` | Incomparable cases exist (e.g., NaN) |
| Equality operator | `bool operator==(const T&) const = default;` | Defaulting this alone also auto-generates `!=` |

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

## Embedded Suitability: Medium

- Compile-time feature with zero runtime overhead — the defaulted comparison code is equivalent to hand-written code
- Well suited to structs that need lexicographical comparison, such as sensor data or protocol headers
- Requires C++20 support (GCC 10+); some embedded toolchains are not fully there yet
- Comparison categories (strong/weak/partial) are fairly abstract concepts; the team needs a shared understanding

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 10 | 10 | 19.20 |

## See Also

- [cppreference: Default comparisons](https://en.cppreference.com/w/cpp/language/default_comparisons)
- [cppreference: std::strong_ordering](https://en.cppreference.com/w/cpp/utility/compare/strong_ordering)

---

*Part of the content is referenced from [cppreference.com](https://en.cppreference.com/) and is licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
