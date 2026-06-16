---
chapter: 99
cpp_standard:
- 11
- 14
- 17
- 20
- 23
description: Define anonymous function objects in-place, capable of capturing variables
  from the surrounding scope.
difficulty: beginner
order: 2
reading_time_minutes: 2
tags:
- host
- cpp-modern
- beginner
title: Lambda expression
translation:
  source: documents/cpp-reference/core-language/02-lambda.md
  source_hash: 19d0fda53067e3da4f6f5ec5abcca449ddd5c414bfd2b71f9b1b278e14179780
  translated_at: '2026-06-16T03:28:42.732832+00:00'
  engine: anthropic
  token_count: 421
---
# Lambda Expressions (C++11)

## In a Nutshell

Lambdas allow us to define an anonymous function object directly in code. They are commonly used to pass short snippets of logic as arguments to algorithms or callbacks.

## Header

None (language feature)

## Core API Quick Reference

| Operation | Signature | Description |
|------|------|------|
| No-capture lambda | `[]() {}` | Basic syntax, generates a closure type |
| No-argument lambda | `[] {}` | Shorthand omitting the parameter list |
| Capture by value | `[x]` | Captures variables by copying their value |
| Capture by reference | `[&x]` | Captures variables by reference |
| Capture all by value | `[=]` | Captures all used automatic variables by value |
| Capture all by reference | `[&]` | Captures all used automatic variables by reference |
| Mutable lambda | `[x]() mutable` | Allows modification of the captured copy |
| Generic lambda | `[[](auto x)` | Parameters use `auto`, templated `operator()` |
| Explicit template parameters | `[]<typename T>(T x)` | C++20, explicitly specifies template parameter list |
| Static lambda | `[]() static` | C++23, `operator()` is a static member function |

## Minimal Example

```cpp
#include <algorithm>
#include <vector>
#include <iostream>

int main() {
    std::vector<int> v{3, 1, 4, 1, 5, 9};

    // Define a lambda to check if a number is even
    auto is_even = [](int n) { return n % 2 == 0; };

    // Use it with an algorithm
    auto count = std::count_if(v.begin(), v.end(), is_even);

    std::cout << "Even numbers: " << count << std::endl;
    return 0;
}
```

## Embedded Suitability: High

- Closure types are generated at compile time with no heap allocation overhead and zero additional runtime cost.
- Replaces function pointers and raw functors, making callback code more compact and readable.
- Be aware of lifetime risks with reference captures in asynchronous or interrupt contexts; value capture is recommended for embedded callbacks.
- C++14 generic lambdas allow us to write generic sorting/searching comparison logic without template overhead.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.5 | 3.1   | 19.0 |

## See Also

- [cppreference: Lambda expressions](https://en.cppreference.com/w/cpp/language/lambda)

---
*Part of the content references [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
