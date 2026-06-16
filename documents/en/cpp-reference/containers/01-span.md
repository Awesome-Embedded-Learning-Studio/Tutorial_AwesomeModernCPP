---
chapter: 99
cpp_standard:
- 20
- 23
description: Non-owning view of a contiguous sequence, a zero-overhead alternative
  to passing pointer and length arguments
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
  source_hash: eaaf5fb818f874e391db98bc97b1e7e222554259ccfceb053bbf630629b64702
  translated_at: '2026-06-16T03:28:09.929344+00:00'
  engine: anthropic
  token_count: 475
---
# std::span (C++20)

## In a Nutshell

A lightweight, non-owning view for safely referencing a contiguous sequence of memory, serving as a modern replacement for passing traditional pointer-plus-length arguments.

## Header

`#include <span>`

## Core API Cheat Sheet

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Constructor | `template<class T, size_t E = dynamic_extent> class span` | Template class supporting static or dynamic extent |
| Get pointer | `T* data() const` | Access underlying contiguous storage |
| Element count | `size_t size() const` | Returns number of elements |
| Byte size | `size_t size_bytes() const` | Returns size in bytes of the sequence |
| Is empty | `bool empty() const` | Checks if the sequence is empty |
| Subscript | `reference operator[](size_t idx) const` | Access specified element (no bounds checking) |
| First element | `reference front() const` | Access the first element |
| Last element | `reference back() const` | Access the last element |
| Take first N | `template<size_t C> constexpr span<element_type, C> first() const` | Get a sub-view of the first N elements |
| Subspan | `template<size_t O, size_t C> constexpr span<element_type, C> subspan() const` | Get a sub-view with specified offset and length |

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

- Zero-overhead abstraction: Contains only a pointer and a size (or compile-time constant size), with no heap allocation.
- Perfect replacement for raw pointer arguments: Unifies interfaces for arrays, `std::array`, and `std::vector`, enhancing safety.
- Trivially copyable type (explicitly required since C++23, though mainstream implementations already satisfied this), making it safe for use with ISRs and DMA buffers.
- `std::as_bytes` and `std::as_writable_bytes` significantly simplify hardware register mapping and low-level byte-oriented data processing.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| TBA | TBA | TBA |

## See Also

- [Tutorial: Deep Dive into span](../../vol3-standard-library/08-span.md)
- [cppreference: std::span](https://en.cppreference.com/w/cpp/container/span)

---

*Part of the content references [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
