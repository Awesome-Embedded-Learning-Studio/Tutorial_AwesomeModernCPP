---
title: Range-based for loop
description: Iterate over all elements in a container or array with more concise syntax
chapter: 99
order: 7
tags:
- host
- cpp-modern
- beginner
difficulty: beginner
cpp_standard:
- 11
- 14
- 17
- 20
- 23
translation:
  source: documents/cpp-reference/core-language/07-range-for.md
  source_hash: 71206a5364cd1d7ea15a3be0e9749f68e557ff2aa400389ff554bdf4d3c4b2b9
  translated_at: '2026-05-26T10:15:51.251212+00:00'
  engine: anthropic
  token_count: 320
---
# Range-based for Loop (C++11)

## In a Nutshell

Syntactic sugar for traversing all elements of a container or array without manually writing iterators, making loop code more concise and less error-prone.

## Header

None (language feature)

## Core API Quick Reference

| Operation | Signature | Description |
|------|------|------|
| Read-only traversal | `for (auto item : range)` | Copies each element to `item` |
| Reference traversal | `for (auto& item : range)` | Accesses elements via lvalue reference (modifiable) |
| Const reference traversal | `for (const auto& item : range)` | Avoids copying and prevents modification |
| Init statement | `for (init; auto& item : range)` | Executes initialization before the loop, since C++20 |
| Array traversal | `for (auto item : arr)` | Supports native arrays of known size |

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

- Zero-overhead abstraction: compiles down to code completely equivalent to hand-written iterator or index loops, with no extra runtime cost
- Concise syntax reduces errors caused by out-of-bounds indexing or invalidated iterators
- Combined with `constexpr` arrays, compile-time traversal is also very practical
- Note: be cautious of lifetime issues when iterating over member functions that return temporary objects (undefined behavior (UB) prior to C++23)

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.6 | 3.0   | 2010 |

## See Also

- [cppreference: Range-based for loop](https://en.cppreference.com/w/cpp/language/range-for)

---

*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
