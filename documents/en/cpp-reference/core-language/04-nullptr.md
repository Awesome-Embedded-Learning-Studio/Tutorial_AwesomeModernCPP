# nullptr (C++11)

## In a Nutshell

A null pointer literal of type `std::nullptr_t` that safely distinguishes integer overloads, completely resolving the ambiguity caused by the macro `NULL` and the integer `0` in templates and function overloading.

## Header

No header required (language keyword); the type is defined in `<cstddef>`.

## Quick API Reference

| Operation | Signature | Description |
|------|------|------|
| Null pointer literal | `nullptr` | A prvalue of type `std::nullptr_t` |
| Implicit conversion | → Any pointer type | Converts to a null pointer value of the corresponding type |
| Implicit conversion | → Any pointer-to-member type | Converts to a null pointer-to-member value of the corresponding type |

## Minimal Example

```cpp
#include <iostream>
void f(int) { std::cout << "int\n"; }
void f(int*) { std::cout << "int*\n"; }

int main() {
    f(0);        // 调用 f(int)，可能非预期
    f(nullptr);  // 调用 f(int*)，精确匹配
    int* p = nullptr;
    if (p == nullptr) { std::cout << "null\n"; }
}
```

## Embedded Applicability: High

- A zero-overhead abstraction; the compiler directly generates a null pointer value at compile time, producing the same instructions as `0` or `NULL`
- Avoids overload ambiguity between integers and pointers in register manipulation functions (such as overloads for hardware registers)
- Behaves correctly in template metaprogramming (such as static assertions and type traits), whereas `NULL` and `0` would fail
- Fully compatible with C-style low-level hardware manipulation code, allowing for a risk-free, gradual replacement

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.6 | 3.0 | 2010 |

## See Also

- [cppreference: nullptr](https://en.cppreference.com/w/cpp/language/nullptr)

---

*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
