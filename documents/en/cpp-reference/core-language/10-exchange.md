# std::exchange (C++14)

## In a Nutshell

Assigns a new value to a variable while retrieving its old value, eliminating the need for a manual temporary variable.

## Header

`#include <utility>`

## Core API Quick Reference

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Replace and return old value | `template<class T, class U = T> T exchange(T& obj, U&& new_value);` | Replaces `obj` with `new_value`, returns the old value of `obj` |

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

- It is a pure inline function with no extra heap allocation or system call overhead.
- It relies on move semantics; when used with custom types, we must verify the actual overhead of move construction or assignment.
- It is very concise for implementing move constructors and state machine transitions, making it suitable for resource-rich scenarios.
- Starting with C++20, it supports `constexpr` and can be used at compile time.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 5.0 | 3.4   | 19.0 |

## See Also

- [cppreference: std::exchange](https://en.cppreference.com/w/cpp/utility/exchange)

---

*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
