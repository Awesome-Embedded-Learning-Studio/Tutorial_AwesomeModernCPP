---
title: std::print
description: Type-safe formatted output to stdout, the new Hello World in C++
chapter: 99
order: 10
tags:
- host
- cpp-modern
- beginner
difficulty: beginner
cpp_standard:
- 23
translation:
  source: documents/cpp-reference/containers/10-print.md
  source_hash: 04881d0f3973a97b4c21bdf51ca60b6223459154cd0b1fb5c39920cd1fd5addb
  translated_at: '2026-05-26T10:14:41.686544+00:00'
  engine: anthropic
  token_count: 432
---
<!--
Reference Card Template
Used for feature cheat sheets under documents/cpp-reference/.
Unlike article-template.md, reference cards use a concise, structured format without a narrative style.

Tag usage rules:
1. Must include exactly 1 platform tag (reference cards uniformly use host)
2. Must include exactly 1 difficulty tag
3. Must include at least 1 topic tag
4. Selected from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# std::print (C++23)

## In a Nutshell

Directly outputs a formatted string to `stdout`—a combination of `std::format` + `std::cout`, and the new way to write Hello World in C++23.

## Header

`#include <print>`

## Core API Cheat Sheet

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Output to stdout | `void print(format_string, args...)` | Formats and outputs to standard output |
| Output with newline | `void println(format_string, args...)` | Automatically appends a newline character |
| Empty line | `void println()` | Outputs only a newline character |
| Output to file | `void print(FILE* f, format_string, args...)` | Outputs to a specified C file stream |
| Output to file with newline | `void println(FILE* f, format_string, args...)` | Newline version |
| Output to stream | `void vprint_unicode(std::ostream&, ...)` | Outputs to a C++ stream |

## Minimal Example

```cpp
// Standard: C++23
#include <print>

int main() {
    std::print("Hello, {}!\n", "world");
    std::println("value = {}", 42);
    std::println("{:>10.2f}", 3.14159); //       3.14
    std::println();                      // 空行
}
```

## Embedded Applicability: Low

- Depends on `stdout` and a file system abstraction layer; bare-metal environments typically lack standard output
- Suitable for embedded Linux host tools and test framework log output
- The formatting engine has a large Flash footprint; we do not recommend introducing it on extremely resource-constrained devices
- We can use `fmt::print` from the `{fmt}` library as a fallback option starting from C++11

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 14 | 18 | 19.34 |

## See Also

- [cppreference: std::print](https://en.cppreference.com/w/cpp/io/print)

---

*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
