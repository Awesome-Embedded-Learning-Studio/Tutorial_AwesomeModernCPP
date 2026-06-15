---
chapter: 99
cpp_standard:
- 17
- 20
- 23
description: Replace multi-level nested namespace braces with the `A::B::C` syntax
difficulty: beginner
order: 15
reading_time_minutes: 1
tags:
- host
- cpp-modern
- beginner
title: Nested namespaces
translation:
  engine: anthropic
  source: documents/cpp-reference/core-language/15-nested-namespace.md
  source_hash: 3aed83f966e3c5c860686def8273b68c5b7f869cf9ce1c50370a45fff65dae07
  token_count: 417
  translated_at: '2026-05-26T10:16:40.516631+00:00'
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

# Nested Namespaces (C++17)

## In a Nutshell

Use `namespace A::B::C { ... }` on a single line to replace three levels of nested braces—pure syntactic sugar, but it drastically reduces indentation levels.

## Header

None (language feature)

## Core API Quick Reference

| Syntax | Equivalent |
|--------|------------|
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

- Pure syntactic sugar with no effect on generated code, but embedded projects typically do not use deep namespace hierarchies
- Helpful for code organization in large libraries and drivers, reducing indentation nesting
- Embedded code often uses flatter namespaces (such as `bsp::`, `hal::`), where a single level is sufficient
- Universally supported by C++17 compilers, with no compatibility concerns

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 7 | 3.9 | 19.1 |

## See Also

- [cppreference: Namespace](https://en.cppreference.com/w/cpp/language/namespace)

---

*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
