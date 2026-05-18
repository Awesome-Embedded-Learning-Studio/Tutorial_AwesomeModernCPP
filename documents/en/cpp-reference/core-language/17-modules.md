---
title: "Modules"
description: "Header replacement compilation units: faster builds, better encapsulation, macro isolation"
chapter: 99
order: 17
tags:
  - host
  - cpp-modern
  - intermediate
difficulty: intermediate
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

# Modules (C++20)

## In a Nutshell

Replace header files with module interface files (`.cppm`) -- compile once and cache the result for dramatically faster recompilation, while isolating macro pollution and providing true symbol visibility control.

## Header

None (language feature, uses new file types and keywords)

## Core API Quick Reference

| Syntax | Description |
|--------|-------------|
| `module;` | Global module fragment start (place `#include` and other preprocessor directives here) |
| `export module mylib;` | Declares a module interface unit, exporting module name `mylib` |
| `export int func();` | Export declaration, visible to module consumers |
| `module mylib;` | Module implementation unit (not exported, implementation only) |
| `import mylib;` | Import a module (replaces `#include`) |
| `export import :sub;` | Re-export a sub-module |
| `module :private;` | Private module fragment (C++20), implementation details not part of the module interface |

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

- Build acceleration: module interfaces are compiled once and cached; large projects can see 30-70% recompilation time reduction
- Macro isolation: `#define` directives outside the module boundary do not leak inside, improving build stability
- Symbol visibility: `export` provides explicit API boundary control, replacing the "everything is public" model of headers
- Build system support is still maturing: CMake's native module support is gradually improving in 3.28+
- Cross-compiler compatibility issues exist (module BMI formats are not interchangeable), so cross-compiler builds require caution
- Embedded toolchains (especially cross-compilation scenarios) lag in module support; not recommended for core embedded projects in the short term

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 11 | 16 | 19.28 |

## See Also

- [cppreference: Modules](https://en.cppreference.com/w/cpp/language/modules)

---

*Some content adapted from [cppreference.com](https://en.cppreference.com/) under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) license*
