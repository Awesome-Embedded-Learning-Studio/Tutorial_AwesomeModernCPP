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
  source_hash: a3a6f1b6714ab57986aa909b0ca030e142f8f852c35db28a7e3149bbe1246e6c
  translated_at: '2026-06-24T00:25:58.133697+00:00'
  engine: anthropic
  token_count: 515
---
<!--
Reference Card Template
For feature cheat sheets under documents/cpp-reference/.
Unlike article-template.md, reference cards use a concise, structured format and do not require a narrative style.

Tag usage rules:
1. Must include 1 platform tag (use 'host' for reference cards)
2. Must include 1 difficulty tag
3. Must include at least 1 topic tag
4. Select from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# std::initializer_list (C++11)

## In a Nutshell

A lightweight, read-only proxy object that allows us to conveniently pass an arbitrary number of homogeneous initial values to containers or custom classes using braces `{}`.

## Header

`#include <initializer_list>`

## Core API Quick Reference

| Operation | Signature | Description |
|------|------|------|
| Constructor | `initializer_list() noexcept` | Creates an empty list (usually implicitly constructed by the compiler) |
| Element Count | `std::size_t size() const noexcept` | Returns the number of elements in the list |
| Begin Pointer | `const T* begin() const noexcept` | Pointer to the first element |
| End Pointer | `const T* end() const noexcept` | Pointer to one past the last element |
| Begin Iterator | `const T* begin(std::initializer_list<T> il) noexcept` | Overload of `std::begin` |
| End Iterator | `const T* end(std::initializer_list<T> il) noexcept` | Overload of `std::end` |

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
- Copying a `std::initializer_list` does not copy the underlying array; it only copies the proxy object itself, incurring no additional allocation overhead.
- The underlying array may reside in read-only memory, making it suitable for initializing static configuration tables stored in ROM.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| To be added | To be added | To be added |

## See Also

- [Tutorial: Initializer Lists](../../vol3-standard-library/containers/11-initializer-lists.md)
- [cppreference: std::initializer_list](https://en.cppreference.com/w/cpp/utility/initializer_list)

---

*Part of the content references [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
