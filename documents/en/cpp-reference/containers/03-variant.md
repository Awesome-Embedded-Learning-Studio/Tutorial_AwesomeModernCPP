---
chapter: 99
cpp_standard:
- 17
- 20
- 23
description: Type-safe unions that hold the value of one of their candidate types
  at any given moment
difficulty: intermediate
order: 3
reading_time_minutes: 2
tags:
- host
- cpp-modern
- intermediate
title: std::variant
translation:
  source: documents/cpp-reference/containers/03-variant.md
  source_hash: a15f4943dd3936608ec1a1fe726992660c0b50ec63312f4202f2205af881d93b
  translated_at: '2026-06-16T03:28:19.596863+00:00'
  engine: anthropic
  token_count: 447
---
# std::variant (C++17)

## In a Nutshell

A type-safe alternative to a union that stores values of different types in the same memory location, accessible via index or type-safe access.

## Header

`#include <variant>`

## Core API Cheat Sheet

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Constructor | `variant()` | Default constructs, holding a value of the first candidate type |
| Assignment | `variant& operator=(T&& t)` | Assigns a value and switches to the corresponding type |
| Access by Type | `template<class T> T& get(variant& v)` | Retrieves value by type, throws exception if type does not match |
| Access by Index | `template<size_t I> T& get(variant& v)` | Retrieves value by index, throws exception if index is out of bounds |
| Safe Access | `template<class T> T* get_if(variant* v)` | Retrieves pointer by type, returns nullptr if no match |
| Type Check | `template<class T> bool holds_alternative(const variant& v)` | Checks if the variant currently holds the specified type |
| Visitor | `template<class Vis> R visit(Vis&& vis, variant& v)` | Passes a callable object, automatically dispatches to the currently active type |
| Current Index | `size_t index() const` | Returns the zero-based index of the currently active type |
| In-place Construction | `template<class T, class... Args> T& emplace(Args&&... args)` | Destroys the old value and constructs a new value in-place |

## Minimal Example

```cpp
#include <iostream>
#include <string>
#include <variant>
// Standard: C++17
int main() {
    std::variant<int, std::string> v = 42;
    std::cout << std::get<int>(v) << '\n';
    v = "hello";
    std::cout << std::get<std::string>(v) << '\n';
    std::visit([](auto&& arg) {
        std::cout << arg << '\n';
    }, v);
}
```

## Embedded Applicability: Medium

- Compared to a bare union, it implies overhead for storing a type index and runtime checks.
- Avoids the risk of errors associated with manually managing union dirty flags, improving code robustness.
- Suitable for application-layer state management or message parsing in resource-rich environments (e.g., SoCs with MMUs).
- In extremely constrained bare-metal environments, we recommend evaluating the `sizeof` overhead before use.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 7.1 | 5.0   | 19.10 |

## See Also

- [cppreference: std::variant](https://en.cppreference.com/w/cpp/utility/variant)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
