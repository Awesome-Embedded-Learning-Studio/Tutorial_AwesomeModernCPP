---
title: Generic lambda
description: Allow Lambda expression parameters to use the auto placeholder, with
  the compiler automatically deducing the type.
chapter: 99
order: 9
tags:
- host
- cpp-modern
- intermediate
difficulty: intermediate
cpp_standard:
- 14
- 17
- 20
- 23
---
# Generic Lambda (C++14)

## In a Nutshell

Allows lambda expression parameters to support `auto`, eliminating the hassle of writing multiple overloads for different types. It is equivalent to generating a templated `operator()`.

## Header

None (language feature)

## Quick API Reference

| Operation | Signature | Description |
|------|------|------|
| Generic parameter | `[captures](auto a, auto b) { ... }` | Declares parameters using `auto`, generating a template `operator()` based on deduced types |
| Forwarding reference parameter | `[captures](auto&&... ts) { ... }` | Combines with `auto&&` to perfectly forward parameter packs |
| Explicit template parameters (C++20) | `[captures]<class T>(T a) { ... }` | Explicitly declares template parameters using angle brackets after square brackets, supporting constraints |
| Captureless conversion to function pointer | `using F = ret(*)(params); operator F() const;` | A captureless generic lambda can implicitly convert to a function pointer (constexpr since C++17) |

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

## Embedded Applicability: High

- Zero runtime overhead; `auto` is deduced only at compile time, and the generated code is identical to hand-written templates
- Ideal for writing generic callback functions (such as sort comparators, timer callbacks), reducing template code redundancy
- The C++14 `auto` syntax is widely supported by GCC 5+ / Clang 3.4+, and can be used with mainstream embedded toolchains

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 5.0 | 3.4   | 19.0 |

## See Also

- [cppreference: Lambda expressions](https://en.cppreference.com/w/cpp/language/lambda)

---

*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
