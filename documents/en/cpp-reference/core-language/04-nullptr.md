---
chapter: 99
cpp_standard:
- 11
- 14
- 17
- 20
- 23
description: Type-safe null pointer literal, replacing NULL and 0
difficulty: beginner
order: 4
reading_time_minutes: 1
tags:
- host
- cpp-modern
- beginner
title: nullptr
translation:
  source: documents/cpp-reference/core-language/04-nullptr.md
  source_hash: 029b188e3461d3df1a7d4207784a11c9135b2ab22946c041fbd4fd3aaf05cf82
  translated_at: '2026-09-27T01:32:44+00:00'
  engine: anthropic
  token_count: 370
---
# nullptr (C++11)

## In a Nutshell

A null pointer literal of type `std::nullptr_t` that safely distinguishes integer overloads, completely resolving the ambiguity the `NULL` macro and the integer `0` create in templates and function overloading.

## Header

No header required (it is a language keyword); the type is defined in `<cstddef>`.

## Core API Quick Reference

| Operation | Signature | Description |
|------|------|------|
| Null pointer literal | `nullptr` | A prvalue of type `std::nullptr_t` |
| Implicit conversion | → any pointer type | Converts to the null pointer value of the corresponding type |
| Implicit conversion | → any member pointer type | Converts to the null member pointer value of the corresponding type |

## Minimal Example

```cpp
#include <iostream>
void f(int) { std::cout << "int\n"; }
void f(int*) { std::cout << "int*\n"; }

int main() {
    f(0);        // Calls f(int), possibly unintended
    f(nullptr);  // Calls f(int*), exact match
    int* p = nullptr;
    if (p == nullptr) { std::cout << "null\n"; }
}
```

## Embedded Applicability: High

- A zero-overhead abstraction: the compiler directly produces a null pointer value at compile time, generating the same instructions as `0` or `NULL`
- Avoids integer-vs-pointer overload ambiguity in register-manipulation functions (e.g., overloads that operate on hardware registers)
- Behaves correctly in template metaprogramming (e.g., static assertions, type traits), where `NULL` and `0` would fail
- Fully compatible with C-style low-level hardware-manipulation code, so `NULL` can be replaced incrementally with no risk

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.6 | 3.0 | 2010 |

## See Also

- [cppreference: nullptr](https://en.cppreference.com/w/cpp/language/nullptr)

---

*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
