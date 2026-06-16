---
chapter: 99
cpp_standard:
- 11
- 14
- 17
- 20
- 23
description: A fixed-size, contiguous container, a zero-overhead wrapper for C-style
  arrays
difficulty: beginner
order: 4
reading_time_minutes: 2
tags:
- host
- cpp-modern
- beginner
title: std::array
translation:
  source: documents/cpp-reference/containers/04-array.md
  source_hash: 44d8a9ce4846a9281b7a5a7c14e494e99f0e233d4dc490e9ac3e5500a72afbeb
  translated_at: '2026-06-16T03:28:25.553320+00:00'
  engine: anthropic
  token_count: 424
---
# std::array (C++11)

## In a nutshell

A fixed-size array that does not decay into a pointer. It offers the performance of a C-style array while supporting standard container interfaces such as `size()`, iterators, and assignment.

## Header file

```cpp
#include <array>
```

## Core API Cheat Sheet

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Element access | `at(size_type)` | Element access with bounds checking |
| Element access | `operator[]` | Element access without bounds checking |
| First element | `front()` | Access the first element |
| Last element | `back()` | Access the last element |
| Underlying pointer | `data()` | Direct access to the underlying array pointer |
| Fill | `fill(const T&)` | Fill all elements with a specified value |
| Size | `size()` | Returns the number of elements (compile-time constant) |
| Empty check | `empty()` | Checks if the array is empty (true if N==0) |
| Swap | `swap(array&)` | Swaps the contents of two arrays |
| Begin iterator | `begin()` | Returns an iterator to the beginning |

## Minimal Example

```cpp
#include <array>
#include <iostream>

int main() {
    // Initialize with an initializer list
    std::array<int, 5> arr = {1, 2, 3, 4, 5};

    // Access elements with bounds checking
    arr.at(0) = 10;

    // Range-based for loop
    for (const auto& val : arr) {
        std::cout << val << " ";
    }
    // Output: 10 2 3 4 5
}
```

## Embedded Applicability: High

- Zero-overhead abstraction; compiles to code identical to a C-style array without introducing heap allocation.
- `size()` is a compile-time constant, making it suitable for template metaprogramming and static assertions.
- Supports `constexpr`, allowing for the construction of lookup tables at compile time.
- Built-in bounds checking via `at()` facilitates debugging and can be removed in Release builds.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.4 | 3.1 | 19.0 |

## See Also

- [Tutorial: Deep Dive into std::array](../../vol3-standard-library/02-array.md)
- [cppreference: std::array](https://en.cppreference.com/w/cpp/container/array)

---

*Part of the content is referenced from [cppreference.com](https://en.cppreference.com/) and is licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
