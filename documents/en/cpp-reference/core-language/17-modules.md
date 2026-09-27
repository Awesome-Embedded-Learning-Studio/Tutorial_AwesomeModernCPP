---
chapter: 99
cpp_standard:
- 20
- 23
description: 'A compilation-unit mechanism replacing header files: faster compilation,
  better encapsulation, and macro isolation'
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
  translated_at: '2026-09-27T01:49:26+00:00'
  engine: anthropic
  token_count: 600
---
<!--
Reference Card Template
Used for feature cheat-sheet pages under documents/cpp-reference/.
Unlike article-template.md, reference cards use a refined, structured format and do not require a narrative style.

Tag usage rules:
1. Must include 1 platform tag (reference cards uniformly use host)
2. Must include 1 difficulty tag
3. Include at least 1 topic tag
4. Select from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# Modules (C++20)

## In a Nutshell

Replace header files with module interface files (`.cppm`)—compile once, cache the result, and dramatically speed up recompilation, while isolating macro pollution and providing genuine control over symbol visibility.

## Header

None (a language feature that uses new file types and keywords)

## Core API Cheat Sheet

| Syntax | Description |
|--------|-------------|
| `module;` | Start of the global module fragment (holds preprocessor directives such as `#include`) |
| `export module mylib;` | Declares a module interface unit, exporting the module name `mylib` |
| `export int func();` | Exported declaration, visible to users of the module |
| `module mylib;` | Module implementation unit (not exported, implementation only) |
| `import mylib;` | Imports a module (replaces `#include`) |
| `export import :sub;` | Re-exports a submodule |
| `module :private;` | Private module fragment (C++20); implementation details are not part of the module interface |

## Minimal Example

```cpp
// Standard: C++20
// --- math.cppm (module interface) ---
export module math;

export int add(int a, int b) {
    return a + b;
}

// --- main.cpp (consumer) ---
import math;
#include <iostream>

int main() {
    std::cout << add(2, 3) << "\n"; // 5
}
```

## Embedded Applicability: Medium

- Faster builds: module interfaces are compiled once and cached, cutting recompilation time on large projects by 30-70%
- Macro isolation: `#define` outside the module boundary cannot leak into the module, improving build stability
- Symbol visibility: `export` explicitly controls the API boundary, replacing the "everything is public" model of header files
- Build system support is still immature: CMake's native support for modules has been gradually maturing in 3.28+
- Compiler implementations have compatibility issues (module BMI formats are not interoperable), so cross-compiler builds require caution
- Embedded toolchains (especially cross-compilation scenarios) lag behind in module support; short-term adoption in the core of embedded projects is not recommended

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 11 | 16 | 19.28 |

## See Also

- [cppreference: Modules](https://en.cppreference.com/w/cpp/language/modules)

---

*Some content is referenced from [cppreference.com](https://en.cppreference.com/), used under the [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) license*
