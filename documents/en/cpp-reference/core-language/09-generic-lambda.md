---
chapter: 99
cpp_standard:
- 14
- 17
- 20
- 23
description: Allows lambda expression parameters to use the `auto` placeholder, with
  the compiler automatically deducing the type.
difficulty: intermediate
order: 9
reading_time_minutes: 1
tags:
- host
- cpp-modern
- intermediate
title: Generic Lambda
translation:
  source: documents/cpp-reference/core-language/09-generic-lambda.md
  source_hash: c84f4a3a2415c50821a89447dc615af436635d1ea4b2d2d831b10db864b45e60
  translated_at: '2026-06-16T03:29:02.629051+00:00'
  engine: anthropic
  token_count: 364
---
# Generic Lambdas (C++14)

## In a Nutshell

Allows lambda expression parameters to support `auto`, eliminating the hassle of writing multiple overloads for different types. It effectively generates a templated `operator()`.

## Header

None (language feature)

## Core API Quick Reference

| Operation | Signature | Description |
|------|------|------|
| Generic parameters | `[](auto x) {}` | Use `auto` to declare parameters; generates a templated `operator()` based on deduced types |
| Forwarding reference parameters | `[](auto&& x) {}` | Combine with `std::forward` to perfectly forward parameter packs |
| Explicit template parameters (C++20) | `[]<typename T>(T x) {}` | Explicitly declare template parameters using angle brackets after the square brackets; supports constraints |
| No-capture function pointer conversion | `[](auto x) {}` | No-capture generic lambdas can be implicitly converted to function pointers (since C++17, `constexpr`) |

## Minimal Example

```cpp
#include <algorithm>
#include <vector>
#include <iostream>

int main() {
    std::vector<int> v{5, 2, 8, 1, 9};

    // Generic lambda: works with int, double, or custom types supporting comparison
    auto greater = [](auto a, auto b) {
        return a > b;
    };

    // Sort in descending order
    std::sort(v.begin(), v.end(), greater);

    for (const auto& x : v) {
        std::cout << x << " ";
    }
    // Output: 9 8 5 2 1
}
```

## Embedded Applicability: High

- Zero runtime overhead; `auto` is deduced at compile-time only, and the generated code is identical to hand-written templates.
- Ideal for writing generic callback functions (e.g., sorting comparators, timer callbacks), reducing template code redundancy.
- The C++14 `auto` syntax is widely supported by GCC 5+ and Clang 3.4+, making it usable with mainstream embedded toolchains.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 5.0 | 3.4   | 19.0 |

## See Also

- [cppreference: Lambda expressions](https://en.cppreference.com/w/cpp/language/lambda)

---

*Part of the content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
