---
chapter: 99
cpp_standard:
- 20
- 23
description: 'Compilation unit mechanism replacing header files: faster compilation,
  better encapsulation, macro isolation'
difficulty: intermediate
order: 17
reading_time_minutes: 2
tags:
- host
- cpp-modern
- intermediate
title: Modules
translation:
  engine: anthropic
  source: documents/cpp-reference/core-language/17-modules.md
  source_hash: 389a256e6dfe9638a25e1d299f0a1715123c4601ede35b955c34e57741a30a2a
  token_count: 441
  translated_at: '2026-05-26T10:16:53.858858+00:00'
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

# Modules (C++20)

## Summary

Replace header files with module interface units (`.cppm`)—results are cached after a single compilation, drastically speeding up recompilation, while isolating macro pollution and providing true symbol visibility control.

## Headers

None (language feature, uses new file types and keywords)

## Core API Quick Reference

| Syntax | Description |
|--------|-------------|
| `module;` | Global module fragment start (place preprocessor directives like `#include`) |
| `export module mylib;` | Declares a module interface unit, exporting module name `mylib` |
| `export int func();` | Export declaration, visible to module consumers |
| `module mylib;` | Module implementation unit (not exported, implementation only) |
| `import mylib;` | Import module (replaces `#include`) |
| `export import :sub;` | Re-export submodule |
| `module :private;` | Private module fragment (C++20), implementation details do not participate in the module interface |

## Minimal Example

```cpp
// Standard: C++20
// --- math.cppm (模块接口) ---
export module math;

export int add(int a, int b) {
    return a + b;
}

// --- main.cpp (使用者) ---
import math;
#include <iostream>

int main() {
    std::cout << add(2, 3) << "\n"; // 5
}
```

## Embedded Applicability: Medium

- Compilation speedup: module interfaces are cached after a single compilation, reducing recompilation time by 30-70% for large projects
- Macro isolation: `#define` outside module boundaries do not leak into the module, improving build stability
- Symbol visibility: `export` explicitly controls API boundaries, replacing the header file "everything is public" model
- Build system support is still incomplete: CMake's native support for modules is gradually maturing in 3.28+
- Compatibility issues exist across compiler implementations (module BMI formats are not universal), cross-compiler builds require caution
- Embedded toolchains (especially in cross-compilation scenarios) lag in modules support; we do not recommend adopting modules in the core of embedded projects in the short term

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 11 | 16 | 19.28 |

## See Also

- [cppreference: Modules](https://en.cppreference.com/w/cpp/language/modules)

---

*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
