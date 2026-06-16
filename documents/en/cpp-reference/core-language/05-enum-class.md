---
chapter: 99
cpp_standard:
- 11
- 14
- 17
- 20
- 23
description: Scoped enums, preventing enum values from polluting the external namespace
  and prohibiting implicit type conversions.
difficulty: beginner
order: 5
reading_time_minutes: 1
tags:
- host
- cpp-modern
- beginner
title: enum class
translation:
  source: documents/cpp-reference/core-language/05-enum-class.md
  source_hash: cb6c8b5560edf460b5c23246c67bca7a6ef5d0d364016c9e4a7910524d9efeb0
  translated_at: '2026-06-16T03:28:48.206005+00:00'
  engine: anthropic
  token_count: 398
---
# enum class (C++11)

## In a Nutshell

Scoped enumerations that resolve the issues of traditional `enum` types polluting the global namespace and implicitly converting to integers.

## Header

No header required (language keyword)

## Core API Cheat Sheet

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Declaration | `enum class Name { A, B };` | Basic scoped enum; underlying type defaults to `int` |
| Specify Underlying Type | `enum class Name : type { A, B };` | Fixed underlying type to save memory |
| Access Enumerators | `Name::A` | Must be accessed via scope operator |
| Cast to Integer | `static_cast<int>(Name::A)` | Explicit cast required; no implicit conversion |
| Opaque Declaration | `enum class Name : type;` | Forward declaration; underlying type must be specified |
| using enum | `using enum Name;` | (C++20) Injects enumerators into the current scope |

## Minimal Example

```cpp
enum class Color : uint8_t { Red, Green, Blue };

auto led = Color::Red;

// led = 0;                 // Error: no implicit conversion
if (led == Color::Green) {  // Type-safe comparison
    // ...
}

int value = static_cast<int>(led); // Explicit cast
```

## Embedded Applicability: High

- Specifying the underlying type (e.g., `uint8_t`, `uint32_t`) allows precise control over memory usage, which is ideal for protocol parsing and register mapping.
- Zero runtime overhead; fully resolved at compile time.
- Eliminates naming conflicts, making it suitable for modular development in large embedded projects.
- Explicit type conversion prevents accidental integer comparisons, enhancing code safety.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.7 | 3.1 | 2010 |

## See Also

- [cppreference: Enumeration declaration](https://en.cppreference.com/w/cpp/language/enum)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
