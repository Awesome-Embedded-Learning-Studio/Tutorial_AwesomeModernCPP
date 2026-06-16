---
chapter: 99
cpp_standard:
- 11
- 14
- 17
- 20
- 23
description: Type-safe null pointer literal, replacing `NULL` and `0`
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
  translated_at: '2026-06-16T03:28:47.642339+00:00'
  engine: anthropic
  token_count: 316
---
# nullptr (C++11)

## In a Nutshell

A null pointer literal of type `std::nullptr_t` that safely distinguishes integer overloads, completely resolving the ambiguity caused by the macro `NULL` and the integer `0` in templates and function overloading.

## Header

No header required (language keyword); the type is defined in `<cstddef>`.

## Core API Quick Reference

| Operation | Signature | Description |
|------|------|------|
| Null pointer literal | `nullptr` | A prvalue of type `std::nullptr_t` |
| Implicit conversion | → Any pointer type | Converts to a null pointer value of the corresponding type |
| Implicit conversion | → Any member pointer type | Converts to a null member pointer value of the corresponding type |

## Minimal Example

```cpp
void f(int* p) {
    // Handle pointer
}

void f(int i) {
    // Handle integer
}

int main() {
    // Calls f(int*)
    f(nullptr);

    // Calls f(int)
    f(0);
}
```

## Embedded Applicability: High

- A zero-overhead abstraction; the compiler directly generates a null pointer value, producing the same instructions as `0` or `NULL`.
- Avoids ambiguity between integer and pointer overloads in register manipulation functions (e.g., overloads for hardware registers).
- Behaves correctly in template metaprogramming (e.g., static assertions, type traits), whereas `NULL` and `0` would fail.
- Fully compatible with C-style low-level hardware manipulation code, allowing for risk-free, gradual replacement.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.6 | 3.0 | 2010 |

## See Also

- [cppreference: nullptr](https://en.cppreference.com/w/cpp/language/nullptr)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
