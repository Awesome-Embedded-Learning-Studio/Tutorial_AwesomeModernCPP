---
chapter: 99
cpp_standard:
- 20
- 23
description: Non-owning view of a contiguous sequence, a zero-overhead alternative
  to passing pointer and length
difficulty: beginner
order: 1
reading_time_minutes: 2
tags:
- host
- cpp-modern
- beginner
title: std::span
translation:
  engine: anthropic
  source: documents/cpp-reference/containers/01-span.md
  source_hash: 08998f44d647e2c9ee6712ca4342ea677f4175754905abfb5e8db85473ef2cd7
  token_count: 472
  translated_at: '2026-06-15T09:06:03.340062+00:00'
---
# std::span (C++20)

## In a nutshell

A lightweight, non-owning view that safely references a contiguous sequence of memory, replacing the traditional method of passing pointers alongside length parameters.

## Header

`#include <span>`

## Core API Cheat Sheet

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Constructor | `template<class T, size_t E = dynamic_extent> class span` | Template class supporting static or dynamic extent |
| Get pointer | `T* data() const` | Access underlying contiguous storage |
| Element count | `size_t size() const` | Returns the number of elements |
| Byte size | `size_t size_bytes() const` | Returns the size of the sequence in bytes |
| Is empty | `bool empty() const` | Checks if the sequence is empty |
| Subscript | `reference operator[](size_t idx) const` | Access specified element (no bounds checking) |
| First element | `reference front() const` | Access the first element |
| Last element | `reference back() const` | Access the last element |
| Take first N | `template<size_t C> constexpr span<element_type, C> first() const` | Get a sub-view of the first N elements |
| Take sub-view | `template<size_t O, size_t C> constexpr span<element_type, C> subspan() const` | Get a sub-view with specified offset and length |

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

## Embedded Applicability: High

- Zero-overhead abstraction: Only contains a pointer and a size (or compile-time constant extent), with no heap allocation.
- Perfect replacement for raw pointer parameters: Unifies interfaces for arrays, `std::array`, and `std::vector`, improving safety.
- `TriviallyCopyable` type (explicitly required in C++23, met by mainstream implementations prior), making it safe for use with ISRs and DMA buffers.
- `size_bytes()` and `as_bytes()` greatly simplify hardware register mapping and low-level byte-level data processing.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| TBD | TBD | TBD |

## See Also

- [Tutorial: Deep Dive into span](../../vol3-standard-library/08-span.md)
- [cppreference: std::span](https://en.cppreference.com/w/cpp/container/span)

---

*Part of the content references [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
