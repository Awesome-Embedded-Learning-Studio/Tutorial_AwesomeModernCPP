---
title: "std::print"
description: "Type-safe formatted output to stdout, the new C++ Hello World"
chapter: 99
order: 10
tags:
  - host
  - cpp-modern
  - beginner
difficulty: beginner
cpp_standard: [23]
---

<!--
Reference Card Template
Used for feature quick-reference pages under documents/cpp-reference/.
Unlike article-template.md, reference cards use a concise, structured format and do not require a narrative style.

Tag usage rules:
1. Must include exactly 1 platform tag (reference cards uniformly use host)
2. Must include exactly 1 difficulty tag
3. Must include at least 1 topic tag
4. Selected from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# std::print (C++23)

## In a Nutshell

Directly outputs formatted strings to `stdout` -- the fusion of `std::format` and `std::cout`, the new C++23 way to write Hello World.

## Header

`#include <print>`

## Core API Quick Reference

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Output to stdout | `void print(format_string, args...)` | Format and output to standard output |
| Output with newline | `void println(format_string, args...)` | Automatically appends a newline |
| Blank line | `void println()` | Outputs only a newline character |
| Output to file | `void print(FILE* f, format_string, args...)` | Output to a specified C file stream |
| Output to file with newline | `void println(FILE* f, format_string, args...)` | Newline version |
| Output to stream | `void vprint_unicode(std::ostream&, ...)` | Output to a C++ stream |

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

- Depends on `stdout` and filesystem abstraction layers; bare-metal environments typically have no standard output
- Suitable for embedded Linux host-side tools and test framework log output
- The formatting engine has significant Flash overhead; not recommended for extremely resource-constrained devices
- The `{fmt}` library's `fmt::print` can serve as a fallback for C++11 and later

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 14 | 18 | 19.34 |

## See Also

- [cppreference: std::print](https://en.cppreference.com/w/cpp/io/print)

---

*Some content adapted from [cppreference.com](https://en.cppreference.com/) under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) license*
