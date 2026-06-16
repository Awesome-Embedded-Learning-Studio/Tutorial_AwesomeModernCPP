---
chapter: 99
cpp_standard:
- 17
- 20
- 23
description: Lightweight, non-owning string view; zero-copy reference to a contiguous
  character sequence
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
  translated_at: '2026-06-16T03:28:12.125738+00:00'
  engine: anthropic
  token_count: 510
---
# std::string_view (C++17)

## In a Nutshell

A read-only string "view" that performs no copying or memory allocation. It holds only a pointer and a length, making it ideal for replacing `const std::string&` as a function parameter.

## Header

```cpp
#include <string_view>
```

## Core API Cheat Sheet

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Construction | `string_view(const CharT*, size_t)` | Constructs from a pointer and length |
| Construction | `string_view(const CharT*)` | Constructs from a C-style string |
| Length | `size()` | Returns the number of characters |
| Empty Check | `empty()` | Checks if the view is empty |
| Element Access | `operator[]` | Accesses character at the specified position |
| Data Pointer | `data()` | Returns the underlying character array pointer |
| Remove Prefix | `remove_prefix(size_t n)` | Moves the start position forward by n |
| Remove Suffix | `remove_suffix(size_t n)` | Moves the end position backward by n |
| Substring | `substr(pos, len)` | Returns a substring view |
| Find | `find(str)` | Finds the position of a substring |

## Minimal Example

```cpp
#include <string_view>
#include <iostream>

void print_sv(std::string_view sv) {
    std::cout << sv << std::endl;
}

int main() {
    // No copy, just a view
    std::string str = "Hello";
    std::string_view sv = str;

    print_sv("World"); // Implicit conversion from const char*
    print_sv(sv);      // Pass by view
}
```

## Embedded Applicability: High

- Zero heap allocation. It has only two members (pointer and length), resulting in minimal memory overhead (typically 16 bytes).
- A `TriviallyCopyable` type, making it safe for use in interrupt contexts or for parsing DMA transfer buffers.
- Replaces `const std::string&` to avoid implicit `std::string` construction and the associated heap allocation.
- **Caution**: Be mindful of lifetimes. Never bind a temporary `std::string` to a `std::string_view`.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 7.1 | 4.0   | 19.10 |

## See Also

- [cppreference: std::basic_string_view](https://en.cppreference.com/w/cpp/string/basic_string_view)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
