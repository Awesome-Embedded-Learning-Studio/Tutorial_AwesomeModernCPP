---
chapter: 99
cpp_standard:
- 17
- 20
- 23
description: A wrapper that may or may not contain a value, used to safely express
  "no value" semantics
difficulty: beginner
order: 3
reading_time_minutes: 1
tags:
- host
- cpp-modern
- beginner
title: std::optional
translation:
  source: documents/cpp-reference/memory/03-optional.md
  source_hash: 9ec38736539e011433bfc3498bff703caabaa1466021aab14d987d7db90d69c6
  translated_at: '2026-09-27T02:14:43+00:00'
  engine: anthropic
  token_count: 800
---
# std::optional (C++17)

## In a Nutshell

A container for representing "a value that may not exist" — safer and more intuitive than returning a `bool` plus a pointer or an output parameter.

## Header

`#include <optional>`

## Core API Cheat Sheet

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Construct | `optional()` | Default construction; contains no value |
| Assign empty | `optional& operator=(nullopt_t)` | Sets the state to no value |
| Check for value | `explicit operator bool() const` | Returns `true` when a value is present |
| Check for value | `bool has_value() const` | Same as above |
| Access value | `T& operator*()` | Dereferences to obtain the value (undefined behavior when there is no value) |
| Safe access | `T& value()` | Returns the value; throws `bad_optional_access` if there is none |
| Value or default | `T value_or(const T& default_value) const` | Returns the value if present, otherwise the default value |
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

- Zero-overhead abstraction: the no-value state takes up only a `bool`-sized amount of storage, and no heap allocation is involved
- Can replace raw pointers as the return type of functions that might fail, avoiding the risk of dereferencing a null pointer
- Fully supported since C++17; member functions become fully `constexpr` from C++23 onward, further widening the range of applicable scenarios

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| TBD | TBD | TBD |

## See Also

- [cppreference: std::optional](https://en.cppreference.com/w/cpp/utility/optional)

---

*Some content is adapted from [cppreference.com](https://en.cppreference.com/), used under the [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) license*
