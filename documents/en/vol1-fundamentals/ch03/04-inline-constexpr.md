---
title: inline and constexpr functions
description: Understanding the true meaning of inline and the compile-time evaluation
  capability of constexpr functions lays the foundation for zero-overhead abstraction
  in modern C++.
chapter: 3
order: 4
difficulty: beginner
reading_time_minutes: 12
platform: host
prerequisites:
- 重载与默认参数
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
cpp_standard:
- 11
- 14
- 17
- 20
---
# `inline` and `constexpr` Functions

We have written quite a few functions by now. Every time we call a function, the program actually has a lot of work to do—saving the current execution position, allocating a stack frame, jumping to the function body, jumping back after execution, destroying the stack frame, and restoring the scene. For a large function of dozens of lines, this overhead is negligible, but for a small function that only does `return x * x`, the call overhead might exceed the computation of the function itself. Can we directly expand these "paper-thin" function calls, eliminating all the costs of jumping and stack frames? This is exactly the problem that `inline` and `constexpr` aim to solve.

## `inline`—The Most Misunderstood Keyword

### `inline` Does Not Mean "Force Inline"

Many people understand `inline` as "suggesting that the compiler expand this function at the call site." This understanding wasn't entirely wrong in the past, but in modern C++, it has severely deviated from the true purpose of `inline`. The fact is: **the compiler has full authority to ignore the `inline` keyword you wrote**. Modern compilers have very mature inline heuristic algorithms that automatically decide whether to inline based on factors like function body size and call frequency. Conversely, even if you don't write `inline`, the compiler might fully inline a short function.

So what does `inline` actually do? The answer is related to the ODR.

### The True Meaning of `inline`—ODR Exemption

C++ has an ironclad rule called the **ODR (One Definition Rule)**: a function can only have one definition throughout the entire program. If you write the function definition in a header file, and that header file is included by two `.cpp` files, the linker will see two identical definitions and directly report a redefinition error.

The `inline` keyword can break this rule. Functions marked as `inline` are allowed to have identical definitions in multiple translation units, as long as all definitions are exactly the same—the linker will automatically merge them and keep only one copy. So the essence of `inline` is not "please expand this function," but rather "allow this function to be defined in a header file without blowing up when included multiple times."

```cpp
// math_utils.h
#pragma once

// 有了 inline：多个 .cpp include 此头文件不会重定义
inline int square(int x)
{
    return x * x;
}
```

> **Pitfall Warning**: The definition of an `inline` function must appear in a header file. If you only write an `inline` declaration in the header file and put the definition in a `.cpp` file, other translation units won't find the function body when calling it, and the linker will either report an error or fall back to a normal function call. **The definition must appear in the header file together with the declaration.**
>
> C++17 introduced `inline` variables. Just like `inline` functions, they allow defining global variables in header files without violating the ODR when included multiple times. For example, `inline static const int kMaxSize = 100;` can be written like this in a header file.

After C++17, the presence of `inline` as a keyword has grown increasingly weak. `constexpr` functions are `inline` by default, member functions defined within a class body are also `inline` by default, and template functions are similarly `inline` by default. The only scenario that truly requires manually writing `inline` is almost exclusively "defining a non-template, non-constexpr free function in a header file." However, understanding its true meaning remains an important step in understanding the C++ compilation and linking model.

## `constexpr` Functions—Evaluate at Compile Time When Possible

If `inline` solves the problem of "allowing multiple definitions," then `constexpr` solves a more fundamental question: **can we let a function finish its calculation at compile time, directly "welding" the result into the binary file?**

### The Basic Meaning of `constexpr`

`constexpr` is a keyword introduced in C++11, used to declare functions or variables that "can potentially be evaluated at compile time." When a `constexpr` function is called, if all arguments are compile-time known constants, the function's evaluation occurs during the compilation phase, and the result directly becomes a constant. If the arguments contain values that can only be determined at runtime, the function degrades into a normal runtime call. This dual-mode characteristic of "evaluate at compile time if possible, otherwise evaluate at runtime" is the most powerful aspect of `constexpr`.

