---
chapter: 99
cpp_standard:
- 11
- 14
- 17
- 20
- 23
description: Converts an lvalue to an rvalue reference, triggering move semantics
  to enable efficient resource transfer.
difficulty: intermediate
order: 8
reading_time_minutes: 2
tags:
- host
- cpp-modern
- intermediate
title: std::move
translation:
  source: documents/cpp-reference/core-language/08-move-forward.md
  source_hash: 7e16d37c12fb3e02f7179844902934715a0d4e3ca007953775110f4590fa6345
  translated_at: '2026-06-16T03:29:03.189591+00:00'
  engine: anthropic
  token_count: 420
---
# std::move (C++11)

## In a Nutshell

Casts an lvalue to an rvalue reference, signaling to the compiler that "this object's resources can be stolen," thereby triggering move construction or move assignment to avoid deep copies.

## Header File

<utility>

## Core API Cheat Sheet

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Move cast (Since C++14) | `remove_reference_t<T>&& move(T&& t) noexcept;` | Casts object `t` to an rvalue reference (xvalue) |
| Perfect forwarding | `T&& forward(T&& t) noexcept;` | Preserves value category in forwarding reference scenarios, must be used with `T&&` |
| Conditional move | `T&& move_if_noexcept(T& t) noexcept;` | Casts to rvalue if move constructor is non-throwing; otherwise returns lvalue |

## Minimal Example

```cpp
#include <utility>
#include <iostream>
#include <vector>

class Buffer {
    std::vector<int> data_;
public:
    Buffer(size_t size) : data_(size) {}
    // Move constructor
    Buffer(Buffer&& other) noexcept : data_(std::move(other.data_)) {
        std::cout << "Move constructor called\n";
    }
    // Move assignment
    Buffer& operator=(Buffer&& other) noexcept {
        if (this != &other) {
            data_ = std::move(other.data_);
        }
        return *this;
    }
};

int main() {
    Buffer a(1000);
    // Explicitly cast lvalue 'a' to rvalue to trigger move
    Buffer b = std::move(a);
    // 'a' is now in a valid but unspecified state
    return 0;
}
```

## Embedded Applicability: High

- **Zero-overhead abstraction**: `std::move` is essentially a `static_cast<T&&>`, completed at compile time with no runtime cost.
- **Avoid deep copies**: Significantly reduces RAM usage and CPU overhead when passing large buffers (like `std::vector`, `std::string`).
- **Works with custom resource classes**: Can be used to transfer ownership of raw pointers (requires RAII), replacing manual resource handover.
- **Note**: The moved-from object is in a "valid but unspecified" state; do not read its value, only assign to it or destroy it.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.6 | 3.0   | 19.0 |

## See Also

- [cppreference: std::move](https://en.cppreference.com/w/cpp/utility/move)

---

*Part of the content references [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
