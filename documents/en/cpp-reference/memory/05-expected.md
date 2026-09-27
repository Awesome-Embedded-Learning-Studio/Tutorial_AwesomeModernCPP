---
chapter: 99
cpp_standard:
- 23
description: A type-safe wrapper that carries either a normal value or an error, replacing
  exceptions and two-return-value patterns
difficulty: intermediate
order: 5
reading_time_minutes: 2
tags:
- host
- cpp-modern
- intermediate
- expected
title: std::expected
translation:
  source: documents/cpp-reference/memory/05-expected.md
  source_hash: 216bd3947b36d90ef096a54181bcad6d17d952bf40ca92fdc8bb27b6f92b1d66
  translated_at: '2026-09-27T02:16:45+00:00'
  engine: anthropic
  token_count: 620
---
<!--
Reference Card Template
For feature cheat sheets under documents/cpp-reference/.
Unlike article-template.md, reference cards use a concise, structured format and do not require a narrative style.

Tag usage rules:
1. Must include 1 platform tag (reference cards uniformly use 'host')
2. Must include 1 difficulty tag
3. Must include at least 1 topic tag
4. Select from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# std::expected (C++23)

## In a Nutshell

Holds either the expected normal value `T` or an unexpected error `E` — a type-safe, zero-overhead error-propagation mechanism that replaces exceptions and the `std::pair<T, Error>` pattern.

## Header

`#include <expected>`

## Core API Cheat Sheet

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Construct (success value) | `expected(T value)` | Wraps a normal value |
| Construct (error) | `expected(unexpect_t, E err)` | Wraps an error (`std::unexpected{err}`) |
| Check for success | `bool has_value() const noexcept` | Whether a normal value is held |
| Implicit bool conversion | `explicit operator bool() const noexcept` | Same as has_value |
| Get the value | `T& value()` | Returns a reference to the normal value (throws on failure) |
| Get the error | `const E& error() const` | Returns a reference to the error |
| Dereference | `T& operator*()` | Accesses the normal value (unchecked; undefined behavior if it holds an error) |
| Chained transform | `auto transform(F&& f)` | If a value is held, applies f to it and wraps the result |
| Chained error handling | `auto and_then(F&& f)` | If a value is held, calls f and returns its expected result |
| Error branch | `auto or_else(F&& f)` | If an error is held, calls f to handle it |
| Error transform | `auto transform_error(F&& f)` | If an error is held, applies f to the error |
| Create a success value | `std::expected<T, E>(value)` | Factory: directly constructs a success |
| Create an error value | `std::unexpected{err}` | Factory: constructs an unexpected for implicit conversion to expected |

## Minimal Example

```cpp
// Standard: C++23
#include <expected>
#include <iostream>
#include <string>

std::expected<int, std::string> divide(int a, int b) {
    if (b == 0) return std::unexpected{"division by zero"};
    return a / b;
}

int main() {
    auto r1 = divide(10, 3);
    if (r1) std::cout << *r1 << "\n"; // 3

    auto r2 = divide(10, 0);
    if (!r2) std::cout << r2.error() << "\n"; // division by zero

    // Chained calls
    auto r3 = divide(20, 4).transform([](int v) { return v * 2; });
    std::cout << *r3 << "\n"; // 10
}
```

## Embedded Applicability: High

- A zero-overhead abstraction: its size is `sizeof(T) + sizeof(E)` plus a discriminant flag, with no heap allocation
- Replaces the exception-handling mechanism, making it a good fit for embedded environments where exceptions are disabled (`-fno-exceptions`)
- More type-safe than the error code + output parameter pattern, forcing callers to handle errors
- Chained operations (transform/and_then) can compose complex workflows while keeping the code linear and readable

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 12 | 16 | 19.36 |

## See Also

- [Tutorial: std::expected Error Handling](../../vol2-modern-features/ch10-error-handling/03-expected-error.md)
- [cppreference: std::expected](https://en.cppreference.com/w/cpp/utility/expected)

---

*Part of the content references [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
