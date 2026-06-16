---
chapter: 99
cpp_standard:
- 20
- 23
description: A C++20 language feature that automatically generates all six comparison
  operators with a single definition
difficulty: intermediate
order: 12
reading_time_minutes: 2
tags:
- host
- cpp-modern
- intermediate
title: Three-way comparison operator (<=>)
translation:
  source: documents/cpp-reference/core-language/12-spaceship-operator.md
  source_hash: 8c37ae14058b22b1bd43e8f33a489597996c81c26ca0abf88a5dc7a623ad473d
  translated_at: '2026-06-16T03:29:22.185214+00:00'
  engine: anthropic
  token_count: 526
---
<!--
Reference Card Template
Used for feature cheat sheets under documents/cpp-reference/.
Unlike article-template.md, reference cards use a concise, structured format and do not require a narrative style.

Tag usage rules:
1. Must include 1 platform tag (use 'host' for reference cards)
2. Must include 1 difficulty tag
3. Include at least 1 topic tag
4. Select from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# Three-Way Comparison Operator <=> (C++20)

## In a Nutshell

Defining `operator<=>` allows the compiler to automatically generate `<`, `>`, `<=`, `>=`, `==`, and `!=`. Say goodbye to writing comparison code manually.

## Header

`<compare>` (when using predefined comparison categories)

## Core API Cheat Sheet

| Operation | Signature | Description |
|------|------|------|
| Three-way comparison | `auto operator<=>(const T&) const = default;` | Compiler automatically generates comparison logic |
| Manual three-way comparison | `std::strong_ordering operator<=>(const T&) const;` | Custom comparison semantics |
| Strong ordering | `std::strong_ordering` | Equivalent elements are indistinguishable (e.g., `int`) |
| Weak ordering | `std::weak_ordering` | Equivalent elements are distinguishable but compare equal (e.g., case-insensitive strings) |
| Partial ordering | `std::partial_ordering` | Incomparable values exist (e.g., `NaN`) |
| Equality operator | `bool operator==(const T&) const = default;` | Defaulting this alone automatically generates `!=` |

## Minimal Example

```cpp
#include <compare>
#include <iostream>

struct Point {
    int x, y;

    // Compiler auto-generates <, <=, >, >=, ==, !=
    std::strong_ordering operator<=>(const Point&) const = default;
};

int main() {
    Point p1{1, 2}, p2{1, 5};

    if (p1 < p2) {
        std::cout << "p1 is less than p2\n";
    }
    // p1 == p1, p2 != p1 also work
}
```

## Embedded Applicability: Medium

- Compile-time feature, zero runtime overhead—defaulted comparison code is equivalent to handwritten code.
- Suitable for structs requiring lexicographical comparison, such as sensor data or protocol headers.
- Requires C++20 support (GCC 10+); some embedded toolchains are not yet fully ready.
- Comparison categories (strong/weak/partial) are abstract concepts; teams need a unified understanding.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 10 | 10 | 19.20 |

## See Also

- [cppreference: Default comparisons](https://en.cppreference.com/w/cpp/language/default_comparisons)
- [cppreference: std::strong_ordering](https://en.cppreference.com/w/cpp/utility/compare/strong_ordering)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
