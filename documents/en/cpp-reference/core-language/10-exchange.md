---
chapter: 99
cpp_standard:
- 14
- 17
- 20
- 23
description: Replace the old value with a new value and return the old value
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
  source_hash: 835d27d86d82597a3b19ef0f0f1d8ff827e6520aa2d0fca593bc3a73bfcf7865
  translated_at: '2026-06-16T03:28:57.962702+00:00'
  engine: anthropic
  token_count: 310
---
# std::exchange (C++14)

## In a Nutshell

Assigns a new value to a variable while retrieving its old value, avoiding the need for manual temporary variables.

## Header

`#include <utility>`

## Core API Quick Reference

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Replace and return old value | `template<class T, class U = T> T exchange(T& obj, U&& new_value);` | Replaces `obj` with `new_value` and returns the old value of `obj` |

## Minimal Example

```cpp
// Standard: C++14
#include <iostream>
#include <utility>

int main() {
    int a = 10, b = 20;
    // 交换 a 和 b，无需临时变量
    a = std::exchange(b, a);
    std::cout << a << " " << b << "\n"; // 输出: 10 10

    // 打印斐波那契数列前几项
    for (int x{0}, y{1}; x < 50; x = std::exchange(y, x + y))
        std::cout << x << " ";
}
```

## Embedded Applicability: Medium

- It is a pure inline function with no additional heap allocation or system call overhead.
- It relies on move semantics; when using it with custom types, verify the actual cost of move construction/assignment.
- It is very concise for implementing move constructors and state machine transitions, making it suitable for resource-rich environments.
- Supported as `constexpr` since C++20, allowing for compile-time usage.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 5.0 | 3.4   | 19.0 |

## See Also

- [cppreference: std::exchange](https://en.cppreference.com/w/cpp/utility/exchange)

---

*Part of the content references [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
