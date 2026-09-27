---
chapter: 99
cpp_standard:
- 11
- 14
- 17
- 20
- 23
description: Define an anonymous function object in-place, able to capture variables
  from the enclosing scope
difficulty: beginner
order: 2
reading_time_minutes: 2
tags:
- host
- cpp-modern
- beginner
title: Lambda Expressions
translation:
  source: documents/cpp-reference/core-language/02-lambda.md
  source_hash: 19d0fda53067e3da4f6f5ec5abcca449ddd5c414bfd2b71f9b1b278e14179780
  translated_at: '2026-09-27T01:29:02+00:00'
  engine: anthropic
  token_count: 850
---
# Lambda Expressions (C++11)

## In a Nutshell

A lambda lets us define an anonymous function object in-place right in our code, and is commonly used to pass short snippets of logic as arguments to algorithms or callbacks.

## Header

None (language feature)

## Core API Quick Reference

| Operation | Signature | Description |
|------|------|------|
| Basic form | `[captures](params) { body }` | Basic syntax, generates a closure type |
| No-parameter lambda | `[captures] { body }` | Shorthand that omits the parameter list |
| Capture by value | `[x, y]` | Captures variables by copying their values |
| Capture by reference | `[&x, &y]` | Captures variables by reference |
| Capture all by value | `[=]` | Captures all used automatic variables by value |
| Capture all by reference | `[&]` | Captures all used automatic variables by reference |
| Mutable lambda | `[captures](params) mutable { body }` | Allows modifying the copies captured by value |
| Generic lambda | `[captures](auto a, auto b) { body }` | Parameters use `auto`, templated `operator()` |
| Explicit template parameters | `[captures]<typename T>(T a) { body }` | C++20, explicitly specifies a template parameter list |
| Static lambda | `[captures](params) static { body }` | C++23, `operator()` is a static member function |

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
    std::cout << count << "\n"; // Output: 2
}
```

## Embedded Suitability: High

- The closure type is generated at compile time, with no heap allocation overhead and zero additional runtime cost
- Replaces function pointers and hand-written functors, making callback code more compact and more readable
- Watch out for the lifetime risks of reference captures in asynchronous or interrupt contexts; value capture is recommended for embedded callbacks
- C++14 generic lambdas let us write generic sorting/searching comparison logic without template overhead

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.5 | 3.1   | 19.0 |

## See Also

- [cppreference: Lambda expressions](https://en.cppreference.com/w/cpp/language/lambda)

---
*Some content references [cppreference.com](https://en.cppreference.com/), used under the [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) license*
