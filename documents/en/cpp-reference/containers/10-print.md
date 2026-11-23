---
chapter: 99
cpp_standard:
- 23
description: Type-safe formatted output to stdout, the new Hello World of C++
difficulty: beginner
order: 10
reading_time_minutes: 2
tags:
- host
- cpp-modern
- beginner
title: std::print
translation:
  source: documents/cpp-reference/containers/10-print.md
  source_hash: a09b4bc9f0c019f2743d47a45f6d14264007f725bfa2d66295b12cdddef8e746
  translated_at: '2026-11-22T16:41:57.171115+00:00'
  engine: glm-5.3-refresh
  token_count: 1756
---
<!--
Reference Card Template
Used for feature cheat sheets under documents/cpp-reference/.
Unlike article-template.md, reference cards use a refined, structured format and do not require a narrative style.

Tag Usage Rules:
1. Must include 1 platform tag (reference cards uniformly use host)
2. Must include 1 difficulty tag
3. Must include at least 1 topic tag
4. Select from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# std::print (C++23)

## TL;DR

Writes formatted strings straight to `stdout` — `std::format` and `std::cout` rolled into one, the new way to write Hello World in C++23.

## Header

`#include <print>`

## Core API Cheat Sheet

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Output to stdout | `void print(format_string, args...)` | Formats and writes to standard output |
| Output with newline | `void println(format_string, args...)` | Automatically appends a newline |
| Blank line | `void println()` | Outputs just a newline character |
| Output to file | `void print(FILE* f, format_string, args...)` | Writes to a given C file stream |
| Output to file with newline | `void println(FILE* f, format_string, args...)` | Newline version |
| Output to stream | `void vprint_unicode(std::ostream&, std::format_args)` | Writes to a C++ stream |

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

- Depends on `stdout` and the filesystem abstraction layer; bare-metal environments usually have no standard output
- A good fit for log output in embedded Linux host-side tools and test frameworks
- The formatting engine carries significant Flash overhead; not recommended for extremely resource-constrained devices
- You can fall back to `fmt::print` from the `{fmt}` library, available since C++11

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 14 | 18 | 19.34 |

## See Also

- [cppreference: std::print](https://en.cppreference.com/w/cpp/io/print)

---

*Some content is adapted from [cppreference.com](https://en.cppreference.com/), used under the [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) license*
