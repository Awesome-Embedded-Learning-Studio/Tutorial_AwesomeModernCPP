---
title: "Structured Bindings"
description: "Decompose tuple, pair, struct, or array elements into individual variables in one line"
chapter: 99
order: 11
tags:
  - host
  - cpp-modern
  - beginner
difficulty: beginner
cpp_standard: [17, 20, 23]
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

# Structured Bindings (C++17)

## In a Nutshell

Decompose tuple, pair, struct, or array elements into independent variables in a single line — no more `std::get` or field-by-field access.

## Header

None (language feature)

## Core API Quick Reference

| Binding Form | Syntax | Description |
|-------------|--------|-------------|
| By value | `auto [a, b] = expr;` | Copy elements into new variables |
| Lvalue reference | `auto& [a, b] = expr;` | Bind to references of the original object |
| Const reference | `const auto& [a, b] = expr;` | Read-only reference, avoids copies |
| Forwarding reference | `auto&& [a, b] = expr;` | Perfect forwarding semantics |
| Array decomposition | `auto [a, b, c] = arr;` | Bind to array elements (count must match) |
| Pair decomposition | `auto [key, val] = *map_iter;` | Bind to pair's first/second |
| Tuple decomposition | `auto [x, y, z] = tup;` | Bind to tuple-like `get<I>` |
| Struct decomposition | `auto [x, y] = point;` | Bind to public data members (declaration order) |

## Minimal Example

```cpp
// Standard: C++17
#include <iostream>
#include <map>
#include <tuple>

struct Point { double x, y; };

int main() {
    // Struct decomposition
    Point p{1.0, 2.0};
    auto [px, py] = p;
    std::cout << px << ", " << py << "\n"; // 1, 2

    // Pair decomposition (map iteration)
    std::map<int, const char*> m{{1, "one"}, {2, "two"}};
    for (const auto& [key, val] : m) {
        std::cout << key << ": " << val << "\n";
    }

    // Tuple decomposition
    auto [a, b, c] = std::make_tuple(10, 20, 30);
    std::cout << a + b + c << "\n"; // 60
}
```

## Embedded Applicability: High

- Pure compile-time syntactic sugar with zero runtime overhead — generates identical code to manual field access
- Simplifies unpacking of multi-field structures like register groups and sensor data, improving readability
- Use with `const auto&` to avoid copies, ideal for read-only access to hardware-mapped structs
- C++17 is well-supported by mainstream embedded toolchains (GCC 7+, ARM Clang 6+)

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 7 | 4.0 | 19.1 |

## See Also

- [cppreference: Structured binding declaration](https://en.cppreference.com/w/cpp/language/structured_binding)

---

*Some content adapted from [cppreference.com](https://en.cppreference.com/) under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) license*
