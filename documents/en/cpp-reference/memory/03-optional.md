---
chapter: 99
cpp_standard:
- 17
- 20
- 23
description: A wrapper that may or may not contain a value, used to safely express
  "no value" semantics.
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
  translated_at: '2026-06-16T03:29:39.393857+00:00'
  engine: anthropic
  token_count: 404
---
# std::optional (C++17)

## In a Nutshell

A container used to represent "a value that may not exist," which is safer and more intuitive than returning a status code plus a pointer or using output parameters.

## Header File

`<optional>`

## Core API Cheat Sheet

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Construct | `optional()` | Default constructor, does not contain a value |
| Assign empty | `reset()` or `= nullopt` | Sets the state to valueless |
| Check has value | `has_value()` | Returns `true` if a value is present |
| Check has value | `operator bool()` | Same as above |
| Get value | `operator*()` or `operator->()` | Dereference to get value (undefined behavior if no value) |
| Safe get | `value()` | Get value, throws `bad_optional_access` if no value |
| Value or default | `value_or()` | Returns value if present, otherwise returns default value |
| In-place construct | `emplace()` | Constructs value in-place |
| Reset | `reset()` | Destroys the contained value |

## Minimal Example

```cpp
#include <optional>
#include <iostream>

// A function that might fail
std::optional<int> divide(int a, int b) {
    if (b == 0) {
        return std::nullopt; // Indicate failure
    }
    return a / b; // Indicate success
}

int main() {
    auto result = divide(10, 2);

    // Check if result contains a value
    if (result) {
        std::cout << "Result: " << *result << '\n';
    } else {
        std::cout << "Division failed\n";
    }

    // Get value or default
    auto safe_result = divide(10, 0).value_or(-1);
    std::cout << "Safe result: " << safe_result << '\n';
}
```

## Embedded Applicability: High

- Zero-overhead abstraction; when no value is present, it only occupies storage space equivalent to one byte (plus alignment/padding), and involves no heap allocation.
- Can replace raw pointers as return values for functions that may fail, avoiding the risks of null pointer dereferencing.
- Fully supported since C++17; member functions are comprehensively `constexpr` in C++23 and later, further broadening the range of applicable scenarios.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| TBD | TBD | TBD |

## See Also

- [cppreference: std::optional](https://en.cppreference.com/w/cpp/utility/optional)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