```cpp
constexpr int square(int x)
{
    return x * x;
}

int main()
{
    constexpr int kResult = square(5);  // 编译期求值，kResult = 25

    int x = 0;
    std::cin >> x;
    int runtime_result = square(x);    // 运行时求值，退化为普通调用

    return 0;
}
```

> **Pitfall Warning**: `constexpr` does not equal `const`. `constexpr` means "compile-time constant" (the value is determined at compile time), while `const` means "immutable at runtime" (the value is determined at runtime but cannot be changed). If you need a compile-time determined value, use `constexpr` instead of `const`.

### The Evolution of `constexpr`—Each Generation Stronger Than the Last

What a `constexpr` function can and cannot do has seen restrictions relaxed with every C++ standard. In C++11, a `constexpr` function body could only contain a single `return` statement, with no local variables, loops, or `if/else`. Writing a factorial function relied solely on recursion combined with the ternary operator:

```cpp
// C++11：只能用 return + 三元运算符
constexpr int factorial(int n)
{
    return (n <= 1) ? 1 : n * factorial(n - 1);
}
```

C++14 significantly relaxed these restrictions—function bodies could have local variables, `if/else`, and `for/while` loops, making the syntax finally normal. C++17 further allowed `constexpr` lambdas and `if constexpr`, and C++20 lifted almost all restrictions—even allowing the use of `std::vector`, `std::string`, and dynamic memory allocation inside `constexpr` functions. By C++23, exceptions can even be thrown and caught with `try/catch` inside `constexpr`. This trend is very clear: **C++ is enabling as much logic as possible to execute at compile time**.

## Practical Examples of Compile-Time Calculation

### Compile-Time Fibonacci and `static_assert`

`static_assert` is a compile-time assertion—if the condition is not met, compilation fails directly. Using it to verify the results of a `constexpr` function both ensures logical correctness and forces the compiler to actually complete the calculation at compile time.

```cpp
constexpr int fib(int n)
{
    if (n <= 1) { return n; }
    return fib(n - 1) + fib(n - 2);
}

static_assert(fib(0) == 0);
static_assert(fib(1) == 1);
static_assert(fib(10) == 55);
```

However, the time complexity of the recursive Fibonacci is O(2^n), and the compiler must bear this exponential cost when executing it at compile time. In compile-time calculations, we should try to use iteration to control complexity.

### Using `constexpr` for Template Parameter Calculation

Non-type parameters of a template must be compile-time constants, and the return value of a `constexpr` function perfectly satisfies this condition:

```cpp
constexpr int bytes_from_bits(int bits)
{
    return (bits + 7) / 8;  // bit 数换算成 byte 数
}

// 模板参数需要编译期常量，constexpr 函数完美适配
std::array<uint8_t, bytes_from_bits(32)> buffer{};
```

This usage is especially common in embedded development—values that can be determined at compile time, such as register widths, buffer sizes, and DMA transfer lengths, can be calculated using `constexpr` functions and directly fed to templates, ensuring type safety without any runtime overhead.

### Compile-Time Lookup Tables

Precomputed lookup tables are frequently needed in embedded development. The traditional approach is to hand-write an array, and when parameters change, you have to manually recalculate. With `constexpr`, you can let the compiler generate it for you:

```cpp
/// @brief 编译期生成 CRC8 查找表
constexpr std::array<uint8_t, 256> make_crc8_table()
{
    std::array<uint8_t, 256> table{};
    constexpr uint8_t kPoly = 0x07;
    for (int i = 0; i < 256; ++i) {
        uint8_t byte = static_cast<uint8_t>(i);
        for (int bit = 0; bit < 8; ++bit) {
            byte = (byte & 0x80) ? (byte << 1) ^ kPoly : (byte << 1);
        }
        table[i] = byte;
    }
    return table;
}

// 编译期生成，运行时零开销
constexpr auto kCrc8Table = make_crc8_table();
```

