---
chapter: 99
cpp_standard:
- 17
- 20
- 23
description: Defining global variables in a header file without violating the ODR
  (one definition rule), with the compiler guaranteeing a single instance
difficulty: beginner
order: 14
reading_time_minutes: 1
tags:
- host
- cpp-modern
- beginner
title: Inline Variable
translation:
  engine: anthropic
  source: documents/cpp-reference/core-language/14-inline-variables.md
  source_hash: 0ea7b67e0dde71306439802b1916ff0d7a5310c37f59ee4b2fda966cb6ca843c
  token_count: 422
  translated_at: '2026-05-26T10:16:18.167467+00:00'
---
<!--
Reference Card Template
Used for feature quick-reference pages under documents/cpp-reference/.
Unlike article-template.md, reference cards use a concise, structured format without a narrative style.

Tag usage rules:
1. Must include exactly 1 platform tag (reference cards uniformly use host)
2. Must include exactly 1 difficulty tag
3. Must include at least 1 topic tag
4. Selected from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# Inline Variables (C++17)

## In a Nutshell

Use `inline` to modify namespace-scope variables, allowing us to define global variables in headers without causing multiple definition linker errors—the compiler guarantees a single instance across the entire program.

## Header

None (language feature)

## Core API Quick Reference

| Syntax | Description |
|--------|-------------|
| `inline` | Inline variable definition at namespace scope |
| `inline constexpr` | `constexpr` variables are implicitly `inline`, no need for redundant annotations |
| `inline static` | In-class static member variables, directly initializable inside the class since C++17 |
| `inline thread_local` | Used with thread-local storage |

## Minimal Example

```cpp
// Standard: C++17
// header.h
#pragma once
#include <string>

inline const std::string kVersion = "1.0.0";
inline int kMaxRetries = 3;

// 多个翻译单元 include 此头文件，
// 链接时保证只有一个 kVersion 和 kMaxRetries 实例
```

```cpp
// main.cpp
#include <iostream>
#include "header.h"

int main() {
    std::cout << kVersion << "\n";     // 1.0.0
    std::cout << kMaxRetries << "\n";  // 3
}
```

## Embedded Applicability: High

- An ideal companion for header-only libraries, replacing the `extern` global variable pattern
- `constexpr` variables are implicitly `inline`, so compile-time constant tables commonly used in embedded systems naturally benefit
- Eliminates the boilerplate of "declare in header + define in source file"
- Zero runtime overhead, only affects symbol merging during the linking phase

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 7 | 3.9 | 19.1 |

## See Also

- [cppreference: inline specifier](https://en.cppreference.com/w/cpp/language/inline)

---

*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
