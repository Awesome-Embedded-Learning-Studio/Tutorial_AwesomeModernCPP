---
chapter: 99
cpp_standard:
- 11
- 14
- 17
- 20
- 23
description: Iterate over all elements in a container or array using more concise
  syntax
difficulty: beginner
order: 7
reading_time_minutes: 1
tags:
- host
- cpp-modern
- beginner
title: Range-based for loop
translation:
  source: documents/cpp-reference/core-language/07-range-for.md
  source_hash: 7da4ee12b4ec2364d3813b54c9f41abddd7080d87b219bcd9ec56228fa562f92
  translated_at: '2026-06-16T03:28:58.111263+00:00'
  engine: anthropic
  token_count: 325
---
# Range-based for Loop (C++11)

## In a Nutshell

Syntactic sugar that allows us to traverse all elements of a container or array without manually writing iterators, making loop code more concise and less error-prone.

## Header

None (language feature)

## Core API Quick Reference

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Read-only traversal | `for (auto item : container)` | Copies each element to `item` |
| Reference traversal | `for (auto& item : container)` | Accesses elements via lvalue reference (mutable) |
| Const reference traversal | `for (const auto& item : container)` | Avoids copying and prevents modification |
| Initialization statement | `for (init; auto& item : container)` | Executes initialization before the loop (since C++20) |
| Array traversal | `for (auto& item : array)` | Supports native arrays of known size |

## Minimal Example

```cpp
#include <vector>
#include <iostream>

int main() {
    std::vector<int> data = {1, 2, 3, 4, 5};

    // Read-only traversal (copies elements)
    for (auto val : data) {
        std::cout << val << " ";
    }
    // Output: 1 2 3 4 5

    // Reference traversal (modifies elements)
    for (auto& val : data) {
        val *= 2;
    }

    // Const reference traversal (avoids copying)
    for (const auto& val : data) {
        std::cout << val << " ";
    }
    // Output: 2 4 6 8 10

    // C++20: Initialization statement
    for (auto idx = 0; const auto& val : data) {
        std::cout << idx << ": " << val << "\n";
        ++idx;
    }
}
```

## Embedded Applicability: High

- Zero-overhead abstraction: Compiles to code equivalent to hand-written iterator or index loops, with no extra runtime cost.
- Concise syntax reduces errors caused by out-of-bounds indices or invalid iterators.
- Very practical for compile-time traversal of `std::array` when combined with `constexpr`.
- Note: Be cautious of lifetime issues when traversing member functions that return temporary objects (this is UB prior to C++23).

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.6 | 3.0   | 2010 |

## See Also

- [cppreference: Range-based for loop](https://en.cppreference.com/w/cpp/language/range-for)

---

*Part of the content references [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
