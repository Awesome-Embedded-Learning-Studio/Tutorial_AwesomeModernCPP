---
title: "std::format"
description: "Type-safe, extensible formatting library replacing printf and stringstream"
chapter: 99
order: 7
tags:
  - host
  - cpp-modern
  - beginner
difficulty: beginner
cpp_standard: [20, 23]
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

# std::format (C++20)

## In a Nutshell

A type-safe replacement for `printf` -- format strings with `{}` placeholders, compile-time argument count checking, and support for custom type formatting.

## Header

`#include <format>`

## Core API Quick Reference

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Format string | `string format(fmt, args...)` | Returns the formatted string |
| Format to output | `void vformat_to(out_it, fmt, args)` | Output to an iterator |
| Format to buffer | `size_t formatted_size(fmt, args...)` | Pre-compute output length |
| Format to stdout | (C++23) `void print(fmt, args...)` | Direct output to standard output |
| Positional arguments | `"{0} {1} {0}"` | Reference arguments by index |
| Width/precision | `"{:>10.2f}"` | Right-align, width 10, precision 2 |
| Custom formatting | `template<> struct formatter` `<T>` | Specialize `std::formatter` to support custom types |

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

## Embedded Applicability: Medium

- Replaces `printf`, eliminating the risk of runtime crashes from format-string/argument type mismatches
- Replaces `std::stringstream`, avoiding heap allocation overhead
- Compile-time argument count checking; full compile-time format specifier validation requires C++23's `std::is_constant_evaluated`
- Flash overhead may be significant (formatting engine code size); evaluate for extremely resource-constrained devices
- The [{fmt}](https://github.com/fmtlib/fmt) library can serve as a C++11-compatible fallback

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 13 | 17 | 19.29 |

## See Also

- [cppreference: std::format](https://en.cppreference.com/w/cpp/utility/format)

---

*Some content adapted from [cppreference.com](https://en.cppreference.com/) under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) license*
