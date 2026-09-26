---
chapter: 99
cpp_standard:
- 20
- 23
description: A non-owning view of contiguous sequences, a zero-overhead replacement
  for pointer-plus-length parameter passing
difficulty: beginner
order: 1
reading_time_minutes: 2
tags:
- host
- cpp-modern
- beginner
title: std::span
translation:
  source: documents/cpp-reference/containers/01-span.md
  source_hash: 3e8128e3808bb22e8fbab10998a924b452b7ec9337a3cc919ee59601e44b5f9a
  translated_at: '2026-09-26T17:12:24+00:00'
  engine: anthropic
  token_count: 1000
---
# std::span (C++20)

## In a nutshell

A lightweight non-owning view that safely refers to a stretch of contiguous memory, replacing the traditional way of passing a pointer plus a length around.

## Header file

`#include <span>`

## Core API Cheat Sheet

| Operation | Signature | Description |
|------|------|------|
| Construction | `template<class T, size_t E = dynamic_extent> class span` | Class template supporting a static or dynamic length |
| Get pointer | `T* data() const` | Accesses the underlying contiguous storage |
| Element count | `size_t size() const` | Returns the number of elements |
| Byte size | `size_t size_bytes() const` | Returns the number of bytes the sequence occupies |
| Is empty | `bool empty() const` | Checks whether the sequence is empty |
| Subscript access | `reference operator[](size_t idx) const` | Accesses the element at the given index (no bounds checking) |
| First element | `reference front() const` | Accesses the first element |
| Last element | `reference back() const` | Accesses the last element |
| Take first N | `template<size_t C> constexpr span<element_type, C> first() const` | Gets a subview of the first N elements |
| Take subview | `template<size_t O, size_t C> constexpr span<element_type, C> subspan() const` | Gets a subview at the specified offset and length |

## Minimal Example

```cpp
// Standard: C++20
#include <iostream>
#include <span>

void print(std::span<const int> s) {
    for (int v : s) std::cout << v << ' ';
    std::cout << '\n';
}

int main() {
    int arr[] = {1, 2, 3, 4, 5};
    std::span<int> s(arr);
    print(s);            // 1 2 3 4 5
    print(s.first(3));   // 1 2 3
    print(s.subspan(2)); // 3 4 5
}
```

## Embedded Suitability: High

- Zero-overhead abstraction: holds nothing but a pointer and a length (or a compile-time constant length), with no heap allocation
- A perfect replacement for passing raw pointers around: unifies the interface of arrays, `std::array`, and `std::vector`, improving safety
- A `TriviallyCopyable` type (explicitly required since C++23; mainstream implementations already satisfied this before that), safe to use in interrupt and DMA buffer operations
- `size_bytes()` and `as_bytes()` greatly simplify mapping hardware registers and doing low-level byte-wise data processing

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| To be added | To be added | To be added |

## See Also

- [Tutorial: Deep Dive into span](../../vol3-standard-library/containers/08-span.md)
- [cppreference: std::span](https://en.cppreference.com/w/cpp/container/span)

---

*Part of the content is referenced from [cppreference.com](https://en.cppreference.com/) and is licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
