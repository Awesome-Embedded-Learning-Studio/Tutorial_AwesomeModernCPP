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
title: Structured Binding
translation:
  source: documents/cpp-reference/core-language/11-structured-binding.md
  source_hash: 1621cf676a413f714b056e1b86e30afb79840e22ed5ac6ce7ac8a10ac5cd9287
  translated_at: '2026-06-16T03:29:22.065137+00:00'
  engine: anthropic
  token_count: 549
---
<!--
Reference Card Template
Used for feature cheat sheets under documents/cpp-reference/.
Unlike article-template.md, reference cards use a concise, structured format, not a narrative style.

Tag usage rules:
1. Must include 1 platform tag (use 'host' for reference cards)
2. Must include 1 difficulty tag
3. Must include at least 1 topic tag
4. Select from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# Structured Binding (C++17)

## One-Liner

A single line of syntax that destructures elements of a tuple, pair, struct, or array into separate variables simultaneously, eliminating `std::tie` and per-field access.

## Header

None (language feature)

## Core API Cheat Sheet

| Binding Form | Syntax | Description |
|--------------|--------|-------------|
| By value | `auto [x, y] = ...;` | Copies elements to new variables |
| Lvalue reference | `auto& [x, y] = ...;` | Binds to references of the original object |
| Read-only reference | `const auto& [x, y] = ...;` | Const reference, avoids copying |
| Forwarding reference | `auto&& [x, y] = ...;` | Perfect forwarding semantics |
| Array destructuring | `int arr[3]; auto& [x, y, z] = arr;` | Binds to array elements (count must match) |
| Pair destructuring | `auto& [key, val] = pair;` | Binds to `first`/`second` of a pair |
| Tuple destructuring | `auto& [a, b] = tuple;` | Binds to tuple-like elements |
| Struct destructuring | `auto& [x, y] = struct_obj;` | Binds to public data members (declaration order) |

## Minimal Example

```cpp
#include <iostream>
#include <tuple>

int main() {
    // 1. Pair destructuring
    std::pair<int, int> coord{10, 20};
    auto& [x, y] = coord; // Bind by reference
    x = 30;               // Modifies coord.first

    // 2. Struct destructuring
    struct Sensor { int id; float value; };
    Sensor s{1, 3.14f};
    auto [id, val] = s;   // Bind by value (copy)

    // 3. Array destructuring
    int data[3] = {1, 2, 3};
    auto& [a, b, c] = data;

    std::cout << x << ", " << y << "\n"; // 30, 20
}
```

## Embedded Applicability: High

- Pure compile-time syntactic sugar with zero runtime overhead; generated code is equivalent to manual field access.
- Simplifies unpacking of multi-field structures like register sets or sensor data, improving readability.
- Use `const auto&` to avoid copying, ideal for read-only access to hardware-mapped structs.
- C++17 is fully supported in mainstream embedded toolchains (GCC 7+, ARM Clang 6+).

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 7 | 4.0 | 19.1 |

## See Also

- [Tutorial: Structured Bindings](../../vol2-modern-features/ch05-structured-bindings/01-structured-bindings.md)
- [cppreference: Structured binding declaration](https://en.cppreference.com/w/cpp/language/structured_binding)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
