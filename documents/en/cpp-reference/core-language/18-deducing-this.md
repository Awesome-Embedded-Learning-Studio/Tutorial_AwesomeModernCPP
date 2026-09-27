---
chapter: 99
cpp_standard:
- 23
description: 'Explicit object parameter deduction: the first parameter of a member function is automatically deduced to match the type and value category of *this'
difficulty: intermediate
order: 18
reading_time_minutes: 2
tags:
- host
- cpp-modern
- intermediate
title: Deducing this
translation:
  source: documents/cpp-reference/core-language/18-deducing-this.md
  source_hash: 4390c71b51c4db98a286c470411528823593f770c902c4f3f12ca61e708a26ce
  translated_at: '2026-09-27T01:49:21+00:00'
  engine: anthropic
  token_count: 490
---
<!--
Reference Card Template
Used for feature cheat-sheet pages under documents/cpp-reference/.
Unlike article-template.md, reference cards use a refined, structured format and do not require a narrative style.

Tag usage rules:
1. Must include 1 platform tag (reference cards uniformly use host)
2. Must include 1 difficulty tag
3. Include at least 1 topic tag
4. Select from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# Deducing this (C++23)

## In a Nutshell

Write the first parameter of a member function as `this Self` or `this Self&&`, and the compiler deduces it automatically from the value category of the calling object (lvalue/rvalue/const)—eliminating the `const`/non-`const`/rvalue-reference overload trio.

## Header

None (language feature)

## Core API Cheat Sheet

| Syntax | Description |
|------|------|
| `void func(this Self&& self)` | Rvalue-reference explicit object parameter |
| `void func(this const Self& self)` | `const` lvalue reference (read-only) |
| `void func(this Self& self)` | Non-`const` lvalue reference (mutable) |
| `void func(this auto&& self)` | Perfect forwarding—one definition covers all value categories |
| Combined with templates | `template<class Self> void func(this Self&& self)` templated explicit object parameter |
| CRTP simplification | Explicit object parameters can directly replace CRTP, reducing base-class overhead |

## Minimal Example

```cpp
// Standard: C++23
#include <iostream>
#include <utility>

struct Wrapper {
    int value;

    // One function covers all three scenarios: const / non-const / rvalue
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

## Embedded Applicability: Moderate

- Less boilerplate: one explicit object parameter replaces the `const`/non-`const`/rvalue overload trio
- Simplifies CRTP: types are deduced directly in the member function, eliminating base-class indirection overhead
- Especially useful for recursive lambdas and chained-call APIs
- A C++23 feature; compiler support is still rolling out (GCC 14.1+, Clang 18+, MSVC 19.34+)
- Embedded toolchains have long upgrade cycles, so it is not suitable in the short term for projects that need broad compatibility

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 14.1 | 18 | 19.34 |

## See Also

- [cppreference: Deducing this](https://en.cppreference.com/w/cpp/language/member_functions#Explicit_object_parameter)

---

*Some content is referenced from [cppreference.com](https://en.cppreference.com/), used under the [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) license*
