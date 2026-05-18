---
title: "Nested Namespaces"
description: "Use A::B::C syntax to replace deeply nested namespace braces"
chapter: 99
order: 15
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

# Nested Namespaces (C++17)

## In a Nutshell

Replace three levels of nested braces with a single `namespace A::B::C { ... }` line -- pure syntactic sugar, but it dramatically reduces indentation depth.

## Header

None (language feature)

## Core API Quick Reference

| Syntax | Equivalent |
|--------|-----------|
| `namespace A::B { ... }` | `namespace A { namespace B { ... } }` |
| `namespace A::B::C { ... }` | `namespace A { namespace B { namespace C { ... } } }` |
| `namespace A::inline B { ... }` | `namespace A { inline namespace B { ... } }` (C++20) |

## Minimal Example

```cpp
// Standard: C++17
#include <iostream>

// Nested namespace definition
namespace hardware::spi {
    void init() { std::cout << "SPI init\n"; }
}

// Equivalent C++11 style (identical effect)
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

- Pure syntactic sugar that does not affect generated code; however, embedded projects typically do not have deep namespace hierarchies
- Helpful for organizing large libraries and drivers, reducing nesting indentation
- Embedded code often uses flat namespaces (e.g., `bsp::`, `hal::`) where a single level suffices
- Universally supported by C++17 compilers with no compatibility concerns

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 7 | 3.9 | 19.1 |

## See Also

- [cppreference: Namespace](https://en.cppreference.com/w/cpp/language/namespace)

---

*Some content adapted from [cppreference.com](https://en.cppreference.com/) under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) license*
