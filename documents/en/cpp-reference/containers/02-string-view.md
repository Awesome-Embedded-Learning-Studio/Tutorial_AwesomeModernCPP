---
title: std::string_view
description: Lightweight, non-owning string view, a zero-copy reference to a contiguous
  sequence of characters
chapter: 99
order: 2
tags:
- host
- cpp-modern
- beginner
difficulty: beginner
cpp_standard:
- 17
- 20
- 23
translation:
  source: documents/cpp-reference/containers/02-string-view.md
  source_hash: c02f01a41e1a3a72a09dda5e846b5ed0675c6ad5b1343cda95eafa94ef82c389
  translated_at: '2026-05-26T10:13:55.202443+00:00'
  engine: anthropic
  token_count: 507
---
# std::string_view (C++17)

## In a Nutshell

A read-only string "view" that performs no copying or memory allocation. It only holds a pointer and a length, making it ideal for replacing `const std::string&` as a function parameter.

## Header

`#include <string_view>`

## Core API Quick Reference

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Constructor | `constexpr basic_string_view(const CharT* s, size_type count)` | Constructs from a pointer and length |
| Constructor | `constexpr basic_string_view(const CharT* s)` | Constructs from a C-string |
| Length | `constexpr size_type size() const` | Returns the number of characters |
| Empty check | `constexpr bool empty() const` | Checks if the view is empty |
| Element access | `constexpr const CharT& operator[](size_type pos) const` | Accesses the character at the specified position |
| Data pointer | `constexpr const CharT* data() const` | Returns a pointer to the underlying character array |
| Remove prefix | `constexpr void remove_prefix(size_type n)` | Advances the starting position by n |
| Remove suffix | `constexpr void remove_suffix(size_type n)` | Moves the end position back by n |
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
    print(s);                    // 接受 std::string
    print("world");              // 接受字符串字面量
    std::string_view sv = s;
    sv.remove_prefix(1);         // 变为 "ello"
    print(sv.substr(0, 2));      // 输出 "el"
}
```

## Embedded Applicability: High

- Zero heap allocation; it only has two members, a pointer and a length, resulting in minimal memory overhead (typically 16 bytes)
- A TriviallyCopyable type, safe to use in interrupt contexts or for parsing DMA transfer buffers
- Replaces `const std::string&` to avoid heap allocations caused by implicit `std::string` construction
- Note on lifecycles: never bind a temporary `std::string` to a `string_view`

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 7.1 | 4.0   | 19.10 |

## See Also

- [cppreference: std::basic_string_view](https://en.cppreference.com/w/cpp/string/basic_string_view)

---

*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
