---
chapter: 99
cpp_standard:
- 11
- 14
- 17
- 20
- 23
description: Fixed-size contiguous container, a zero-overhead wrapper over C-style
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
  source_hash: c4f9c1234850acc66cff7bcd0e84f309d15c146bd76b9de9a49449379b9e1d11
  translated_at: '2026-09-26T17:12:11+00:00'
  engine: anthropic
  token_count: 500
---
# std::array (C++11)

## In a Nutshell

A fixed-size array that never decays into a pointer: it delivers the performance of a C-style array while supporting standard container interfaces such as size(), iterators, and assignment.

## Header File

`#include <array>`

## Core API Cheat Sheet

| Operation | Signature | Description |
|------|------|------|
| Element access | `reference at(size_type pos)` | Bounds-checked element access |
| Element access | `reference operator[](size_type pos)` | Unchecked element access |
| First element | `reference front()` | Accesses the first element |
| Last element | `reference back()` | Accesses the last element |
| Underlying pointer | `T* data() noexcept` | Direct access to the underlying array pointer |
| Fill | `void fill(const T& value)` | Fills every element with the given value |
| Size | `constexpr size_type size() noexcept` | Returns the number of elements (a compile-time constant) |
| Empty check | `constexpr bool empty() noexcept` | Checks whether the array is empty (true when N == 0) |
| Swap | `void swap(array& other)` | Swaps the contents of two arrays |
| Begin iterator | `iterator begin() noexcept` | Returns an iterator pointing to the beginning |

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
        std::cout << v << ' '; // outputs: 42 0 0
    std::cout << "\nsize: " << arr.size(); // outputs: size: 3
}
```

## Embedded Suitability: High

- A zero-overhead abstraction: after compilation it is identical to a C-style array and introduces no heap allocation
- `size()` is a compile-time constant, usable in template metaprogramming and static assertions
- Supports `constexpr`, making it a good fit for building lookup tables at compile time
- The built-in bounds check in `at()` is convenient for debugging and can be removed in Release builds

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.4 | 3.1 | 19.0 |

## See Also

- [Tutorial: std::array in Depth](../../vol3-standard-library/containers/02-array.md)
- [cppreference: std::array](https://en.cppreference.com/w/cpp/container/array)

---

*Part of the content is referenced from [cppreference.com](https://en.cppreference.com/) and is licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
