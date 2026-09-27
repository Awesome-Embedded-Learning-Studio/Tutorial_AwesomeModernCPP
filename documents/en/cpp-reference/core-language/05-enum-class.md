---
chapter: 99
cpp_standard:
- 11
- 14
- 17
- 20
- 23
description: Scoped enumerations that keep enumerator names out of the surrounding
  namespace and forbid implicit type conversions
difficulty: beginner
order: 5
reading_time_minutes: 2
tags:
- host
- cpp-modern
- beginner
title: enum class
translation:
  source: documents/cpp-reference/core-language/05-enum-class.md
  source_hash: cb6c8b5560edf460b5c23246c67bca7a6ef5d0d364016c9e4a7910524d9efeb0
  translated_at: '2026-09-27T01:31:22+00:00'
  engine: anthropic
  token_count: 1150
---
# enum class (C++11)

## In a Nutshell

A scoped enumeration type that fixes the two classic problems of plain `enum`: enumerator names polluting the global namespace, and implicit conversion to integers.

## Header

No header required (language keyword)

## Core API Cheat Sheet

| Operation | Signature | Description |
|------|------|------|
| Declaration | `enum class Name { A, B, C };` | Basic scoped enum; the underlying type defaults to `int` |
| Specify the underlying type | `enum class Name : uint8_t { A, B };` | Pins down the underlying type, saving memory |
| Access an enumerator | `Name::A` | Must be accessed through the scope operator |
| Convert to an integer | `static_cast<int>(Name::A)` | Explicit cast required; no implicit conversion |
| Opaque declaration | `enum class Name : uint8_t;` | Forward declaration; the underlying type must be specified |
| using enum | `using enum Name;` | (C++20) Brings the enumerators into the current scope |

## Minimal Example

```cpp
// Standard: C++11
#include <iostream>

int main() {
    enum class Color : uint8_t { red, green = 20, blue };
    Color r = Color::blue;

    switch (r) {
        case Color::red:   std::cout << "red\n";   break;
        case Color::green: std::cout << "green\n"; break;
        case Color::blue:  std::cout << "blue\n";  break;
    }

    // int n = r; // error
    int n = static_cast<int>(r);
    std::cout << n << '\n'; // 21
}
```

## Embedded Applicability: High

- Specifying the underlying type (e.g., `uint8_t`, `uint32_t`) gives you exact control over memory footprint, a good fit for protocol parsing and register mapping
- Zero runtime overhead; everything is fully resolved at compile time
- Eliminates naming conflicts, which suits modular development in large embedded projects
- Requiring an explicit cast rules out accidental integer comparisons, improving code safety

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.7 | 3.1 | 2010 |

## See Also

- [cppreference: Enumeration declaration](https://en.cppreference.com/w/cpp/language/enum)

---

*Part of the content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
