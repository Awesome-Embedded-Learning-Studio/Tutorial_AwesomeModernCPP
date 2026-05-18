---
title: "Inline Variables"
description: "Define global variables in headers without violating ODR; the compiler guarantees a single instance"
chapter: 99
order: 14
tags:
  - host
  - cpp-modern
  - beginner
difficulty: beginner
cpp_standard: [17, 20, 23]
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

# Inline Variables (C++17)

## In a Nutshell

Use `inline` to qualify namespace-scope variables, allowing global variable definitions in headers without producing multiple-definition link errors -- the compiler guarantees a single instance across the entire program.

## Header

None (language feature)

## Core API Quick Reference

| Syntax | Description |
|--------|-------------|
| `inline T var = val;` | Inline variable definition at namespace scope |
| `inline constexpr T var = val;` | `constexpr` variables are implicitly `inline`; no need to repeat the qualifier |
| `inline static T var = val;` | Static member variable inside a class; can be initialized inline since C++17 |
| `inline thread_local T var = val;` | Combined with thread-local storage |

## Minimal Example

```cpp
// Standard: C++17
// header.h
#pragma once
#include <string>

inline const std::string kVersion = "1.0.0";
inline int kMaxRetries = 3;

// Multiple translation units include this header;
// the linker guarantees only one instance of kVersion and kMaxRetries
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

- Ideal companion for header-only libraries, replacing the extern global variable pattern
- `constexpr` variables are implicitly `inline`, so compile-time constant tables commonly used in embedded code benefit naturally
- Eliminates the boilerplate of "declare in header + define in source file"
- Zero runtime overhead; only affects symbol merging at link time

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 7 | 3.9 | 19.1 |

## See Also

- [cppreference: inline specifier](https://en.cppreference.com/w/cpp/language/inline)

---

*Some content adapted from [cppreference.com](https://en.cppreference.com/) under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) license*
