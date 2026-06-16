---
chapter: 99
cpp_standard:
- 17
- 20
- 23
description: Use `A::B::C` syntax instead of nested namespace braces
difficulty: beginner
order: 15
reading_time_minutes: 1
tags:
- host
- cpp-modern
- beginner
title: Nested Namespaces
translation:
  source: documents/cpp-reference/core-language/15-nested-namespace.md
  source_hash: 3a94860212341828616537940d079fd7b81f0fff3acda7dbf780db22f24277cc
  translated_at: '2026-06-16T03:29:27.263714+00:00'
  engine: anthropic
  token_count: 421
---
<!--
Reference Card Template
For feature cheat sheets under documents/cpp-reference/.
Unlike article-template.md, reference cards use a concise, structured format and do not require a narrative style.

Tag usage rules:
1. Must include 1 platform tag (use 'host' for reference cards)
2. Must include 1 difficulty tag
3. Must include at least 1 topic tag
4. Select from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# Nested Namespaces (C++17)

## The Gist

Use `namespace A::B::C` to replace three layers of nested braces—pure syntactic sugar that drastically reduces indentation levels.

## Header

None (language feature)

## Core API Cheat Sheet

| Syntax | Equivalent |
|------|---------|
| `namespace A::B::C { ... }` | `namespace A { namespace B { namespace C { ... } } }` |
| `namespace A::B::C::D { ... }` | `namespace A { namespace B { namespace C { namespace D { ... } } } }` |
| `namespace A::B { inline namespace C { ... } }` | `namespace A { namespace B { inline namespace C { ... } } }` (C++20) |

## Minimal Example

```cpp
// C++17 style: concise and flat
namespace App::Hardware::Driver {
    void init() {
        // Initialization logic
    }
}

// Traditional C++ style: verbose and deeply indented
namespace App {
    namespace Hardware {
        namespace Driver {
            void init() {
                // Initialization logic
            }
        }
    }
}
```

## Embedded Applicability: Low

- Pure syntactic sugar; it does not affect generated code, but embedded projects typically do not have deep namespace hierarchies.
- Helpful for organizing code in large libraries and drivers by reducing indentation nesting.
- Embedded code often uses flatter namespaces (e.g., `HAL`, `Driver`), where a single level is sufficient.
- Universally supported by C++17 compilers with no compatibility concerns.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 7 | 3.9 | 19.1 |

## See Also

- [cppreference: Namespace](https://en.cppreference.com/w/cpp/language/namespace)

---

*Part of the content references [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
