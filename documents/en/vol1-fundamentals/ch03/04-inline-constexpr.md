---
title: "inline and constexpr Functions"
description: "Understand what inline really means and how constexpr functions compute at compile time, laying the groundwork for zero-overhead abstraction in modern C++"
chapter: 3
order: 4
difficulty: beginner
reading_time_minutes: 19
platform: host
prerequisites:
  - "Overloading and Default Parameters"
tags:
  - cpp-modern
  - host
  - beginner
  - 入门
  - 基础
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/vol1-fundamentals/ch03/04-inline-constexpr.md
  source_hash: c72fc784f83a76340f619bd6aaff809278cd29106750a269039cad6e32ad12cc
  translated_at: '2026-09-25T10:24:00+00:00'
  engine: anthropic
  token_count: 8500
---

# inline and constexpr Functions: Don't Send a One-Liner on a Wasted Trip

We've written quite a few functions by now. Every time we call one, the program actually has a fair bit of work to do: save the current execution position, allocate a stack frame, jump to the function body, jump back when it's done, destroy the stack frame, and restore the caller's state. For a big function of dozens of lines, that overhead is nothing; but for a tiny function that just does `return x * x`, the call overhead can be larger than the function's own computation. Can we expand these one-or-two-line functions right at the call site and skip the whole cost of jumping and stack frames? That's exactly the problem `inline` and `constexpr` are here to solve.

## inline—The Most Misunderstood Keyword

### inline Is Not "Forced Inlining"

Many people read `inline` as "suggesting that the compiler expand this function at the call site." That understanding wasn't exactly wrong in the past, but in modern C++ it has drifted far from what `inline` is really for. The fact is: **the compiler is fully entitled to ignore the `inline` keyword we write**. Modern compilers have highly mature inlining heuristics and decide automatically whether to inline, based on factors such as function body size and call frequency. Conversely, even when we don't write `inline`, the compiler may perfectly well expand a short function inline.

So what does `inline` actually do? The answer has to do with the ODR—let's lay that rule out.

### What inline Really Means—ODR Exemption

C++ has one rule we must follow, the **ODR (One Definition Rule)**: a function may have only one definition in the entire program. If we write a function definition in a header file and that header gets included by two `.cpp` files, the linker sees two identical definitions and reports a redefinition error on the spot.

The `inline` keyword can break that rule. A function marked `inline` is allowed to have identical definitions across multiple translation units; **as long as all the definitions are exactly the same, the linker merges them automatically and keeps only one copy**. So the essence of `inline` as we see it is "this function may be defined in a header, and being included multiple times won't blow up"—a completely different thing from "please expand this function."

```cpp
// math_utils.h
#pragma once

// With inline: multiple .cpp files including this header won't cause redefinition
inline int square(int x)
{
    return x * x;
}
```

**The definition of an `inline` function must appear in a header file.** If we write only an `inline` declaration in the header and put the definition in a `.cpp`, other translation units can't find the function body when they call it, and the linker reports an undefined reference outright.

C++17 introduced `inline` variables. Just like `inline` functions, they let us define global variables in a header file without violating the ODR when it's included multiple times. Write `inline int mode_flags = 0;` in a header, include it from several `.cpp` files—in the end there is still only one definition. One boundary line is worth knowing here: namespace-scope `const` variables have internal linkage to begin with, so they don't violate the ODR even without `inline`; what genuinely needs `inline` variables are the global variables that change.

After C++17, `inline` as a keyword has been fading into the background. `constexpr` functions are `inline` by default, member functions defined inside the class body are `inline` by default, and function templates are `inline` by default too. The one scenario that still genuinely requires writing `inline` by hand is pretty much "defining a non-template, non-constexpr free function in a header file." But understanding what it really means remains an important step toward understanding C++'s compile-and-link model.

## constexpr Functions—Compute at Compile Time When We Can

If `inline` solves "allowing multiple definitions," then `constexpr` solves a more fundamental problem: **can we have a function finish computing at compile time and write the result straight into the binary?**

### The Basic Meaning of constexpr

