---
chapter: 99
cpp_standard:
- 17
- 20
- 23
description: Replace multi-level nested namespace braces with the `A::B::C` syntax
difficulty: beginner
order: 15
reading_time_minutes: 2
tags:
- host
- cpp-modern
- beginner
title: Nested Namespaces
translation:
  source: documents/cpp-reference/core-language/15-nested-namespace.md
  source_hash: 3a94860212341828616537940d079fd7b81f0fff3acda7dbf780db22f24277cc
  translated_at: '2026-11-23T01:12:08+00:00'
  engine: glm-5.3-fence-fix
  token_count: 1686
---
<!--
Reference Card Template
Used for feature cheat sheets under documents/cpp-reference/.
Unlike article-template.md, reference cards use a concise, structured format and do not require a narrative style.

Tag usage rules:
1. Must include 1 platform tag (use 'host' for reference cards)
2. Must include 1 difficulty tag
3. Must include at least 1 topic tag
4. Select from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# Nested Namespaces (C++17)

## In a Nutshell

Use the single line `namespace A::B::C { ... }` in place of three layers of nested braces—pure syntactic sugar, yet it greatly reduces indentation levels.

## Header

None (language feature)

## Core API Cheat Sheet

| Syntax | Equivalent Form |
|------|---------|
| `namespace A::B { ... }` | `namespace A { namespace B { ... } }` |
| `namespace A::B::C { ... }` | `namespace A { namespace B { namespace C { ... } } }` |
| `namespace A::inline B { ... }` | `namespace A { inline namespace B { ... } }` (C++20) |

## Minimal Example

```cpp
// Standard: C++17
#include <iostream>

// 嵌套命名空间定义
namespace hardware::spi {
    void init() { std::cout << "SPI init\n"; }
}

// 等价的 C++11 写法（效果完全相同）
namespace hardware {
    namespace i2c {
        void init() { std::cout << "I2C init\n"; }
    }
}

int main() {
    hardware::spi::init(); // SPI init
    hardware::i2c::init(); // I2C init
}
```

## Embedded Applicability: Low

- Pure syntactic sugar that does not affect the generated code, and embedded projects usually do not have deep namespace hierarchies.
- Helpful for organizing code in large libraries and drivers, reducing indentation nesting.
- Embedded code often uses flatter namespaces (e.g., `bsp::`, `hal::`), where a single level is enough.
- Universally supported by C++17 compilers, with no compatibility concerns.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 7 | 3.9 | 19.1 |

## See Also

- [cppreference: Namespace](https://en.cppreference.com/w/cpp/language/namespace)

---

*Part of the content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