The entire lookup table is fully generated during the compilation phase and directly embedded into the `.rodata` section of the binary file. Accessing it at runtime is no different from accessing a hand-written `const` array.

## `consteval` and `constinit`—Stricter Control (C++20)

Building upon `constexpr`, C++20 introduced two new keywords. For now, we just need to know they exist.

Functions declared with `consteval` **must** be evaluated at compile time—if you call them with a runtime value, the compiler will directly report an error. This is completely different from `constexpr`'s "compile if possible, otherwise runtime" approach:

```cpp
consteval int power(int base, int exp)
{
    int result = 1;
    for (int i = 0; i < exp; ++i) { result *= base; }
    return result;
}

constexpr int kVal = power(2, 10);  // OK：编译期求值，kVal = 1024
// int x; std::cin >> x;
// int y = power(x, 3);             // 编译错误：x 不是编译期常量
```

`constinit` applies to variable declarations, guaranteeing that the variable completes its initialization at compile time without requiring it to become a `const`. This solves the "static initialization order fiasco"—the initialization order of global variables across different translation units is undefined, which can lead to undefined behavior. `constinit` ensures initialization happens at compile time, completely bypassing this issue.

## When to Use `constexpr`

A simple rule of thumb: **if a function is a pure function—given the same input it always returns the same output, and it has no side effects—then it is a candidate for `constexpr`.** Mathematical functions (square, absolute value, greatest common divisor), lookup table generation (sine tables, CRC tables), configuration value calculations (register addresses, buffer sizes), type trait checks (`std::size()`, `std::extent_v`)—these pure computations that do not rely on runtime state should all be handed over to the compiler as much as possible. The compiler has more patience than the CPU, and it only calculates once.

## Hands-On Practice—inline_constexpr.cpp

Now let's integrate the content of this chapter into a complete program, demonstrating the different behaviors of `constexpr` at compile time and runtime.

```cpp
#include <array>
#include <cstdint>
#include <cstdio>

/// @brief 编译期平方计算
constexpr int square(int x)
{
    return x * x;
}

/// @brief 编译期阶乘（迭代版，C++14 风格）
constexpr int factorial(int n)
{
    int result = 1;
    for (int i = 2; i <= n; ++i) {
        result *= i;
    }
    return result;
}

/// @brief 编译期整数幂
constexpr int power(int base, int exp)
{
    int result = 1;
    for (int i = 0; i < exp; ++i) {
        result *= base;
    }
    return result;
}

/// @brief 编译期生成 CRC8 查找表
constexpr std::array<uint8_t, 256> make_crc8_table()
{
    std::array<uint8_t, 256> table{};
    constexpr uint8_t kPoly = 0x07;
    for (int i = 0; i < 256; ++i) {
        uint8_t byte = static_cast<uint8_t>(i);
        for (int bit = 0; bit < 8; ++bit) {
            byte = (byte & 0x80) ? (byte << 1) ^ kPoly : (byte << 1);
        }
        table[i] = byte;
    }
    return table;
}

// 编译期验证
static_assert(square(5) == 25, "square(5) should be 25");
static_assert(square(-3) == 9, "square(-3) should be 9");
static_assert(factorial(5) == 120, "5! should be 120");
static_assert(factorial(10) == 3628800, "10! should be 3628800");
static_assert(power(2, 10) == 1024, "2^10 should be 1024");

// 编译期生成查找表
constexpr auto kCrc8Table = make_crc8_table();

/// @brief 用查找表计算 CRC8
uint8_t compute_crc8(const uint8_t* data, size_t length)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < length; ++i) {
        crc = kCrc8Table[crc ^ data[i]];
    }
    return crc;
}

int main()
{
    printf("=== 编译期计算结果 ===\n");
    printf("square(5)     = %d\n", square(5));
    printf("factorial(10) = %d\n", factorial(10));
    printf("power(2, 16)  = %d\n", power(2, 16));
    printf("CRC8 table[0] = 0x%02X\n", kCrc8Table[0]);
    printf("CRC8 table[1] = 0x%02X\n", kCrc8Table[1]);

    printf("\n=== 运行时 CRC8 计算 ===\n");
    uint8_t test_data[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    uint8_t crc = compute_crc8(test_data, sizeof(test_data));
    printf("CRC8 of {01 02 03 04 05} = 0x%02X\n", crc);

    printf("\n=== 编译期 vs 运行时 ===\n");
    constexpr int kCompileTime = square(7);
    int runtime_input = 7;
    int runtime_result = square(runtime_input);
    printf("constexpr square(7) = %d\n", kCompileTime);
    printf("runtime  square(7)  = %d\n", runtime_result);
    printf("结果一致: %s\n",
           kCompileTime == runtime_result ? "是" : "否");

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra -O2 -o inline_constexpr inline_constexpr.cpp
./inline_constexpr
```

