---
chapter: 99
cpp_standard:
- 11
- 14
- 17
- 20
- 23
description: Placed after a member function declaration to confirm the function really
  overrides a base-class virtual function, with a compile error otherwise
difficulty: beginner
order: 6
reading_time_minutes: 1
tags:
- host
- cpp-modern
- beginner
title: override specifier
translation:
  source: documents/cpp-reference/core-language/06-override-final.md
  source_hash: a8b5f85610928bd6195d5b697fe609acba57eb537a0fb42ad726ace344ffdc25
  translated_at: '2026-09-27T01:37:26+00:00'
  engine: anthropic
  token_count: 350
---
# override specifier (C++11)

## In a Nutshell

Adding `override` at the end of a virtual function declaration asks the compiler to verify that you really did override a base-class virtual function; a signature mismatch or a non-virtual base-class function becomes an immediate compile error.

## Header

None (a language-keyword-level feature)

## Core API Cheat Sheet

| Operation | Signature | Description |
|------|------|------|
| Function declaration | `return_type func_name(params) override;` | Used at declaration time to ensure you are overriding a base-class virtual function |
| Function definition (in-class) | `return_type func_name(params) override { ... }` | Used when defining the function inside the class |
| Pure virtual override | `return_type func_name(params) override = 0;` | `override` appears before `= 0` |
| Combined with final | `return_type func_name(params) override final;` | Can be combined with `final` in either order |
| Destructor override | `~Derived() override;` | Can be used to check overrides of virtual destructors |

## Minimal Example

```cpp
// Standard: C++11
#include <iostream>
struct Base { virtual void foo() { std::cout << "Base\n"; } };
struct Derived : Base {
    // void foo(int) override; // compile error: signature mismatch
    void foo() override { std::cout << "Derived\n"; }
};
int main() {
    Derived d;
    d.foo();
}
```

## Embedded Applicability: High

- Zero runtime overhead; it is purely a compile-time static check
- Embedded code often has hardware abstraction layers (HALs) built on multi-level inheritance, and `override` effectively prevents the silent errors caused by modifications to a base-class interface
- No impact on code size or execution speed, a good fit for resource-sensitive contexts

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.7 | 3.0 | 2012 |

## See Also

- [cppreference: override specifier](https://en.cppreference.com/w/cpp/language/override)

---

*Part of the content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
