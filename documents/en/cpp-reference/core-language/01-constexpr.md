# constexpr (C++11)

## In a Nutshell

Tells the compiler "this value or function can be evaluated at compile time," shifting runtime computations to the compile phase and achieving zero-overhead complex logic.

## Header

None (language keyword)

## Core API Quick Reference

| Operation | Signature | Description |
|------|------|------|
| Compile-time variable | `constexpr T var = expr;` | Requires `expr` to be a constant expression; the variable is implicitly `const` |
| Compile-time function | `constexpr T func(params);` | If arguments are constants, it evaluates at compile time; otherwise, it degrades to a normal function |
| Compile-time construction | `constexpr T::T(params);` | Allows constructing literal type objects in constant expressions |
| Compile-time destruction | `constexpr T::~T();` | (C++20) Allows destroying objects in constant expressions |
| Feature test macro | `__cpp_constexpr` | Detects the current compiler's level of constexpr support |

## Minimal Example

```cpp
// Standard: C++14
#include <iostream>

constexpr int factorial(int n) {
    int res = 1;
    while (n > 1) res *= n--;
    return res;
}

int main() {
    constexpr int val = factorial(5); // 编译期计算
    std::cout << val << '\n';         // 输出: 120
    int k = 4;
    std::cout << factorial(k) << '\n';// 运行期计算: 24
}
```

## Embedded Applicability: High

- Moves computations like table lookups, CRC checks, and protocol parsing to compile time, saving Flash/RAM space
- Compile-time computed values can be used directly as template parameters (e.g., array sizes), meeting the static configuration needs of bare-metal environments
- Offers better readability and debugging experience compared to C macros and template metaprogramming
- Note that C++11 has many restrictions (single return statement); we recommend embedded projects use at least the C++14 standard

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.6 | 3.1 | 19.0 |

## See Also

- [cppreference: constexpr specifier](https://en.cppreference.com/w/cpp/language/constexpr)

---
*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