`constexpr` is a keyword introduced in C++11 that declares a function or variable as "possibly evaluated at compile time." When a `constexpr` function is called and every argument is a constant known at compile time, the evaluation happens during compilation and the result becomes a constant directly. If any argument is a value that can only be determined at runtime, the function degrades into an ordinary runtime call—we don't need to write two versions of the code for the two scenarios. This dual-mode nature of "compute at compile time when possible, fall back to runtime otherwise" is what makes `constexpr` so practical.

```cpp
constexpr int square(int x)
{
    return x * x;
}

int main()
{
    constexpr int kResult = square(5);  // Evaluated at compile time, kResult = 25

    int x = 0;
    std::cin >> x;
    int runtime_result = square(x);    // Evaluated at runtime, degrades to an ordinary call

    return 0;
}
```

`constexpr` is not the same as `const`. `constexpr` means "compile-time constant" (the value is determined at compile time); `const` means "not modifiable at runtime" (the value is determined at runtime but cannot change). If you need a value fixed at compile time, reach for `constexpr`, not `const`.

### The Evolution of constexpr—Stronger with Every Generation

Every C++ standard has relaxed what a `constexpr` function may and may not do. In C++11, a `constexpr` function body could contain only a single `return` statement—no local variables, no loops, no `if/else`—so writing factorial meant recursion plus the ternary operator:

```cpp
// C++11: only return + the ternary operator allowed
constexpr int factorial(int n)
{
    return (n <= 1) ? 1 : n * factorial(n - 1);
}
```

C++14 loosened the restrictions dramatically: the function body could now have local variables, `if/else`, and `for/while` loops—the code finally looked normal. C++17 went further and allowed `constexpr` lambdas and `if constexpr`. C++20 lifted almost all remaining restrictions, even permitting `std::vector`, `std::string`, and dynamic memory allocation inside `constexpr` functions. By C++26, even throwing exceptions and catching them with `try/catch` during `constexpr` evaluation is allowed. The trend is unmistakable: **C++ is pushing for as much logic as possible to run at compile time**.

## Compile-Time Computation in Practice

### Compile-Time Fibonacci and static_assert

`static_assert` is a compile-time assertion: if the condition fails, compilation fails right there. We use it to verify the results of `constexpr` functions—it both ensures the logic is correct and forces the compiler to actually finish the computation at compile time.

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

Note, though, that the recursive Fibonacci has O(2^n) time complexity, and the compiler pays that same exponential price when executing it at compile time. In compile-time computation we should stick to iteration wherever possible to keep complexity under control.

### Using constexpr for Template Argument Computation

Non-type template arguments must be compile-time constants, and the return value of a `constexpr` function satisfies exactly that:

```cpp
constexpr int bytes_from_bits(int bits)
{
    return (bits + 7) / 8;  // convert a bit count into a byte count
}

// Template arguments need compile-time constants; constexpr functions fit perfectly
std::array<uint8_t, bytes_from_bits(32)> buffer{};
```

This pattern shows up constantly in embedded development: register widths, buffer sizes, DMA transfer lengths—values that can be pinned down at compile time. We compute them with `constexpr` functions and pass them straight to templates, getting type safety with zero runtime overhead.

### Compile-Time Lookup Tables

Embedded development often calls for precomputed lookup tables. The traditional approach is to write the array by hand and recompute it by hand whenever a parameter changes. With `constexpr`, we can have the compiler generate it for us:

```cpp
/// @brief Generate a CRC8 lookup table at compile time
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

// Generated at compile time, zero runtime overhead
constexpr auto kCrc8Table = make_crc8_table();
```

The entire lookup table is generated during compilation and embedded directly into the `.rodata` section of the binary. Accessing it at runtime is in no way different from accessing a hand-written `const` array.

## consteval and constinit—Stricter Control (C++20)

C++20 introduced two new keywords on top of `constexpr`; for now, we just need to know they exist.

A function declared `consteval` **must** be evaluated at compile time; call it with a runtime value and the compiler errors out immediately. That's completely different from `constexpr`'s "compile time when possible, runtime otherwise":

