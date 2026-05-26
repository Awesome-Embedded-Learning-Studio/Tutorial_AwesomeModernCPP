---
title: std::variant
description: A type-safe union that holds a value of one of its alternative types
  at any given time
chapter: 99
order: 3
tags:
- host
- cpp-modern
- intermediate
difficulty: intermediate
cpp_standard:
- 17
- 20
- 23
translation:
  source: documents/cpp-reference/containers/03-variant.md
  source_hash: e34295641193107d22e44d40b7c148c46c73d1b120c249d15b16c2a8fd11743d
  translated_at: '2026-05-26T10:13:22.028996+00:00'
  engine: anthropic
  token_count: 443
---
# std::variant (C++17)

## In a Nutshell

A type-safe alternative to `union` that stores values of different types in the same memory region, with access by index or type safety.

## Header

`#include <variant>`

## Core API Quick Reference

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Constructor | `variant()` | Default constructs, holding a value of the first candidate type |
| Assignment | `variant& operator=(T&& t)` | Assigns a value and switches to the corresponding type |
| Access by type | `template<class T> T& get(variant& v)` | Retrieves a value by type, throws an exception on type mismatch |
| Access by index | `template<size_t I> T& get(variant& v)` | Retrieves a value by index, throws an exception on out-of-bounds index |
| Safe access | `template<class T> T* get_if(variant* v)` | Retrieves a pointer by type, returns `nullptr` on mismatch |
| Type check | `template<class T> bool holds_alternative(const variant& v)` | Checks if the variant currently holds the specified type |
| Visitor | `template<class Vis> R visit(Vis&& vis, variant& v)` | Passes a callable object, automatically dispatching to the active type |
| Current index | `size_t index() const` | Returns the zero-based index of the currently active type |
| In-place construction | `template<class T, class... Args> T& emplace(Args&&... args)` | Destroys the old value and constructs a new value in-place |

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

- Compared to a bare `union`, it implies extra storage for a type index and runtime checking overhead.
- It eliminates the error-prone manual management of `union` dirty flags, improving code robustness.
- It is well-suited for application-layer state management or message parsing on resource-rich targets (such as SoCs with an MMU).
- For extremely constrained bare-metal environments, we recommend evaluating the `sizeof` overhead before using it cautiously.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 7.1 | 5.0   | 19.10 |

## See Also

- [cppreference: std::variant](https://en.cppreference.com/w/cpp/utility/variant)

---

*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
