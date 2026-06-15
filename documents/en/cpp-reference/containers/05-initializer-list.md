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
  engine: anthropic
  source: documents/cpp-reference/containers/05-initializer-list.md
  source_hash: aee3f97cd1a75fd47d8d15060f08cf186e4ff14367c651860d2881fd20178a53
  token_count: 508
  translated_at: '2026-06-15T09:06:26.428918+00:00'
---
<!--
Reference Card Template
For feature quick reference pages under documents/cpp-reference/.
Unlike article-template.md, reference cards use a refined, structured format and do not require a narrative style.

Tag usage rules:
1. Must include 1 platform tag (use 'host' for reference cards)
2. Must include 1 difficulty tag
3. Must include at least 1 topic tag
4. Select from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# std::initializer_list (C++11)

## In a Nutshell

A lightweight, read-only proxy object that allows you to conveniently pass an arbitrary number of initial values of the same type to containers or custom classes using brace initialization `{}`.

## Header

`#include <initializer_list>`

## Core API Cheat Sheet

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Constructor | `initializer_list() noexcept` | Creates an empty list (usually implicitly constructed by the compiler) |
| Element Count | `std::size_t size() const noexcept` | Returns the number of elements in the list |
| Begin Pointer | `const T* begin() const noexcept` | Pointer to the first element |
| End Pointer | `const T* end() const noexcept` | Pointer to one past the last element |
| Begin Iterator | `const T* begin(std::initializer_list<T> il) noexcept` | Overloaded `std::begin` |
| End Iterator | `const T* end(std::initializer_list<T> il) noexcept` | Overloaded `std::end` |

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

- The underlying implementation typically contains only a pointer and a length (or two pointers), resulting in minimal memory overhead.
- Copying `std::initializer_list` does not copy the underlying array; it only copies the proxy object itself, incurring no additional allocation overhead.
- The underlying array may be stored in read-only memory, making it suitable for initializing static configuration tables placed in ROM.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| TBA | TBA | TBA |

## See Also

- [Tutorial: Initializer Lists](../../vol3-standard-library/11-initializer-lists.md)
- [cppreference: std::initializer_list](https://en.cppreference.com/w/cpp/utility/initializer_list)

---

*Portions of content adapted from [cppreference.com](https://en.cppreference.com/), available under the [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) license*
