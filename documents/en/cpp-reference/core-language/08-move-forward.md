---
chapter: 99
cpp_standard:
- 11
- 14
- 17
- 20
- 23
description: Converts an lvalue to an rvalue reference, triggering move semantics
  for efficient resource transfer
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
  translated_at: '2026-09-27T01:36:19+00:00'
  engine: anthropic
  token_count: 1050
---
# std::move (C++11)

## In a Nutshell

It force-casts an lvalue to an rvalue reference, telling the compiler "this object's resources are up for grabs" — which triggers move construction or move assignment and avoids a deep copy.

## Header

`#include <utility>`

## Core API Quick Reference

| Operation | Signature | Description |
|------|------|------|
| Move cast (since C++14) | `template<class T> constexpr std::remove_reference_t<T>&& move(T&& t) noexcept;` | Converts the object `t` to an rvalue reference (xvalue) |
| Perfect forwarding | `template<class T> T&& forward(typename std::remove_reference<T>::type& t) noexcept;` | Preserves the value category in forwarding-reference scenarios; works in tandem with `std::move` |
| Conditional move | `template<class T> typename std::conditional<...>::type move_if_noexcept(T& t) noexcept;` | Converts to an rvalue if the move constructor is non-throwing; otherwise returns an lvalue |

## Minimal Example

```cpp
#include <iostream>
#include <string>
#include <utility>
#include <vector>
// Standard: C++11
int main() {
    std::string str = "Hello";
    std::vector<std::string> v;
    v.push_back(str);              // copy
    v.push_back(std::move(str));   // move; str is left in a valid but unspecified state
    std::cout << v[0] << " " << v[1] << "\n";
    std::cout << "str empty: " << str.empty() << "\n";
}
```

## Embedded Suitability: High

- Zero-overhead abstraction: `std::move` is essentially a `static_cast`, done at compile time with no runtime cost
- Avoids deep copies: when passing large buffers (such as `std::vector<uint8_t>` or `std::string`), it significantly cuts both RAM usage and CPU overhead
- Pairs with custom resource classes: can be used to transfer ownership of raw pointers (in combination with RAII), replacing manual resource handover
- Note that a moved-from object is in a "valid but unspecified" state: you can no longer read its value — you can only assign to it or destroy it

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.6 | 3.0   | 19.0 |

## See Also

- [cppreference: std::move](https://en.cppreference.com/w/cpp/utility/move)

---

*Some content is adapted from [cppreference.com](https://en.cppreference.com/), used under the [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) license*
