---
chapter: 99
cpp_standard:
- 17
- 20
- 23
description: A type-safe union that holds the value of one of its candidate types
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
  translated_at: '2026-09-26T17:12:53+00:00'
  engine: anthropic
  token_count: 1100
---
# std::variant (C++17)

## In a Nutshell

A type-safe replacement for a union: it stores values of different types in the same chunk of memory and lets you access them safely by index or by type.

## Header File

`#include <variant>`

## Core API Cheat Sheet

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Constructor | `variant()` | Default-constructs the variant, holding a value of the first candidate type |
| Assignment | `variant& operator=(T&& t)` | Assigns the value and switches the active alternative accordingly |
| Access by type | `template<class T> T& get(variant& v)` | Fetches the value by type; throws an exception if the type does not match |
| Access by index | `template<size_t I> T& get(variant& v)` | Fetches the value by index; throws an exception if the index is out of range |
| Safe access | `template<class T> T* get_if(variant* v)` | Fetches a pointer by type; returns nullptr on mismatch |
| Type check | `template<class T> bool holds_alternative(const variant& v)` | Checks whether the variant currently holds the specified type |
| Visitor | `template<class Vis> R visit(Vis&& vis, variant& v)` | Passes in a callable and dispatches it to the currently active type |
| Current index | `size_t index() const` | Returns the zero-based index of the currently active type |
| In-place construction | `template<class T, class... Args> T& emplace(Args&&... args)` | Destroys the old value and constructs a new one in place |

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

- Compared with a bare union, it carries extra storage for the type index plus runtime check overhead
- It spares us the error-prone job of hand-managing union dirty flags, which makes the code more robust
- A good fit for application-layer state management or message parsing on reasonably resourced targets (e.g., SoCs with an MMU)
- On severely constrained bare-metal targets, weigh the `sizeof` overhead carefully before committing to it

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 7.1 | 5.0   | 19.10 |

## See Also

- [cppreference: std::variant](https://en.cppreference.com/w/cpp/utility/variant)

---

*Part of the content references [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
