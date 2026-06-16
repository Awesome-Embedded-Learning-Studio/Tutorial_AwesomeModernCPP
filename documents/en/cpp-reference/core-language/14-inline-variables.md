---
chapter: 99
cpp_standard:
- 17
- 20
- 23
description: Define global variables in header files without violating the one definition
  rule (ODR); the compiler guarantees a single instance.
difficulty: beginner
order: 14
reading_time_minutes: 2
tags:
- host
- cpp-modern
- beginner
title: inline variable
translation:
  source: documents/cpp-reference/core-language/14-inline-variables.md
  source_hash: 8505b0782a87f65c4971bb685b5330692137d6b2f0fff6a648ce1f2cf183b203
  translated_at: '2026-06-16T04:37:32.538757+00:00'
  engine: anthropic
  token_count: 426
---
<!--
Reference Card Template
Used for feature cheat sheets under documents/cpp-reference/.
Unlike article-template.md, reference cards use a refined structured format and do not require a narrative style.

Tag usage rules:
1. Must include 1 platform tag (use 'host' for reference cards)
2. Must include 1 difficulty tag
3. Must include at least 1 topic tag
4. Select from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# Inline Variables (C++17)

## In a Nutshell

Use `inline` to modify namespace-scope variables, allowing global variable definitions in header files without causing multiple-definition linker errors—the compiler guarantees a single instance across the program.

## Header

None (language feature)

## Core API Cheat Sheet

| Syntax | Description |
|------|------|
| `inline Type var = value;` | Inline variable definition at namespace scope |
| `const inline` | `const` variables are implicitly `inline`, no need to repeat the specifier |
| `static inline Type var = value;` | In-class static member variables; C++17 allows in-class initialization |
| `thread_local inline` | Used with thread-local storage |

## Minimal Example

```cpp
// config.h
#pragma once
#include <cstdint>

// Define a global configuration in the header
// No need for a separate config.cpp file
inline std::uint32_t system_tick_rate_hz = 1000;

// const variables are implicitly inline
inline constexpr std::size_t buffer_size = 512;

// Class static members can be initialized in-class
class SystemState {
public:
    static inline bool is_initialized = false;
};
```

```cpp
// main.cpp
#include "config.h"
#include <iostream>

int main() {
    // Access the inline variable
    std::cout << "Tick Rate: " << system_tick_rate_hz << std::endl;
    system_tick_rate_hz = 2000; // Modifies the single shared instance
}
```

## Embedded Applicability: High

- An ideal partner for header-only libraries, replacing the `extern` global variable pattern.
- `const` variables are implicitly `inline`, so compile-time constant tables commonly used in embedded systems benefit naturally.
- Eliminates boilerplate code for "declare in header + define in source file".
- Zero runtime overhead; only affects symbol merging during the linking phase.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 7 | 3.9 | 19.1 |

## See Also

- [cppreference: inline specifier](https://en.cppreference.com/w/cpp/language/inline)

---

*Part of the content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
