---
title: "std::expected"
description: "Type-safe wrapper holding either a expected value or an error, replacing exceptions and dual-return patterns"
chapter: 99
order: 5
tags:
  - host
  - cpp-modern
  - intermediate
  - expected
difficulty: intermediate
cpp_standard: [23]
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

# std::expected (C++23)

## In a Nutshell

Holds either the desired value `T` or an unexpected error `E` — a type-safe, zero-overhead error propagation mechanism that replaces exceptions and `std::pair<T, Error>` patterns.

## Header

`#include <expected>`

## Core API Quick Reference

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Construct (success) | `expected(T value)` | Wrap a normal value |
| Construct (error) | `expected(unexpect_t, E err)` | Wrap an error (`std::unexpected{err}`) |
| Check success | `bool has_value() const noexcept` | Whether it holds a normal value |
| Implicit bool | `explicit operator bool() const noexcept` | Same as has_value |
| Get value | `T& value()` | Get reference to value (throws on error) |
| Get error | `const E& error() const` | Get reference to error |
| Dereference | `T& operator*()` | Get value (unchecked, UB if error) |
| Chain transform | `auto transform(F&& f)` | If value, apply f and wrap result |
| Chain and_then | `auto and_then(F&& f)` | If value, call f and return its expected result |
| Error branch | `auto or_else(F&& f)` | If error, call f to handle it |
| Error transform | `auto transform_error(F&& f)` | If error, apply f to the error |
| Create success | `std::expected<T, E>(value)` | Factory: construct success directly |
| Create error | `std::unexpected{err}` | Factory: construct unexpected for implicit conversion to expected |

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

    // Chained call
    auto r3 = divide(20, 4).transform([](int v) { return v * 2; });
    std::cout << *r3 << "\n"; // 10
}
```

## Embedded Applicability: High

- Zero-overhead abstraction: size equals `sizeof(T) + sizeof(E)` plus a discriminator flag, no heap allocation
- Replaces exception handling, suitable for embedded environments with exceptions disabled (`-fno-exceptions`)
- More type-safe than error code + output parameter patterns, forcing callers to handle errors
- Chain operations (transform/and_then) compose complex workflows while keeping code linear and readable

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 12 | 16 | 19.36 |

## See Also

- [cppreference: std::expected](https://en.cppreference.com/w/cpp/utility/expected)

---

*Some content adapted from [cppreference.com](https://en.cppreference.com/) under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) license*
