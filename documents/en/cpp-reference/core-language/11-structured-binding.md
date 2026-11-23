---
chapter: 99
cpp_standard:
- 17
- 20
- 23
description: Unpack the elements of a tuple, pair, struct, or array into multiple
  variables at once
difficulty: beginner
order: 11
reading_time_minutes: 2
tags:
- host
- cpp-modern
- beginner
title: Structured Bindings
translation:
  source: documents/cpp-reference/core-language/11-structured-binding.md
  source_hash: 2314cef3e6ea89a53ceac35230a89062a14d3ade18eb851e41fcbeaad2b72810
  translated_at: '2026-11-22T16:41:57.286681+00:00'
  engine: glm-5.3-refresh
  token_count: 2198
---
<!--
Reference Card Template
Used for feature quick-reference pages under documents/cpp-reference/.
Unlike article-template.md, reference cards use a concise, structured format with no narrative style.

Tag usage rules:
1. Must include 1 platform tag (reference cards use host throughout)
2. Must include 1 difficulty tag
3. Must include at least 1 topic tag
4. Select from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# Structured Bindings (C++17)

## In a Nutshell

A single line of syntax that destructures the elements of a tuple, pair, struct, or array into independent variables at once, sparing you `std::get` and field-by-field access.

## Header

None (language feature)

## Core API Quick Reference

| Binding Form | Syntax | Description |
|------|------|------|
| Bind by value | `auto [a, b] = expr;` | Copies the elements into new variables |
| Lvalue reference | `auto& [a, b] = expr;` | Binds references to the original object |
| Read-only reference | `const auto& [a, b] = expr;` | Const reference, avoids copying |
| Forwarding reference | `auto&& [a, b] = expr;` | Perfect forwarding semantics |
| Array destructuring | `auto [a, b, c] = arr;` | Binds to array elements (count must match) |
| Pair destructuring | `auto [key, val] = *map_iter;` | Binds to a pair's `first`/`second` |
| Tuple destructuring | `auto [x, y, z] = tup;` | Binds to `get<I>` of tuple-like types |
| Struct destructuring | `auto [x, y] = point;` | Binds to public data members (in declaration order) |

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

- Pure compile-time syntactic sugar with zero runtime overhead; the generated code is fully equivalent to manual field access
- Simplifies unpacking multi-field structures such as register banks and sensor data, improving readability
- Combine with `const auto&` to avoid copies — ideal for read-only access to hardware-mapped structs
- Well supported in mainstream embedded toolchains (GCC 7+, ARM Clang 6+) since C++17

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 7 | 4.0 | 19.11 |

## See Also

- [Tutorial: Structured Bindings](../../vol2-modern-features/ch05-structured-bindings/01-structured-bindings.md)
- [cppreference: Structured binding declaration](https://en.cppreference.com/w/cpp/language/structured_binding)

---

*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
