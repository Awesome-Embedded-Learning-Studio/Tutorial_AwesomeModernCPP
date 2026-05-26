---
title: override specifier
description: Used after a member function declaration to ensure the function actually
  overrides a base class virtual function, otherwise a compilation error occurs.
chapter: 99
order: 6
tags:
- host
- cpp-modern
- beginner
difficulty: beginner
cpp_standard:
- 11
- 14
- 17
- 20
- 23
translation:
  source: documents/cpp-reference/core-language/06-override-final.md
  source_hash: b7306d0b76990914cc840f8b6868f856de44f05b98ed09558670e282d94d535b
  translated_at: '2026-05-26T10:15:51.790175+00:00'
  engine: anthropic
  token_count: 366
---
# override Specifier (C++11)

## In a Nutshell

Appending `override` to the end of a virtual function declaration lets the compiler verify that it truly overrides a base class virtual function. A signature mismatch or a non-virtual base class function will trigger a compile-time error.

## Header

None (language keyword-level feature)

## Core API Quick Reference

| Operation | Signature | Description |
|------|------|------|
| Function declaration | `ret_type func(params) override;` | Used in declarations to ensure a base class virtual function is overridden |
| Function definition (in-class) | `ret_type func(params) override { ... }` | Used for in-class definitions |
| Pure virtual function override | `ret_type func(params) override = 0;` | `override` appears before `= 0` |
| Combined with final | `ret_type func(params) override final;` | Can be combined with `final` in any order |
| Destructor override | `~ClassName() override;` | Can be used to check the overriding of virtual destructors |

## Minimal Example

```cpp
class Sensor {
public:
    virtual void read() = 0;
    virtual ~Sensor() = default;
};

class TemperatureSensor : public Sensor {
public:
    // Correct: overrides base class pure virtual function
    void read() override {
        // Read temperature data
    }

    // Error: misspelled name, compiler catches this thanks to override
    // void reed() override;

    ~TemperatureSensor() override = default;
};
```

## Embedded Applicability: High

- Zero runtime overhead; performs static checking at compile time only
- Embedded code often features multi-level inheritance in the HAL (Hardware Abstraction Layer), and `override` effectively prevents silent errors caused by base class interface modifications
- Does not affect code size or execution speed, making it suitable for resource-constrained scenarios

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.7 | 3.0 | 2012 |

## See Also

- [cppreference: override specifier](https://en.cppreference.com/w/cpp/language/override)

---

*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
