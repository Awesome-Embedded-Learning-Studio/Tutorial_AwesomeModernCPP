---
chapter: 99
cpp_standard:
- 11
- 14
- 17
- 20
- 23
description: Fixed-size contiguous container, zero-overhead wrapper for C-style arrays
difficulty: beginner
order: 4
reading_time_minutes: 2
tags:
- host
- cpp-modern
- beginner
title: std::array
translation:
  engine: anthropic
  source: documents/cpp-reference/containers/04-array.md
  source_hash: e5f7f56e6b65e001f05b42cb5ca16f25351de75e2af2374cc5c7eb21cf5a299d
  token_count: 420
  translated_at: '2026-06-15T09:06:13.960964+00:00'
---
# std::array (C++11)

## In a Nutshell

A fixed-size array that does not decay into a pointer. It offers the performance of a C-style array while supporting standard container interfaces such as `size()`, iterators, and assignment.

## Header

`#include <array>`

## Core API Cheat Sheet

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Element access | `reference at(size_type pos)` | Element access with bounds checking |
| Element access | `reference operator[](size_type pos)` | Element access without bounds checking |
| First element | `reference front()` | Access the first element |
| Last element | `reference back()` | Access the last element |
| Underlying pointer | `T* data() noexcept` | Direct access to the underlying array pointer |
| Fill | `void fill(const T& value)` | Fill all elements with a specified value |
| Size | `constexpr size_type size() noexcept` | Returns the number of elements (compile-time constant) |
| Empty check | `constexpr bool empty() noexcept` | Checks if empty (true when N==0) |
| Swap | `void swap(array& other)` | Swaps the contents of two arrays |
| Iterator start | `iterator begin() noexcept` | Returns an iterator to the beginning |

## Minimal Example

```cpp
#include <array>
#include <iostream>
// Standard: C++11
int main() {
    std::array<int, 3> arr = {1, 2, 3};
    arr.fill(0);
    arr[0] = 42;
    for (const auto& v : arr)
        std::cout << v << ' '; // 输出: 42 0 0
    std::cout << "\nsize: " << arr.size(); // 输出: size: 3
}
```

## Embedded Suitability: High

- Zero-overhead abstraction; compiles to identical code as a C-style array without introducing heap allocation.
- `size()` is a compile-time constant, making it suitable for template metaprogramming and static assertions.
- Supports `constexpr`, allowing us to build lookup tables at compile time.
- Built-in bounds checking via `at()` facilitates debugging and can be removed in Release builds.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.4 | 3.1 | 19.0 |

## See Also

- [Tutorial: std::array Deep Dive](../../vol3-standard-library/02-array.md)
- [cppreference: std::array](https://en.cppreference.com/w/cpp/container/array)

---

*Part of this content is referenced from [cppreference.com](https://en.cppreference.com/) and licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
