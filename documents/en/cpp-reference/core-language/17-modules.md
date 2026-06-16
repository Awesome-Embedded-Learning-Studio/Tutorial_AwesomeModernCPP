---
chapter: 99
cpp_standard:
- 20
- 23
description: 'Compilation unit mechanism replacing header files: faster compilation,
  better encapsulation, and macro isolation.'
difficulty: intermediate
order: 17
reading_time_minutes: 2
tags:
- host
- cpp-modern
- intermediate
title: Modules
translation:
  source: documents/cpp-reference/core-language/17-modules.md
  source_hash: 18bc407053e4b058d96f1d351fcbe30ebdac9f05adb927ac240805c36279752a
  translated_at: '2026-06-16T03:29:35.728912+00:00'
  engine: anthropic
  token_count: 444
---
<!--
Reference Card Template
Used for feature cheat sheets under documents/cpp-reference/.
Unlike article-template.md, reference cards use a structured format and do not require a narrative style.

Tag Usage Rules:
1. Must include 1 platform tag (use 'host' for reference cards)
2. Must include 1 difficulty tag
3. Must include at least 1 topic tag
4. Select from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# Modules (C++20)

## In a Nutshell

Replace header files with module interface units (`.cppm`)—compile once and cache the result to significantly speed up recompilation, while isolating macro pollution and providing true symbol visibility control.

## Headers

None (Language feature, uses new file types and keywords)

## Core API Cheat Sheet

| Syntax | Description |
|--------|-------------|
| `module;` | Start of the global module fragment (for preprocessor directives like `#include`) |
| `export module ModuleName;` | Declare a module interface unit, exporting the module name `ModuleName` |
| `export` | Export declaration, making it visible to module consumers |
| `module ModuleName;` | Module implementation unit (internal, does not export) |
| `import ModuleName;` | Import a module (replaces `#include`) |
| `export import SubModule;` | Re-export a submodule |
| `module :private;` | Private module fragment (C++20), implementation details not part of the module interface |

## Minimal Example

```cpp
// math_utils.cppm
export module math_utils;  // Declare module interface

namespace math {
    export constexpr int add(int a, int b) { // Exported function
        return a + b;
    }
}

// main.cpp
import math_utils;        // Import module
import std;               // Import standard library module (if supported)

int main() {
    return math::add(1, 2);
}
```

## Embedded Applicability: Medium

- **Compilation Speed:** Module interfaces are compiled once and cached, reducing recompilation time for large projects by 30-70%.
- **Macro Isolation:** `#define` macros outside the module boundary do not leak into the module, improving build stability.
- **Symbol Visibility:** `export` explicitly controls API boundaries, replacing the "everything is public" nature of headers.
- **Build System Support:** Native CMake support for modules is gradually maturing in version 3.28+.
- **Compatibility:** Compiler implementations vary (BMI formats are not universal), so cross-compiler builds require caution.
- **Embedded Toolchains:** Support for modules in embedded toolchains (especially cross-compilation scenarios) lags behind; short-term adoption in core embedded projects is not recommended.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 11  | 16    | 19.28 |

## See Also

- [cppreference: Modules](https://en.cppreference.com/w/cpp/language/modules)

---

*Parts of the content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
