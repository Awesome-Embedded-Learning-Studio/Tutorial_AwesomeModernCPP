# Lambda Expressions (C++11)

## In a Nutshell

Lambda expressions allow us to define an anonymous function object inline, commonly used to pass short logic as an argument to algorithms or callbacks.

## Header

None (language feature)

## Core API Quick Reference

| Operation | Signature | Description |
|------|------|------|
| Non-capturing lambda | `[captures](params) { body }` | Basic syntax, generates a closure type |
| No-parameter lambda | `[captures] { body }` | Shorthand that omits the parameter list |
| Capture by value | `[x, y]` | Captures variables by copying |
| Capture by reference | `[&x, &y]` | Captures variables by reference |
| Capture all by value | `[=]` | Captures all used automatic variables by value |
| Capture all by reference | `[&]` | Captures all used automatic variables by reference |
| Mutable lambda | `[captures](params) mutable { body }` | Allows modifying the copies captured by value |
| Generic lambda | `[captures](auto a, auto b) { body }` | Uses auto for parameters, templated operator() |
| Explicit template parameters | `[captures]<typename T>(T a) { body }` | C++20, explicitly specifies a template parameter list |
| Static lambda | `[captures](params) static { body }` | C++23, operator() is a static member function |

## Minimal Example

```cpp
#include <algorithm>
#include <vector>
#include <iostream>
// Standard: C++11
int main() {
    std::vector<int> v = {3, 1, 4, 1, 5};
    int threshold = 3;
    auto count = std::count_if(v.begin(), v.end(),
        [threshold](int x) { return x > threshold; });
    std::cout << count << "\n"; // 输出: 2
}
```

## Embedded Applicability: High

- Closure types are generated at compile time with no heap allocation overhead and zero extra runtime cost
- Replaces function pointers and hand-written functors, making callback code more compact and readable
- Beware of lifetime risks with reference captures in asynchronous or interrupt scenarios; value capture is recommended for embedded callbacks
- C++14 generic lambdas allow us to write generic sorting or search comparison logic without template overhead

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.5 | 3.1   | 19.0 |

## See Also

- [cppreference: Lambda expressions](https://en.cppreference.com/w/cpp/language/lambda)

---
*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
