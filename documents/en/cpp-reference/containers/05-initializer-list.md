---
chapter: 99
cpp_standard:
- 11
- 14
- 17
- 20
- 23
description: Lightweight proxy type used when initializing objects or passing arguments
  with curly braces `{}`
difficulty: beginner
order: 5
reading_time_minutes: 2
tags:
- host
- cpp-modern
- beginner
title: std::initializer_list
translation:
  source: documents/cpp-reference/containers/05-initializer-list.md
  source_hash: 197c5953f5761c391451a910096e121575d8ae79b09644f845eb7f7d3e1dcf00
  translated_at: '2026-06-16T03:28:25.608053+00:00'
  engine: anthropic
  token_count: 513
---
<!--
Reference Card Template
For feature cheat sheets under documents/cpp-reference/.
Unlike article-template.md, reference cards follow a refined, structured format and do not require a narrative style.

Tag usage rules:
1. Must include 1 platform tag (use 'host' for reference cards)
2. Must include 1 difficulty tag
3. Must include at least 1 topic tag
4. Select from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

---
title: "std::initializer_list (C++11)"
description: "A lightweight proxy object for passing brace-enclosed lists of initial values."
tags:

- cpp11
- stl
- host
- beginner
- type

---

# std::initializer_list (C++11)

## In a nutshell

A lightweight, read-only proxy object that allows us to conveniently pass an arbitrary number of initial values of the same type to containers or custom classes using brace-enclosed `init` lists.

## Header

`#include <initializer_list>`

## Core API Cheat Sheet

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Constructor | `initializer_list() noexcept` | Creates an empty list (usually implicitly constructed by the compiler) |
| Element count | `std::size_t size() const noexcept` | Returns the number of elements in the list |
| Begin pointer | `const T* begin() const noexcept` | Pointer to the first element |
| End pointer | `const T* end() const noexcept` | Pointer to one past the last element |
| Begin iterator | `const T* begin(std::initializer_list<T> il) noexcept` | Overloaded `begin` |
| End iterator | `const T* end(std::initializer_list<T> il) noexcept` | Overloaded `end` |

## Minimal Example

```cpp
// Standard: C++11
#include <iostream>
#include <initializer_list>
#include <vector>

struct Container {
    std::vector<int> v;
    Container(std::initializer_list<int> l) : v(l) {}
    void append(std::initializer_list<int> l) {
        v.insert(v.end(), l.begin(), l.end());
    }
};

int main() {
    Container c = {1, 2, 3}; // 隐式构造 initializer_list
    c.append({4, 5});
    for (int x : c.v) std::cout << x << ' ';
}
```

## Embedded Applicability: High

- The underlying implementation typically contains only a pointer and a size (or two pointers), resulting in minimal memory overhead.
- Copying an `std::initializer_list` does not copy the underlying array; it only copies the proxy object itself, incurring no allocation overhead.
- The underlying array may reside in read-only memory, making it suitable for initializing static configuration tables stored in ROM.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| TBA | TBA | TBA |

## See Also

- [Tutorial: Initializer Lists](../../vol3-standard-library/11-initializer-lists.md)
- [cppreference: std::initializer_list](https://en.cppreference.com/w/cpp/utility/initializer_list)

---

*Part of the content is referenced from [cppreference.com](https://en.cppreference.com/) and licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
