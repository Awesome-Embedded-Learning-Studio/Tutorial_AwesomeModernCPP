---
title: "Deducing this"
description: "Explicit object parameter deduction: member functions deduce the type and value category of *this automatically"
chapter: 99
order: 18
tags:
  - host
  - cpp-modern
  - intermediate
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

# Deducing this (C++23)

## In a Nutshell

Write `this Self` or `this Self&&` as the first parameter of a member function, and the compiler automatically deduces based on the calling object's value category (lvalue/rvalue/const) -- eliminating the `const`/non-`const`/rvalue reference overload trilogy.

## Header

None (language feature)

## Core API Quick Reference

| Syntax | Description |
|--------|-------------|
| `void func(this Self&& self)` | Rvalue reference object parameter |
| `void func(this const Self& self)` | const lvalue reference (read-only) |
| `void func(this Self& self)` | Non-const lvalue reference (modifiable) |
| `void func(this auto&& self)` | Perfect forwarding, one definition covers all value categories |
| With templates | `template<class Self> void func(this Self&& self)` templatized explicit object parameter |
| CRTP simplification | Explicit object parameter can directly replace CRTP, reducing base class overhead |

## Minimal Example

```cpp
// Standard: C++23
#include <iostream>
#include <utility>

struct Wrapper {
    int value;

    // One function covers const / non-const / rvalue scenarios
    template <typename Self>
    auto&& get(this Self&& self) {
        return std::forward<Self>(self).value;
    }
};

int main() {
    Wrapper w{42};
    const Wrapper cw{99};

    std::cout << w.get() << "\n";   // 42 (non-const lvalue)
    std::cout << cw.get() << "\n";  // 99 (const lvalue)
    std::cout << Wrapper{7}.get() << "\n"; // 7 (rvalue)
}
```

## Embedded Applicability: Medium

- Reduces boilerplate: one explicit object parameter replaces three `const`/non-`const`/rvalue overloads
- Simplifies CRTP: deduces types directly in member functions, eliminating base class indirection overhead
- Especially useful for recursive lambdas and chained-call APIs
- C++23 feature, compiler support is still advancing (GCC 14.1+, Clang 18+, MSVC 19.34+)
- Embedded toolchain upgrade cycles are long; not suitable in the short term for projects requiring broad compatibility

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 14.1 | 18 | 19.34 |

## See Also

- [cppreference: Deducing this](https://en.cppreference.com/w/cpp/language/member_functions#Explicit_object_parameter)

---

*Some content adapted from [cppreference.com](https://en.cppreference.com/) under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) license*
