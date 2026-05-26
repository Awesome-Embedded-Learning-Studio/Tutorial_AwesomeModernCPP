---
title: std::optional
description: A wrapper that may or may not contain a value, used to safely express
  a "no value" semantic.
chapter: 99
order: 3
tags:
- host
- cpp-modern
- beginner
difficulty: beginner
cpp_standard:
- 17
- 20
- 23
translation:
  source: documents/cpp-reference/memory/03-optional.md
  source_hash: 79fa4c5e44a437944026ea566af1a0f9662962fad408c26544fc1b8d9748f00d
  translated_at: '2026-05-26T10:17:33.868342+00:00'
  engine: anthropic
  token_count: 400
---
# std::optional (C++17)

## In a Nutshell

A container that represents "a value may or may not exist," which is safer and more intuitive than returning a `bool` plus a pointer or using an output parameter.

## Header

`#include <optional>`

## Core API Cheat Sheet

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Construct | `optional()` | Default constructor, contains no value |
| Assign empty | `optional& operator=(nullopt_t)` | Sets the state to no value |
| Check for value | `explicit operator bool() const` | Returns `true` when a value is present |
| Check for value | `bool has_value() const` | Same as above |
| Access value | `T& operator*()` | Dereferences to get the value (undefined behavior if no value is present) |
| Safe access | `T& value()` | Gets the value, throws `bad_optional_access` if no value is present |
| Value or default | `T value_or(const T& default_value) const` | Returns the value if present, otherwise returns the default value |
| In-place construct | `T& emplace(Args&&... args)` | Constructs the value in place |
| Reset | `void reset() noexcept` | Destroys the contained value |

## Minimal Example

```cpp
#include <iostream>
#include <optional>
#include <string>

std::optional<std::string> find(bool b) {
    return b ? std::optional<std::string>{"found"} : std::nullopt;
}

int main() {
    auto res = find(false);
    std::cout << res.value_or("not found") << '\n';

    if (auto val = find(true))
        std::cout << *val << '\n';
}
```

## Embedded Applicability: High

- A zero-overhead abstraction; when no value is present, it only occupies storage the size of one `bool`, with no heap allocation involved.
- Can replace raw pointers as function return values for operations that might fail, avoiding the risk of null pointer dereferences.
- Fully supported since C++17, and member functions are comprehensively `constexpr` starting in C++23, further broadening its applicable scenarios.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| TBA | TBA | TBA |

## See Also

- [cppreference: std::optional](https://en.cppreference.com/w/cpp/utility/optional)

---

*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