Expected output:

```text
=== 编译期计算结果 ===
square(5)     = 25
factorial(10) = 3628800
power(2, 16)  = 65536
CRC8 table[0] = 0x00
CRC8 table[1] = 0x07

=== 运行时 CRC8 计算 ===
CRC8 of {01 02 03 04 05} = 0xBC

=== 编译期 vs 运行时 ===
constexpr square(7) = 49
runtime  square(7)  = 49
结果一致: 是
```

`static_assert` has already verified the correctness of all calculation results during the compilation phase—if there is a bug in a function's implementation, the compilation simply won't pass. `kCrc8Table` is a 256-byte lookup table, completely generated at compile time and embedded into the binary file, with no initialization overhead when accessed at runtime. `square(7)` produces the same results at compile time and runtime, which is the essence of `constexpr`'s "one code, two modes" philosophy.

> **Pitfall Warning**: If a `constexpr` function contains floating-point operations, there may be tiny differences between the results of compile-time evaluation and runtime evaluation—floating-point precision is not entirely consistent across different compilers and platforms. This is not an issue for integer operations, but if you use floating-point algorithms in a `constexpr` function, it's best to lock in the expected result using `static_assert`.

## Try It Yourself

### Exercise 1: `constexpr` Greatest Common Divisor

Write a `constexpr int gcd(int a, int b)` function that uses the Euclidean algorithm to calculate the greatest common divisor of two positive integers. Use `static_assert` to verify `gcd(12, 8) == 4` and `gcd(100, 75) == 25`.

### Exercise 2: Compile-Time Fibonacci Lookup Table

Write a `constexpr` function to generate a `std::array<uint32_t, 30>` containing 30 elements, where the i-th element is the i-th Fibonacci number. Use `static_assert` to verify `table[10] == 55` and `table[20] == 6765`. Note: use iteration instead of recursion to avoid exponential compilation time.

### Exercise 3: `constexpr` popcount

Write a `constexpr int count_bits(int n)` function that returns the number of 1s in the binary representation of an integer `n`. Use `static_assert` to verify `count_bits(0) == 0`, `count_bits(7) == 3`, and `count_bits(255) == 8`. Hint: each `n &= (n - 1)` eliminates the lowest set bit (Brian Kernighan's algorithm).

## Summary

In this chapter, we dissected two keywords closely related to function execution methods. The true meaning of `inline` is not "force inline," but rather an ODR exemption—allowing the same function definition to appear in multiple translation units. `constexpr` is the cornerstone of compile-time calculation in modern C++—functions marked as `constexpr` automatically evaluate at compile time when all arguments are compile-time constants, otherwise degrading into normal runtime calls. C++14 relaxed function body restrictions, C++20 introduced `consteval` and `constinit`, and the overall trend is to move as much computation as possible to compile time.

The core benefit of mastering `constexpr` lies in: **shifting runtime workload to the compiler**. Lookup table generation, configuration parameter calculation, type trait checks—these pure computations that do not rely on runtime state should all be handed over to the compiler as much as possible.

In the next chapter, we will learn about pointers and references—things that drive countless beginners crazy in C++, yet keep veterans coming back for more. Don't be nervous; with the foundation laid so far, the picture of pointers and references will be much clearer than you might imagine.
