---
chapter: 99
cpp_standard:
- 23
description: A type-safe wrapper holding either a normal value or error information,
  replacing exceptions and dual-return-value patterns
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
  engine: anthropic
  source: documents/cpp-reference/memory/05-expected.md
  source_hash: 445e0cacc91a4636be0b4f70b6fc5b25b3a02e2deae1173b09406670529226c7
  token_count: 634
  translated_at: '2026-05-26T10:18:10.339930+00:00'
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

# std::expected (C++23)

## In a Nutshell

Either holds an expected normal value `T`, or an unexpected error `E`—a type-safe, zero-overhead error propagation mechanism that replaces exceptions and the `std::pair<T, Error>` pattern.

## Header

`#include <expected>`

## Core API Quick Reference

| Operation | Signature | Description |
|------|------|------|
| Construct (success value) | `expected(T value)` | Wraps a normal value |
| Construct (error) | `expected(unexpect_t, E err)` | Wraps an error (`std::unexpected{err}`) |
| Check for success | `bool has_value() const noexcept` | Whether it holds a normal value |
| Implicit bool conversion | `explicit operator bool() const noexcept` | Same as has_value |
| Get value | `T& value()` | Gets a reference to the normal value (throws on failure) |
| Get error | `const E& error() const` | Gets a reference to the error |
| Dereference | `T& operator*()` | Gets the normal value (unchecked, undefined behavior if error) |
| Chained transform | `auto transform(F&& f)` | If it has a value, applies f to the value and wraps the result |
| Chained error handling | `auto and_then(F&& f)` | If it has a value, calls f and returns its expected result |
| Error branch | `auto or_else(F&& f)` | If it has an error, calls f to handle the error |
| Error transform | `auto transform_error(F&& f)` | If it has an error, applies f to the error |
| Create success value | `std::expected<T, E>(value)` | Factory: directly constructs a success |
| Create error value | `std::unexpected{err}` | Factory: constructs unexpected for implicit conversion to expected |

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

    // 链式调用
    auto r3 = divide(20, 4).transform([](int v) { return v * 2; });
    std::cout << *r3 << "\n"; // 10
}
```

## Embedded Applicability: High

- Zero-overhead abstraction: size equals `sizeof(T) + sizeof(E)` plus a discriminant flag, no heap allocation
- Replaces exception handling mechanisms, suitable for embedded environments with exceptions disabled (`-fno-exceptions`)
- More type-safe than the error code + output parameter pattern, forcing the caller to handle errors
- Chained operations (transform/and_then) can compose complex business flows while keeping the code linearly readable

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 12 | 16 | 19.36 |

## See Also

- [Tutorial: std::expected Error Handling](../../vol2-modern-features/ch10-error-handling/03-expected-error.md)
- [cppreference: std::expected](https://en.cppreference.com/w/cpp/utility/expected)

---

*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
