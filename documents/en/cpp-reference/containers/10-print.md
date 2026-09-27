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
  source_hash: e345f0e85aea655ccce9c41e918e9fc071bc95bed1c4727528b006aa05f142e7
  translated_at: '2026-09-27T02:03:41+00:00'
  engine: anthropic
  token_count: 420
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
| Output to stream | `void vprint_unicode(std::ostream&, ...)` | Writes to a C++ stream |

## Minimal Example

```cpp
// Standard: C++23
#include <print>

int main() {
    std::print("Hello, {}!\n", "world");
    std::println("value = {}", 42);
    std::println("{:>10.2f}", 3.14159); //       3.14
    std::println();                      // blank line
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
