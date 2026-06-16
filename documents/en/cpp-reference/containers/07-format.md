---
chapter: 99
cpp_standard:
- 20
- 23
description: Type-safe, extensible formatting output library, replacing `printf` and
  `stringstream`
difficulty: beginner
order: 7
reading_time_minutes: 2
tags:
- host
- cpp-modern
- beginner
title: std::format
translation:
  source: documents/cpp-reference/containers/07-format.md
  source_hash: 916efc6d4b78cbc845fc224afa6164b76c9afeed2adca2c0eb1107c97a68787c
  translated_at: '2026-06-16T03:28:29.405332+00:00'
  engine: anthropic
  token_count: 512
---
<!--
Reference Card Template
Used for feature cheat sheets under documents/cpp-reference/.
Unlike article-template.md, reference cards use a concise, structured format, not a narrative style.

Tag usage rules:
1. Must include 1 platform tag (use 'host' for reference cards)
2. Must include 1 difficulty tag
3. Must include at least 1 topic tag
4. Select from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# std::format (C++20)

## TL;DR

A type-safe `printf` alternative—format strings using `{}` placeholders, checks argument count at compile time, and supports custom type formatting.

## Header

`<format>`

## Core API Cheat Sheet

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Format string | `std::format(fmt, args...)` | Returns formatted string |
| Format to output | `std::format_to(out, fmt, args...)` | Outputs to iterator |
| Format to buffer | `std::formatted_size(fmt, args...)` | Pre-calculates output length |
| Format to stdout | (C++23) `std::print(fmt, args...)` | Outputs directly to standard output |
| Positional args | `std::format("{0} {1}", a, b)` | References arguments by index |
| Width/precision | `std::format("{:>10.2}", v)` | Right-aligned, width 10, precision 2 |
| Custom formatting | `template<> struct formatter<T>` | Specialize `formatter` to support custom types |

## Minimal Example

```cpp
#include <format>
#include <iostream>
#include <string>

int main() {
    // Basic replacement
    std::string s = std::format("The answer is {}.", 42);
    // s == "The answer is 42."

    // Alignment and width
    int x = 42;
    std::cout << std::format("{:>10}", x) << '\n'; // "        42"

    // Type-specific formatting (hex)
    std::cout << std::format("{:#x}", 255) << '\n'; // "0xff"
}
```

## Embedded Applicability: Medium

- Replaces `printf`, eliminating runtime crash risks from mismatched format strings and argument types.
- Replaces `std::stringstream`, avoiding heap allocation overhead.
- Checks argument count at compile time, but full compile-time validation of format specifiers requires `std::format` in C++23.
- Flash overhead may be significant (formatting engine code size); evaluate for resource-constrained devices.
- The [{fmt}](https://github.com/fmtlib/fmt) library can be used as a backport for C++11 and later.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 13 | 17 | 19.29 |

## See Also

- [cppreference: std::format](https://en.cppreference.com/w/cpp/utility/format)

---

*Part of the content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
