---
title: std::initializer_list
description: A lightweight proxy type when initializing objects or passing parameters
  using curly braces `{}`
chapter: 99
order: 5
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
<!--
Reference Card Template
Used for feature quick-reference pages under documents/cpp-reference/.
Unlike article-template.md, reference cards use a concise, structured format without a narrative style.

Tag usage rules:
1. Must include exactly 1 platform tag (reference cards uniformly use host)
2. Must include exactly 1 difficulty tag
3. Must include at least 1 topic tag
4. Selected from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# std::initializer_list (C++11)

## In a Nutshell

A lightweight, read-only proxy object that allows us to pass an arbitrary number of same-type initial values to containers or custom classes using braces `{}`.

## Header

`#include <initializer_list>`

## Core API Quick Reference

| Operation | Signature | Description |
|------|------|------|
| Construction | `initializer_list() noexcept` | Creates an empty list (typically implicitly constructed by the compiler) |
| Element count | `std::size_t size() const noexcept` | Returns the number of elements in the list |
| Begin pointer | `const T* begin() const noexcept` | Pointer to the first element |
| End pointer | `const T* end() const noexcept` | Pointer to one past the last element |
| Begin iterator | `const T* begin(std::initializer_list<T> il) noexcept` | Overloaded `std::begin` |
| End iterator | `const T* end(std::initializer_list<T> il) noexcept` | Overloaded `std::end` |

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

- The underlying implementation typically contains only a pointer and a length (or two pointers), resulting in minimal memory overhead
- Copying `std::initializer_list` does not copy the underlying array; it only copies the proxy object itself, with no additional allocation overhead
- The underlying array may reside in read-only memory, making it suitable for initializing ROM-able static configuration tables

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| TBA | TBA | TBA |

## See Also

- [Tutorial: Initializer Lists](../../vol3-standard-library/01-initializer-lists.md)
- [cppreference: std::initializer_list](https://en.cppreference.com/w/cpp/utility/initializer_list)

---

*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
