---
title: Structured binding
description: Destructure elements of a tuple, pair, struct, or array into multiple
  variables at once
chapter: 99
order: 11
tags:
- host
- cpp-modern
- beginner
difficulty: beginner
cpp_standard:
- 17
- 20
- 23
translation:
  source: documents/cpp-reference/core-language/11-structured-binding.md
  source_hash: 201ae798cccf5a6c549492c1a571c3b649627961e37c2bd2b131f3095e7f81e9
  translated_at: '2026-05-26T10:16:07.876888+00:00'
  engine: anthropic
  token_count: 545
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

# Structured Binding (C++17)

## One-Liner

A single line of syntax that destructures the elements of a tuple, pair, struct, or array into independent variables simultaneously, eliminating the need for `std::get` and manual field-by-field access.

## Header

None (language feature)

## Core API Quick Reference

| Binding Form | Syntax | Description |
|--------------|--------|-------------|
| By value | `auto [a, b] = expr;` | Copies elements to new variables |
| Lvalue reference | `auto& [a, b] = expr;` | Binds to a reference of the original object |
| Read-only reference | `const auto& [a, b] = expr;` | Const reference, avoids copying |
| Forwarding reference | `auto&& [a, b] = expr;` | Perfect forwarding semantics |
| Array destructuring | `auto [a, b, c] = arr;` | Binds to array elements (count must match) |
| pair destructuring | `auto [key, val] = *map_iter;` | Binds to first/second of a pair |
| tuple destructuring | `auto [x, y, z] = tup;` | Binds to `get<I>` of a tuple-like object |
| struct destructuring | `auto [x, y] = point;` | Binds to public data members (declaration order) |

## Minimal Example

```cpp
// Standard: C++17
#include <iostream>
#include <map>
#include <tuple>

struct Point { double x, y; };

int main() {
    // struct 解构
    Point p{1.0, 2.0};
    auto [px, py] = p;
    std::cout << px << ", " << py << "\n"; // 1, 2

    // pair 解构（map 迭代）
    std::map<int, const char*> m{{1, "one"}, {2, "two"}};
    for (const auto& [key, val] : m) {
        std::cout << key << ": " << val << "\n";
    }

    // tuple 解构
    auto [a, b, c] = std::make_tuple(10, 20, 30);
    std::cout << a + b + c << "\n"; // 60
}
```

## Embedded Applicability: High

- Pure compile-time syntactic sugar with zero runtime overhead; the generated code is exactly equivalent to manually accessing fields
- Simplifies the unpacking of multi-field structures like register groups and sensor data, improving readability
- Pairs with `const auto&` to avoid copying, ideal for read-only access to hardware-mapped structs
- C++17 is fully supported in mainstream embedded toolchains (GCC 7+, ARM Clang 6+)

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 7 | 4.0 | 19.1 |

## See Also

- [Tutorial: Structured Bindings](../../vol2-modern-features/ch05-structured-bindings/01-structured-bindings.md)
- [cppreference: Structured binding declaration](https://en.cppreference.com/w/cpp/language/structured_binding)

---

*Some content adapted from [cppreference.com](https://en.cppreference.com/) under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) license*
