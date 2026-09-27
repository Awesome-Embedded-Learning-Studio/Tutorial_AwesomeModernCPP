---
chapter: 99
cpp_standard:
- 17
- 20
- 23
description: A lightweight, non-owning string view providing zero-copy access to
  a contiguous character sequence
difficulty: beginner
order: 2
reading_time_minutes: 2
tags:
- host
- cpp-modern
- beginner
title: std::string_view
translation:
  source: documents/cpp-reference/containers/02-string-view.md
  source_hash: fd07cdf9d67313c2c7dce8ae2d1811a69e169d645980adcd2ffaa6475dfe6eaf
  translated_at: '2026-09-26T17:12:41+00:00'
  engine: anthropic
  token_count: 520
---
# std::string_view (C++17)

## In a nutshell

A read-only "view" of a string that copies nothing and allocates no memory — it holds just a pointer and a length, making it a good replacement for `const std::string&` as a function parameter.

## Header file

`#include <string_view>`

## Core API Cheat Sheet

| Operation | Signature | Description |
|------|------|------|
| Construction | `constexpr basic_string_view(const CharT* s, size_type count)` | Constructs from a pointer and a length |
| Construction | `constexpr basic_string_view(const CharT* s)` | Constructs from a C string |
| Length | `constexpr size_type size() const` | Returns the number of characters |
| Empty check | `constexpr bool empty() const` | Checks whether the view is empty |
| Element access | `constexpr const CharT& operator[](size_type pos) const` | Accesses the character at the given position |
| Data pointer | `constexpr const CharT* data() const` | Returns a pointer to the underlying character array |
| Remove prefix | `constexpr void remove_prefix(size_type n)` | Advances the start position by n |
| Remove suffix | `constexpr void remove_suffix(size_type n)` | Pulls the end position back by n |
| Substring | `constexpr basic_string_view substr(size_type pos = 0, size_type count = npos) const` | Returns a substring view |
| Find | `constexpr size_type find(basic_string_view v, size_type pos = 0) const` | Finds the position of a substring |

## Minimal Example

```cpp
#include <iostream>
#include <string_view>
// Standard: C++17

void print(std::string_view sv) {
    std::cout << sv << "\n";
}

int main() {
    std::string s = "hello";
    print(s);                    // accepts a std::string
    print("world");              // accepts a string literal
    std::string_view sv = s;
    sv.remove_prefix(1);         // now "ello"
    print(sv.substr(0, 2));      // outputs "el"
}
```

## Embedded Suitability: High

- Zero heap allocation — just two members, a pointer and a length, so the memory overhead is tiny (typically 16 bytes)
- A TriviallyCopyable type, safe to use in interrupt contexts or when parsing DMA transfer buffers
- Replaces `const std::string&` to avoid the heap allocation caused by an implicit `std::string` construction
- Watch the lifetime: never bind a temporary `std::string` to a `string_view`

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 7.1 | 4.0   | 19.10 |

## See Also

- [cppreference: std::basic_string_view](https://en.cppreference.com/w/cpp/string/basic_string_view)

---

*Part of the content is referenced from [cppreference.com](https://en.cppreference.com/) and is licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