```cpp
consteval int power(int base, int exp)
{
    int result = 1;
    for (int i = 0; i < exp; ++i) { result *= base; }
    return result;
}

constexpr int kVal = power(2, 10);  // OK: evaluated at compile time, kVal = 1024
// int x; std::cin >> x;
// int y = power(x, 3);             // compile error: x is not a compile-time constant
```

`constinit` applies to variable declarations: it guarantees the variable is initialized at compile time without demanding that it be `const`. This defuses the "static initialization order trap"—global variables across different translation units are initialized in an unspecified order, which can lead to undefined behavior. `constinit` moves initialization to compile time, letting us sidestep the problem entirely.

## When to Use constexpr

One simple rule of thumb: **if a function is a pure function—same input always yields the same output, and no side effects—then it is a candidate for `constexpr`.** Math functions (square, absolute value, greatest common divisor), lookup table generation (sine tables, CRC tables), configuration value computation (register addresses, buffer sizes), type trait queries (`std::size()`, `std::extent_v`)—pure computations like these that don't depend on runtime state should all be handed to the compiler as much as possible: compute once at compile time, and zero times at runtime.

## Hands-On Practice—inline_constexpr.cpp

Let's pull everything from this chapter into one complete program that demonstrates how `constexpr` behaves at compile time versus at runtime.

```cpp
#include <array>
#include <cstdint>
#include <cstdio>

/// @brief Compile-time square computation
constexpr int square(int x)
{
    return x * x;
}

/// @brief Compile-time factorial (iterative, C++14 style)
constexpr int factorial(int n)
{
    int result = 1;
    for (int i = 2; i <= n; ++i) {
        result *= i;
    }
    return result;
}

/// @brief Compile-time integer power
constexpr int power(int base, int exp)
{
    int result = 1;
    for (int i = 0; i < exp; ++i) {
        result *= base;
    }
    return result;
}

/// @brief Generate a CRC8 lookup table at compile time
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

// Compile-time verification
static_assert(square(5) == 25, "square(5) should be 25");
static_assert(square(-3) == 9, "square(-3) should be 9");
static_assert(factorial(5) == 120, "5! should be 120");
static_assert(factorial(10) == 3628800, "10! should be 3628800");
static_assert(power(2, 10) == 1024, "2^10 should be 1024");

// Generate the lookup table at compile time
constexpr auto kCrc8Table = make_crc8_table();

/// @brief Compute CRC8 using the lookup table
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

`static_assert` has already verified the correctness of every computed result during compilation—if any function's implementation has a bug, the build simply won't pass. `kCrc8Table` is a 256-byte lookup table, generated entirely at compile time and embedded into the binary; accessing it at runtime costs no initialization whatsoever. `square(7)` produced the same result at compile time and at runtime—that's what `constexpr`'s "one piece of code, two modes" means.

If a `constexpr` function involves floating-point arithmetic, compile-time evaluation and runtime evaluation may differ slightly—floating-point precision isn't completely uniform across compilers and platforms. Integer arithmetic doesn't have this problem, but if we do use a floating-point algorithm in a `constexpr` function, it's best to pin down the expected result with a `static_assert`.

## Try It Yourself

### Exercise 1: constexpr Greatest Common Divisor

Write a `constexpr int gcd(int a, int b)` function that uses the Euclidean algorithm to compute the greatest common divisor of two positive integers. Verify with `static_assert` that `gcd(12, 8) == 4` and `gcd(100, 75) == 25`.

::: details Reference answer

```cpp
#include <iostream>

constexpr int gcd(int a, int b)
{
    return (b == 0) ? a : gcd(b, a % b);
}

static_assert(gcd(12, 8) == 4, "12和8的最大公约数应为4");
static_assert(gcd(100, 75) == 25, "100和75的最大公约数应为25");

