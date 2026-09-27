---
chapter: 99
cpp_standard:
- 14
- 17
- 20
- 23
description: Allows lambda expression parameters to use the auto placeholder, with the compiler deducing the types automatically
difficulty: intermediate
order: 9
reading_time_minutes: 1
tags:
- host
- cpp-modern
- intermediate
title: Generic Lambdas
translation:
  source: documents/cpp-reference/core-language/09-generic-lambda.md
  source_hash: c84f4a3a2415c50821a89447dc615af436635d1ea4b2d2d831b10db864b45e60
  translated_at: '2026-09-27T01:38:19+00:00'
  engine: anthropic
  token_count: 500
---
# Generic Lambdas (C++14)

## In a Nutshell

Lets lambda expression parameters take `auto`, sparing us the hassle of writing multiple overloads for different types — it is equivalent to generating a templated `operator()`.

## Header

None (language feature)

## Core API Quick Reference

| Operation | Signature | Description |
|------|------|------|
| Generic parameters | `[captures](auto a, auto b) { ... }` | Declare parameters with `auto`; a templated `operator()` is generated from the deduced types |
| Forwarding reference parameters | `[captures](auto&&... ts) { ... }` | Combine `auto&&` with perfect forwarding of the parameter pack |
| Explicit template parameters (C++20) | `[captures]<class T>(T a) { ... }` | Declare template parameters explicitly in angle brackets after the square brackets; supports constraints |
| No-capture function pointer conversion | `using F = ret(*)(params); operator F() const;` | Capture-less generic lambdas convert implicitly to a function pointer (`constexpr` since C++17) |

## Minimal Example

```cpp
#include <iostream>
// Standard: C++14
int main() {
    auto compare = [](auto a, auto b) { return a < b; };
    std::cout << compare(3, 4) << "\n";       // int vs int
    std::cout << compare(3.14, 2.72) << "\n"; // double vs double
}
```

## Embedded Suitability: High

- Zero runtime overhead: `auto` is deduced purely at compile time, and the generated code is identical to a hand-written template
- A great fit for generic callbacks (sorting comparators, timer callbacks, and the like), cutting down template code redundancy
- The C++14 `auto` syntax is already widely supported by GCC 5+ / Clang 3.4+, so every mainstream embedded toolchain can use it

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 5.0 | 3.4   | 19.0 |

## See Also

- [cppreference: Lambda expressions](https://en.cppreference.com/w/cpp/language/lambda)

---

*Some content references [cppreference.com](https://en.cppreference.com/), used under the [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) license*
