---
chapter: 99
cpp_standard:
- 11
- 14
- 17
- 20
- 23
description: A lightweight proxy type used when initializing objects or passing arguments with curly braces `{}`
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
  translated_at: '2026-09-26T17:12:49+00:00'
  engine: anthropic
  token_count: 850
---
<!--
Reference Card Template
For feature cheat sheets under documents/cpp-reference/.
Unlike article-template.md, reference cards use a concise, structured format with no narrative style required.

Tag usage rules:
1. Must include 1 platform tag (reference cards consistently use host)
2. Must include 1 difficulty tag
3. Must include at least 1 topic tag
4. Select from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# std::initializer_list (C++11)

## In a Nutshell

A lightweight, read-only proxy object that lets you conveniently pass any number of same-typed initial values to containers or custom classes using curly braces `{}`.

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
    Container c = {1, 2, 3}; // implicitly constructs the initializer_list
    c.append({4, 5});
    for (int x : c.v) std::cout << x << ' ';
}
```

## Embedded Applicability: High

- The underlying implementation usually holds just a pointer and a length (or two pointers), so the memory overhead is minimal
- Copying a `std::initializer_list` does not copy the underlying array — only the proxy object itself is copied, with no extra allocation cost
- The underlying array may live in read-only memory, which makes it a good fit for initializing static configuration tables placed in ROM

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| To be added | To be added | To be added |

## See Also

- [Tutorial: Initializer Lists](../../vol3-standard-library/containers/11-initializer-lists.md)
- [cppreference: std::initializer_list](https://en.cppreference.com/w/cpp/utility/initializer_list)

---

*Some content references [cppreference.com](https://en.cppreference.com/), used under the [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) license*
