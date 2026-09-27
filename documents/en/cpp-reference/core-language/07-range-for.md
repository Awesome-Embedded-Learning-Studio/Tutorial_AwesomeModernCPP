---
chapter: 99
cpp_standard:
- 11
- 14
- 17
- 20
- 23
description: Iterate over all elements in a container or array with more concise
  syntax
difficulty: beginner
order: 7
reading_time_minutes: 1
tags:
- host
- cpp-modern
- beginner
title: Range-Based for Loop
translation:
  source: documents/cpp-reference/core-language/07-range-for.md
  source_hash: 7da4ee12b4ec2364d3813b54c9f41abddd7080d87b219bcd9ec56228fa562f92
  translated_at: '2026-09-27T01:38:09+00:00'
  engine: anthropic
  token_count: 300
---
# Range-Based for Loop (C++11)

## In a nutshell

Syntactic sugar that lets us iterate over every element of a container or array without hand-writing iterators, making loop code more concise and less error-prone.

## Header file

None (language feature)

## Core API Cheat Sheet

| Operation | Signature | Description |
|------|------|------|
| Read-only traversal | `for (auto item : range)` | Copies each element into `item` |
| Reference traversal | `for (auto& item : range)` | Accesses elements via lvalue reference (modifiable) |
| Const reference traversal | `for (const auto& item : range)` | Avoids copies and prevents modification |
| Init statement | `for (init; auto& item : range)` | Runs initialization before the loop starts (since C++20) |
| Array traversal | `for (auto item : arr)` | Works with native arrays of known size |

## Minimal Example

```cpp
#include <vector>
#include <iostream>
// Standard: C++11
int main() {
    std::vector<int> v = {1, 2, 3};
    for (const auto& x : v) {
        std::cout << x << ' ';
    }
    return 0;
}
```

## Embedded Applicability: High

- Zero-overhead abstraction: compiles to code fully equivalent to a hand-written iterator or index loop, with no extra runtime cost
- The concise syntax cuts down on bugs caused by out-of-bounds indices or invalidated iterators
- Also very practical for compile-time traversal of `constexpr` arrays
- Caution: when the range comes from a member function returning a temporary, watch out for lifetime issues (UB before C++23)

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.6 | 3.0   | 2010 |

## See Also

- [cppreference: Range-based for loop](https://en.cppreference.com/w/cpp/language/range-for)

---

*Part of the content is referenced from [cppreference.com](https://en.cppreference.com/) and is licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
