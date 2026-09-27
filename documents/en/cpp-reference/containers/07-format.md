---
chapter: 99
cpp_standard:
- 20
- 23
description: A type-safe, extensible formatting output library that replaces printf
  and stringstream
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
  translated_at: '2026-09-27T02:04:22+00:00'
  engine: anthropic
  token_count: 600
---
<!--
Reference Card Template
Used for feature cheat sheet pages under documents/cpp-reference/.
Unlike article-template.md, reference cards use a concise, structured format and
do not require a narrative style.

Tag usage rules:
1. Must include 1 platform tag (reference cards uniformly use host)
2. Must include 1 difficulty tag
3. Must include at least 1 topic tag
4. Select from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# std::format (C++20)

## In a nutshell

A type-safe `printf` alternative—format strings with `{}` placeholders, compile-time
checking of the argument count, and support for formatting custom types.

## Header file

`#include <format>`

## Core API Cheat Sheet

| Operation | Signature | Description |
|------|------|------|
| Format a string | `string format(fmt, args...)` | Returns the formatted string |
| Format to output | `void vformat_to(out_it, fmt, args)` | Outputs to an iterator |
| Format to buffer | `size_t formatted_size(fmt, args...)` | Precomputes the output length |
| Format to stdout | (C++23) `void print(fmt, args...)` | Writes directly to standard output |
| Positional arguments | `"{0} {1} {0}"` | References arguments by index |
| Width/precision | `"{:>10.2f}"` | Right-aligned, width 10, precision 2 |
| Custom formatting | `template<> struct formatter<T>` | Specialize `std::formatter` to support custom types |

## Minimal Example

```cpp
// Standard: C++20
#include <format>
#include <iostream>
#include <string>

int main() {
    std::string s = std::format("Hello, {}!", "world");
    std::cout << s << "\n"; // Hello, world!

    int version = 2;
    double pi = 3.14159265;
    std::cout << std::format("v{}. pi={:.2f}", version, pi) << "\n";
    // v2. pi=3.14

    // Positional arguments
    std::cout << std::format("{0} + {0} = {1}", 3, 6) << "\n";
    // 3 + 3 = 6
}
```

## Embedded Suitability: Medium

- Replaces `printf`, eliminating the risk of runtime crashes from a format string
  that does not match the argument types
- Replaces `std::stringstream`, avoiding heap allocation overhead
- Checks the argument count at compile time, but full compile-time validation of
  format specifiers requires the help of C++23's `std::is_constant_evaluated`
- Flash overhead can be significant (the code size of the formatting engine);
  evaluate before use on extremely resource-constrained devices
- The [{fmt}](https://github.com/fmtlib/fmt) library can serve as a fallback
  option from C++11 onward

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 13 | 17 | 19.29 |

## See Also

- [cppreference: std::format](https://en.cppreference.com/w/cpp/utility/format)

---

*Part of the content is referenced from [cppreference.com](https://en.cppreference.com/) and is licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
