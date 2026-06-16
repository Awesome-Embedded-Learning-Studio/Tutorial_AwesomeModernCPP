---
chapter: 99
cpp_standard:
- 23
description: Type-safe formatted output to stdout, the new Hello World in C++
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
  translated_at: '2026-06-16T03:28:36.894310+00:00'
  engine: anthropic
  token_count: 435
---
<!--
Reference Card Template
Used for feature cheat sheets under documents/cpp-reference/.
Unlike article-template.md, reference cards use a refined, structured format and do not require a narrative style.

Tag Usage Rules:
1. Must include 1 platform tag (use 'host' for reference cards)
2. Must include 1 difficulty tag
3. Must include at least 1 topic tag
4. Select from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# std::print (C++23)

## TL;DR

Output formatted strings directly to `stdout`—a combination of `printf` + `iostream` + type safety, the new way to write Hello World in C++23.

## Header

```cpp
#include <print>
```

## Core API Cheat Sheet

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Output to stdout | `std::print(fmt, args...);` | Format and output to standard output |
| Output with newline | `std::println(fmt, args...);` | Automatically append a newline character |
| Empty line | `std::println();` | Output only a newline character |
| Output to file | `std::print(file, fmt, args...);` | Output to a specified C file stream |
| Output to file with newline | `std::println(file, fmt, args...);` | Newline version |
| Output to stream | `std::print(stream, fmt, args...);` | Output to a C++ stream |

## Minimal Example

```cpp
#include <print>

int main() {
    // Basic replacement
    std::print("Hello, {}!\n", "World");

    // Automatic newline
    std::println("The answer is {}", 42);

    // User-defined types (if formatter is specialized)
    // std::println("Point: {}", Point{10, 20});
}
```

## Embedded Applicability: Low

- Relies on the OS and filesystem abstraction layer; bare-metal environments typically lack standard output.
- Suitable for logging in embedded Linux host tools or test frameworks.
- The formatting engine incurs significant Flash overhead; it is not recommended for resource-constrained devices.
- Use `fmt` library's `fmt::print` as a fallback option starting from C++11.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 14 | 18 | 19.34 |

## See Also

- [cppreference: std::print](https://en.cppreference.com/w/cpp/io/print)

---

*Part of the content references [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
