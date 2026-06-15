---
chapter: 99
cpp_standard:
- 11
- 14
- 17
- 20
- 23
description: Scoped enumerations prevent enumeration values from polluting the outer
  namespace and prohibit implicit type conversions.
difficulty: beginner
order: 5
reading_time_minutes: 1
tags:
- host
- cpp-modern
- beginner
title: enum class
translation:
  engine: anthropic
  source: documents/cpp-reference/core-language/05-enum-class.md
  source_hash: fc1119531ee51121638ceba0fcafcd0029e2186344f648dcdbd1cb70e7cdd12e
  token_count: 394
  translated_at: '2026-05-26T10:15:36.831894+00:00'
---
# enum class (C++11)

## In a Nutshell

A scoped enumeration type that solves the problems of traditional `enum` polluting the global namespace and implicitly converting to integers.

## Header

None required (language keyword)

## Core API Quick Reference

| Operation | Signature | Description |
|------|------|------|
| Declaration | `enum class Name { A, B, C };` | Basic scoped enumeration, default underlying type is `int` |
| Specify underlying type | `enum class Name : uint8_t { A, B };` | Fixed underlying type, saves memory |
| Access enumerator | `Name::A` | Must be accessed via the scope operator |
| Convert to integer | `static_cast<int>(Name::A)` | Requires explicit conversion, no implicit conversion |
| Opaque declaration | `enum class Name : uint8_t;` | Forward declaration, requires specifying the underlying type |
| using enum | `using enum Name;` | (C++20) Imports enumerators into the current scope |

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

- Specifying the underlying type (such as `uint8_t` or `uint32_t`) allows precise control over memory footprint, making it ideal for protocol parsing and register mapping
- Zero runtime overhead, fully resolved at compile time
- Eliminates naming conflicts, suitable for modular development in large embedded projects
- Explicit type conversions prevent accidental integer comparisons, improving code safety

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.7 | 3.1 | 2010 |

## See Also

- [cppreference: Enumeration declaration](https://en.cppreference.com/w/cpp/language/enum)

---

*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
