---
title: std::array
description: Fixed-size contiguous container, zero-overhead wrapper for C-style arrays
chapter: 99
order: 4
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
---
# std::array (C++11)

## In a Nutshell

A fixed-size array that does not decay to a pointer. It delivers the performance of a C-style array while supporting standard container interfaces like `size()`, iterators, and assignment.

## Header

`#include <array>`

## Core API Quick Reference

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Element access | `reference at(size_type pos)` | Element access with bounds checking |
| Element access | `reference operator[](size_type pos)` | Element access without bounds checking |
| First element | `reference front()` | Access the first element |
| Last element | `reference back()` | Access the last element |
| Underlying pointer | `T* data() noexcept` | Direct access to the underlying array pointer |
| Fill | `void fill(const T& value)` | Fill all elements with a specified value |
| Size | `constexpr size_type size() noexcept` | Return the number of elements (compile-time constant) |
| Empty check | `constexpr bool empty() noexcept` | Check if the array is empty (true when N==0) |
| Swap | `void swap(array& other)` | Swap the contents of two arrays |
| Begin iterator | `iterator begin() noexcept` | Return an iterator to the beginning |

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

- A zero-overhead abstraction that compiles down to the exact same code as a C-style array, introducing no heap allocation.
- `size()` is a compile-time constant, making it usable in template metaprogramming and static assertions.
- Supports `constexpr`, making it ideal for building lookup tables at compile time.
- The built-in bounds checking of `at()` simplifies debugging, and it can be removed in Release builds.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.4 | 3.1 | 19.0 |

## See Also

- [Tutorial: std::array In Depth](../../vol3-standard-library/01-array.md)
- [cppreference: std::array](https://en.cppreference.com/w/cpp/container/array)

---

*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
