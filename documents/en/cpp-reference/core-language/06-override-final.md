---
chapter: 99
cpp_standard:
- 11
- 14
- 17
- 20
- 23
description: Used after a member function declaration to ensure that the function
  actually overrides a virtual function from the base class; otherwise, a compilation
  error occurs.
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
  translated_at: '2026-06-16T03:28:55.315876+00:00'
  engine: anthropic
  token_count: 370
---
# override Specifier (C++11)

## In a Nutshell

Appending `override` to a virtual function declaration instructs the compiler to verify that the function successfully overrides a base class virtual function. Signature mismatches or attempts to override non-virtual functions will result in a compilation error.

## Header

None (This is a language-level keyword feature)

## Core API Quick Reference

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Function declaration | `void foo() override;` | Used in declarations to ensure overriding of a base class virtual function |
| Function definition (in-class) | `void foo() override { }` | Used when defining the function inside the class |
| Pure virtual function override | `void foo() override = 0;` | `override` appears before `= 0` |
| Combined with final | `void foo() override final;` | Can be combined with `final` in any order |
| Destructor override | `~Derived() override;` | Can be used to check overriding of virtual destructors |

## Minimal Example

```cpp
struct Base {
    virtual void func() { /* ... */ }
    virtual void only_in_base() { /* ... */ }
};

struct Derived : Base {
    // Correctly overrides Base::func
    void func() override { /* ... */ }

    // Error: 'only_in_base' is not virtual in Base
    // void only_in_base() override;

    // Error: signature mismatch (const qualifier)
    // void func() const override;
};
```

## Embedded Applicability: High

- Zero runtime overhead; performs static checks exclusively at compile time.
- Embedded code often features multi-layer Hardware Abstraction Layers (HALs); `override` effectively prevents silent errors caused by changes to base class interfaces.
- Does not impact code size or execution speed, making it suitable for resource-constrained environments.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.7 | 3.0 | 2012 |

## See Also

- [cppreference: override specifier](https://en.cppreference.com/w/cpp/language/override)

---

*Part of the content references [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
