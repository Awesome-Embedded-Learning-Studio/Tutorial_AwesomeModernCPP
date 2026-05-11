# std::span (C++20)

## In a Nutshell

A lightweight, non-owning view that safely references a contiguous block of memory, replacing the traditional approach of passing a pointer paired with a length.

## Header

`#include <span>`

## Core API Cheat Sheet

| Operation | Signature | Description |
|------|------|------|
| Construction | `template<class T, size_t E = dynamic_extent> class span` | Template class supporting static or dynamic extents |
| Get pointer | `T* data() const` | Access the underlying contiguous storage |
| Element count | `size_t size() const` | Returns the number of elements |
| Size in bytes | `size_t size_bytes() const` | Returns the size of the sequence in bytes |
| Check if empty | `bool empty() const` | Checks if the sequence is empty |
| Subscript access | `reference operator[](size_t idx) const` | Accesses the specified element (no bounds checking) |
| First element | `reference front() const` | Accesses the first element |
| Last element | `reference back() const` | Accesses the last element |
| Take first N | `template<size_t C> constexpr span<element_type, C> first() const` | Obtains a subview of the first N elements |
| Take subview | `template<size_t O, size_t C> constexpr span<element_type, C> subspan() const` | Obtains a subview with the specified offset and length |

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

- Zero-overhead abstraction: contains only a pointer and a size (or a compile-time constant extent), with no heap allocation
- Perfect replacement for raw pointer parameters: unifies interfaces for arrays, `std::array`, and `std::vector`, improving safety
- `TriviallyCopyable` type (explicitly required since C++23, though mainstream implementations already satisfied this beforehand), safe to use for interrupt and DMA (Direct Memory Access) buffer operations
- `size_bytes()` and `as_bytes()` greatly simplify hardware register mapping and low-level byte-level data processing

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| TBA | TBA | TBA |

## See Also

- [Tutorial: span In Depth](../../vol3-standard-library/02-span.md)
- [cppreference: std::span](https://en.cppreference.com/w/cpp/container/span)

---

*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
