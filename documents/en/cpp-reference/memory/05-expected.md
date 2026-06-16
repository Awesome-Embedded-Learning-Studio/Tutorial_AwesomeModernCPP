---
chapter: 99
cpp_standard:
- 23
description: Type-safe wrapper for holding normal values or error information, replacing
  exceptions and dual return value patterns
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
  translated_at: '2026-06-16T03:30:07.197098+00:00'
  engine: anthropic
  token_count: 637
---
<!--
Reference Card Template
For feature cheat sheets under documents/cpp-reference/.
Unlike article-template.md, reference cards use a refined, structured format and do not require a narrative style.

Tag usage rules:
1. Must include 1 platform tag (use 'host' for reference cards)
2. Must include 1 difficulty tag
3. Must include at least 1 topic tag
4. Select from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# std::expected (C++23)

## In a nutshell

Either holds an expected value `T` or an unexpected error `E`—a type-safe, zero-overhead error propagation mechanism that replaces exceptions and the `error_code` pattern.

## Header

```cpp
#include <expected>
```

## Core API Cheat Sheet

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Construct (success) | `expected(T)` | Wraps a normal value |
| Construct (error) | `expected(unexpected<E>)` | Wraps an error (`std::unexpected`) |
| Check success | `has_value()` | Whether it holds a normal value |
| Implicit bool conversion | `operator bool()` | Same as `has_value` |
| Get value | `value()` | Gets reference to normal value (throws exception on failure) |
| Get error | `error()` | Gets reference to the error |
| Dereference | `operator*()` | Gets normal value (unchecked, undefined behavior if error) |
| Chain transform | `transform(f)` | If has value, applies `f` to value and wraps result |
| Chain error handling | `and_then(f)` | If has value, calls `f` and returns its `expected` result |
| Error branch | `or_else(f)` | If has error, calls `f` to handle error |
| Error transform | `transform_error(f)` | If has error, applies `f` to error |
| Create success value | `make_expected(T)` | Factory: directly constructs success |
| Create error value | `make_unexpected(E)` | Factory: constructs `unexpected` for implicit conversion to `expected` |

## Minimal Example

```cpp
#include <expected>
#include <iostream>
#include <string>

std::expected<int, std::string> parse_int(std::string_view str) {
    if (str.empty()) return std::unexpected("Empty string");
    // ... parsing logic ...
    return 42; // Success
}

int main() {
    auto result = parse_int("123");
    if (result) {
        std::cout << "Value: " << result.value() << "\n";
    } else {
        std::cerr << "Error: " << result.error() << "\n";
    }
    return 0;
}
```

## Embedded Applicability: High

- Zero-overhead abstraction: size equals `max(sizeof(T), sizeof(E))` plus a discriminator flag, no heap allocation.
- Replaces exception handling mechanisms, suitable for embedded environments with exceptions disabled (`-fno-exceptions`).
- More type-safe than the `error_code` + output parameter pattern, forcing the caller to handle errors.
- Chaining operations (`transform`/`and_then`) allows composing complex workflows while keeping code linear and readable.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 12 | 16 | 19.36 |

## See Also

- [Tutorial: std::expected Error Handling](../../vol2-modern-features/ch10-error-handling/03-expected-error.md)
- [cppreference: std::expected](https://en.cppreference.com/w/cpp/utility/expected)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
