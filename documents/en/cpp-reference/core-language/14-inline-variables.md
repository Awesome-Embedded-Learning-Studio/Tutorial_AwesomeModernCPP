---
chapter: 99
cpp_standard:
- 17
- 20
- 23
description: Define global variables in a header without violating the ODR; the compiler
  guarantees a single instance.
difficulty: beginner
order: 14
reading_time_minutes: 2
tags:
- host
- cpp-modern
- beginner
title: inline variables
translation:
  source: documents/cpp-reference/core-language/14-inline-variables.md
  source_hash: 8505b0782a87f65c4971bb685b5330692137d6b2f0fff6a648ce1f2cf183b203
  translated_at: '2026-09-27T01:45:58+00:00'
  engine: anthropic
  token_count: 480
---
<!--
Reference Card Template
Used for feature cheat sheets under documents/cpp-reference/.
Unlike article-template.md, reference cards use a refined structured format and do not require a narrative style.

Tag usage rules:
1. Must include 1 platform tag (reference cards uniformly use host)
2. Must include 1 difficulty tag
3. Must include at least 1 topic tag
4. Select from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# inline variables (C++17)

## In a Nutshell

Apply `inline` to a namespace-scope variable, and you can define a global variable in a header file without triggering multiple-definition linker errors—the compiler guarantees exactly one instance across the entire program.

## Header

None (language feature)

## Core API Cheat Sheet

| Syntax | Description |
|------|------|
| `inline T var = val;` | Inline variable definition at namespace scope |
| `inline constexpr T var = val;` | `constexpr` variables are implicitly `inline`; no need to repeat the specifier |
| `inline static T var = val;` | In-class static data members; since C++17 they can be initialized directly inside the class |
| `inline thread_local T var = val;` | Combines with thread-local storage |

## Minimal Example

```cpp
// Standard: C++17
// header.h
#pragma once
#include <string>

inline const std::string kVersion = "1.0.0";
inline int kMaxRetries = 3;

// Multiple translation units include this header;
// the linker guarantees a single instance of kVersion and kMaxRetries
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

- An ideal companion for header-only libraries, replacing the `extern` global variable pattern.
- `constexpr` variables are implicitly `inline`, so the compile-time constant tables common in embedded code benefit naturally.
- Eliminates the boilerplate of "declare in a header + define in a source file".
- Zero runtime overhead; it only affects symbol merging during linking.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 7 | 3.9 | 19.1 |

## See Also

- [cppreference: inline specifier](https://en.cppreference.com/w/cpp/language/inline)

---

*Part of the content is referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
