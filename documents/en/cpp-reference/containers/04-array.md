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
  source: documents/cpp-reference/containers/04-array.md
  source_hash: c4f9c1234850acc66cff7bcd0e84f309d15c146bd76b9de9a49449379b9e1d11
  translated_at: '2026-06-24T00:25:51.893953+00:00'
  engine: anthropic
  token_count: 427
---
# std::array (C++11)

## In a Nutshell

A fixed-size array that does not decay into a pointer. It offers the performance of a C-style array while supporting standard container interfaces such as `size()`, iterators, and assignment.

## Header

`#include <array>`

## Core API Quick Reference

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Element access | `reference at(size_type pos)` | Access element with bounds checking |
| Element access | `reference operator[](size_type pos)` | Access element without bounds checking |
| First element | `reference front()` | Access the first element |
| Last element | `reference back()` | Access the last element |
| Raw pointer | `T* data() noexcept` | Direct access to the underlying array pointer |
| Fill | `void fill(const T& value)` | Fill all elements with a specified value |
| Size | `constexpr size_type size() noexcept` | Returns the number of elements (compile-time constant) |
| Empty check | `constexpr bool empty() noexcept` | Checks if the array is empty (true if N==0) |
| Swap | `void swap(array& other)` | Swaps the contents of two arrays |
| Begin iterator | `iterator begin() noexcept` | Returns an iterator to the beginning |

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

## Embedded Applicability: High

- Zero-overhead abstraction; compiles to code identical to C-style arrays without introducing heap allocation.
- `size()` is a compile-time constant, making it suitable for template metaprogramming and static assertions.
- Supports `constexpr`, ideal for building lookup tables at compile time.
- Built-in bounds checking via `at()` facilitates debugging, and can be removed in Release builds.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.4 | 3.1 | 19.0 |

## See Also

- [Tutorial: std::array Deep Dive](../../vol3-standard-library/containers/02-array.md)
- [cppreference: std::array](https://en.cppreference.com/w/cpp/container/array)

---

*Part of the content references [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
