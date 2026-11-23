---
chapter: 99
cpp_standard:
- 14
- 17
- 20
- 23
description: Replaces an object's old value with a new one and returns the old value
difficulty: beginner
order: 10
reading_time_minutes: 1
tags:
- host
- cpp-modern
- beginner
title: std::exchange
translation:
  source: documents/cpp-reference/core-language/10-exchange.md
  source_hash: ee1f9528e38a19244359428cb49beac0a6a314c14e7c63ef9be332ad2eca77fc
  translated_at: '2026-11-22T16:41:57.266510+00:00'
  engine: glm-5.3-refresh
  token_count: 1240
---
# std::exchange (C++14)

## In a Nutshell

Assigns a new value to a variable and hands you its old value at the same time, sparing you a hand-written temporary.

## Header

`#include <utility>`

## Core API Quick Reference

| Operation | Signature | Description |
|------|------|------|
| Replace and return the old value | `template<class T, class U = T> T exchange(T& obj, U&& new_value);` | Replaces `obj` with `new_value` and returns the old value of `obj` |

## Minimal Example

```cpp
// Standard: C++14
#include <iostream>
#include <utility>

int main() {
    int a = 10, b = 20;
    // 交换 a 和 b，无需临时变量
    a = std::exchange(b, a);
    std::cout << a << " " << b << "\n"; // 输出: 20 10

    // 打印斐波那契数列前几项
    for (int x{0}, y{1}; x < 50; x = std::exchange(y, x + y))
        std::cout << x << " ";
}
```

## Embedded Applicability: Medium

- A pure inline function, with no extra heap allocation or system-call overhead
- Relies on move semantics; when using it with custom types, verify the actual cost of the move constructor/assignment
- Very concise for implementing move constructors and state machine transitions, a good fit for resource-rich targets
- `constexpr` since C++20, so it can be used at compile time

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 5.0 | 3.4   | 19.0 |

## See Also

- [cppreference: std::exchange](https://en.cppreference.com/w/cpp/utility/exchange)

---

*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