int main()
{
    std::cout << "=== 编译期计算结果 ===" << std::endl;
    std::cout << "12和8的最大公约数: " << gcd(12, 8) << std::endl;
    std::cout << "100和75的最大公约数: " << gcd(100, 75) << std::endl;
    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra main.cpp -o main && ./main
```

Output:

```text
=== 编译期计算结果 ===
12和8的最大公约数: 4
100和75的最大公约数: 25
```

:::

### Exercise 2: A Compile-Time Fibonacci Lookup Table

Write a `constexpr` function that generates a `std::array<uint32_t, 30>` of 30 elements, where the i-th element is the i-th Fibonacci number. Verify with `static_assert` that `table[10] == 55` and `table[20] == 6765`. Use iteration rather than recursion, to avoid exponential compile times.

::: details Reference answer

```cpp
#include <array>
#include <cstdint>
#include <iostream>

constexpr std::array<std::uint32_t, 30> fibonacci()
{
    std::array<std::uint32_t, 30> fib{};
    fib[0] = 0;
    fib[1] = 1;
    for (std::size_t i = 2; i < 30; ++i)
    {
        fib[i] = fib[i - 1] + fib[i - 2];
    }
    return fib;
}

constexpr auto table = fibonacci();
static_assert(table[10] == 55, "Fibonacci(10) 应为 55");
static_assert(table[20] == 6765, "Fibonacci(20) 应为 6765");

int main()
{
    std::cout << "=== 编译期计算结果 ===" << std::endl;
    for (std::size_t i = 0; i < 30; ++i)
    {
        std::cout << "Fibonacci(" << i << ") = " << table[i] << std::endl;
    }
    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra main.cpp -o main && ./main
```

Output:

```text
=== 编译期计算结果 ===
Fibonacci(0) = 0
Fibonacci(1) = 1
Fibonacci(2) = 1
Fibonacci(3) = 2
Fibonacci(4) = 3
Fibonacci(5) = 5
Fibonacci(6) = 8
Fibonacci(7) = 13
Fibonacci(8) = 21
Fibonacci(9) = 34
Fibonacci(10) = 55
Fibonacci(11) = 89
Fibonacci(12) = 144
Fibonacci(13) = 233
Fibonacci(14) = 377
Fibonacci(15) = 610
Fibonacci(16) = 987
Fibonacci(17) = 1597
Fibonacci(18) = 2584
Fibonacci(19) = 4181
Fibonacci(20) = 6765
Fibonacci(21) = 10946
Fibonacci(22) = 17711
Fibonacci(23) = 28657
Fibonacci(24) = 46368
Fibonacci(25) = 75025
Fibonacci(26) = 121393
Fibonacci(27) = 196418
Fibonacci(28) = 317811
Fibonacci(29) = 514229
```

:::

### Exercise 3: constexpr popcount

Write a `constexpr int count_bits(int n)` function that returns how many 1s are in the binary representation of the integer `n`. Verify with `static_assert` that `count_bits(0) == 0`, `count_bits(7) == 3`, and `count_bits(255) == 8`. Hint: each `n &= (n - 1)` clears the lowest set 1 (the Brian Kernighan trick).

::: details Reference answer

```cpp
#include <iostream>

constexpr int count_bits(int n)
{
    unsigned int value = static_cast<unsigned int>(n);
    int count = 0;
    while (value != 0)
    {
        value &= (value - 1);
        ++count;
    }
    return count;
}

static_assert(count_bits(0) == 0, "0的二进制表示中1的个数应为0");
static_assert(count_bits(7) == 3, "7的二进制表示中1的个数应为3");
static_assert(count_bits(255) == 8, "255的二进制表示中1的个数应为8");

int main()
{
    std::cout << "=== 编译期计算结果 ===" << std::endl;
    std::cout << "0的二进制表示中1的个数: " << count_bits(0) << std::endl;
    std::cout << "7的二进制表示中1的个数: " << count_bits(7) << std::endl;
    std::cout << "255的二进制表示中1的个数: " << count_bits(255) << std::endl;
    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra main.cpp -o main && ./main
```

Output:

```text
=== 编译期计算结果 ===
0的二进制表示中1的个数: 0
7的二进制表示中1的个数: 3
255的二进制表示中1的个数: 8
```

:::
