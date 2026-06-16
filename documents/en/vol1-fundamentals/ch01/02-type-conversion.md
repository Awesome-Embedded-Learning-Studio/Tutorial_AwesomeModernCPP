---
chapter: 1
cpp_standard:
- 11
- 14
- 17
- 20
description: Master C++ implicit and explicit conversion rules, learn how to use `static_cast`,
  and avoid classic type conversion pitfalls.
difficulty: beginner
order: 2
platform: host
prerequisites:
- 基本数据类型
reading_time_minutes: 13
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: Type Conversion
translation:
  source: documents/vol1-fundamentals/ch01/02-type-conversion.md
  source_hash: 4ad857473705bc6380f61724d29dc79eb7e51864548e7998610e657409d0aaae
  translated_at: '2026-06-16T03:41:09.817852+00:00'
  engine: anthropic
  token_count: 2293
---
# Type Conversion

After writing a few lines of C++, you will inevitably encounter this situation: a `float` needs to become an `int`, a `double` needs to be truncated to a `float`, or a signed number is being compared with an unsigned number. Type conversion is almost everywhere in real-world programs—and if you don't understand its rules, the compiler will quietly make decisions for you behind your back, leading you to discover a completely incomprehensible bug late one night.

In this chapter, we will thoroughly clarify the rules of type conversion: when the compiler helps you automatically, when you need to specify it explicitly, and how to avoid those classic precision traps.

> ⚠️ **Warning**: Bugs related to type conversion have a particularly nasty characteristic—in the most default cases, they often don't cause compilation errors or crash the program; instead, they silently produce incorrect calculation results. Therefore, I suggest we treat warnings as errors. My CFbox project enforces this on the pipeline to prevent strange corner cases from producing results we don't want.

## Implicit Conversion — The Compiler's Black Box Operation

Implicit conversion is when the compiler thinks "the types don't match here, but I know how to handle it," so it automatically performs the conversion for you without requiring any extra code. This sounds thoughtful, but if you don't know the rules, it's like an over-enthusiastic assistant trying to help but causing trouble instead.

### Integer Promotion and Arithmetic Conversion

C++ implicit conversion has a few core rules. The first is **integer promotion**: integer types smaller than `int` (`char`, `short`, etc.) are automatically promoted to `int` when participating in operations. For example, adding two `char`s results in an `int`, not a `char`—because on many CPUs, `int` is the native operation width and offers the highest efficiency.

The second is **arithmetic conversion**: when two values of different types are used in an operation together, the compiler "leans towards the larger type." When `int` and `float` are added, the `int` is first converted to `float`, and the result is `float`. Conversely, assigning a `float` to an `int` truncates the decimal part—it's not rounding; it's just chopped off.

Let's look at a comprehensive example to run through these types of implicit conversions:

```cpp
#include <iostream>

int main() {
    // 1. Integer promotion
    char a = 100;
    char b = 50;
    // char + char -> int + int -> int
    int result = a + b;
    std::cout << "100 + 50 = " << result << std::endl;

    // 2. Arithmetic conversion
    int i = 10;
    float f = 2.5f;
    // int + float -> float + float -> float
    float sum = i + f;
    std::cout << "10 + 2.5 = " << sum << std::endl;

    // 3. Assignment truncation
    double pi = 3.14159;
    int truncated = pi; // Decimal part discarded
    std::cout << "Truncated pi: " << truncated << std::endl;

    return 0;
}
```

## Classic Implicit Conversion Failures

Understanding the rules is one thing; actually getting tripped up by them is another. Let's look at two typical cases that appear frequently in real projects.

### The Collision of Signed and Unsigned

```cpp
#include <iostream>

int main() {
    if (-1 < 0u) {
        std::cout << "-1 is less than 0" << std::endl;
    } else {
        std::cout << "-1 is NOT less than 0" << std::endl;
    }
    return 0;
}
```

The binary representation of `-1` is all `1`s (in two's complement), so when interpreted as an unsigned integer, it becomes a huge number (specifically, `4294967295` on a 32-bit system). The compiler won't say a word to you. Even more terrifyingly, if you compare a signed number with an unsigned number, the compiler implicitly converts the signed number to unsigned for the comparison, and the result will leave you very confused.

> ⚠️ **Warning**: Comparing signed and unsigned numbers is a particularly high-frequency source of bugs. For example, if you use an `int` to compare with `std::vector::size()` (which returns `size_t`, an unsigned type), if the `int` is negative, it will be converted into a huge unsigned number, completely reversing the comparison result. Many compilers will warn about this when `-Wsign-compare` is enabled, so make sure to turn on these warning options.

### Overflow — The "Small Number" You Think You See Might Not Be Small

```cpp
#include <iostream>

int main() {
    unsigned char u = 255;
    // 255 + 1 -> 256 (int) -> truncated to 0 (unsigned char)
    u = u + 1;
    std::cout << "255 + 1 = " << static_cast<int>(u) << std::endl;
    return 0;
}
```

The maximum positive number an `unsigned char` can represent is `255`. Adding `1` causes an overflow. Although `u` is promoted to `int` during the calculation and the intermediate result `256` is within the `int` range, truncation occurs when assigning back to `unsigned char`, causing the result to wrap around to `0`.

## C-Style Casts — Available But Don't Use Them

In C language, there are two ways to write explicit type conversion: `(type)value` and `type(value)`. They are still legal in C++, but they are a "violent" means—the compiler will almost never refuse you, regardless of whether the conversion is reasonable. C++ provides four named cast operators, each with a specific purpose. Let's look at the one we use most often next.

## static_cast — The Main Tool for Daily Casting

`static_cast` is the cast operator we use most, with the syntax `static_cast<T>(value)`. It performs checks at compile time, can handle most "reasonable" conversions, and refuses obviously unreasonable operations.

```cpp
#include <iostream>

int main() {
    double d = 3.14;
    // Explicitly convert double to int
    int i = static_cast<int>(d);
    std::cout << "Double: " << d << ", Int: " << i << std::endl;

    // void* to int* (safe)
    int x = 42;
    void* vptr = &x;
    int* iptr = static_cast<int*>(vptr);
    std::cout << "Recovered value: " << *iptr << std::endl;

    return 0;
}
```

You might ask: What's the difference from direct assignment? The difference lies in **clear intent**. `static_cast` loudly tells anyone reading the code "a type conversion is definitely needed here, and I know what I am doing," whereas implicit conversion happens quietly. Another important distinction is that `static_cast` performs compile-time checks—if you try to convert a `SomeClass*` to an `UnrelatedClass*`, `static_cast` will directly refuse with an error because there is no reasonable conversion path between these two pointer types.

## reinterpret_cast — Reinterpreting the Underlying Bit Pattern

Among the things `static_cast` can't do, a large category involves "using a piece of memory as another type." For example, if you get a `void*` pointer, you need to convert it back to `int*` to dereference it; or you need to view the underlying bit pattern of a `float` as an `int`. These operations go beyond the safety guarantees of the type system, and the compiler cannot help you check their validity—this is where `reinterpret_cast` comes in.

```cpp
#include <iostream>
#include <cstdint>

int main() {
    int i = 0x461E4E00; // Bit pattern of 10000.0 in IEEE 754 (approx)
    // Treat the int's bits as a float
    float f = *reinterpret_cast<float*>(&i);
    std::cout << "Float value from int bits: " << f << std::endl;

    return 0;
}
```

The name of `reinterpret_cast` says it all—"reinterpret." It does not change the underlying binary data; it just tells the compiler, "Please treat this memory as another type." Because of this, it is also the most dangerous cast operator; using it incorrectly leads directly to undefined behavior.

> ⚠️ **Warning**: Many uses of `reinterpret_cast` are undefined behavior or implementation-defined behavior. For example, converting a `double*` to an `int*` and then dereferencing it is completely unpredictable due to differences in alignment requirements and size. Its truly safe use cases are actually quite rare: conversion between `void*` and raw pointer types, low-level byte observation based on `std::byte`, and some serialization and hardware register access scenarios. We will encounter it more frequently in embedded development, but it is basically unused in host-side application code. A simple rule of thumb: **95% of explicit casts in daily development can be handled by `static_cast`**. If you find yourself wanting to use `reinterpret_cast`, stop and think first about whether there is a design problem.

## const_cast and dynamic_cast (Brief Introduction)

`const_cast` is used to remove or add `const` qualification—if the original object is actually `const`, forcibly removing `const` to write to it is undefined behavior. `dynamic_cast` is used for safe downcasting in inheritance hierarchies and checks the actual type of the object at runtime; we will discuss it in detail after we cover object-oriented programming.

## Numerical Precision — Those Moments That Make You Doubt Life

Another major topic brought up by type conversion is numerical precision. Here we look at three classic scenarios.

### The Trap of Integer Division

```cpp
#include <iostream>

int main() {
    int count = 3;
    int total = 10;
    // int / int -> int (truncated)
    double ratio = count / total;
    std::cout << "Ratio: " << ratio << std::endl; // Output: 0
    return 0;
}
```

Both operands of `count / total` are `int`, so integer division is performed, and the result is also `int`. Although the variable on the left is `double`, that just converts the result `0` to `0.0`. Assignment happens after the operation—to get a floating-point result, you must convert at least one operand to a floating-point type *before* the division.

> ⚠️ **Warning**: Integer division truncation is one of the most common mistakes for beginners, especially when calculating averages or percentages. Remember: as long as both sides of the division sign are integers, the result will be an integer. To get a floating-point result, convert either the numerator or the denominator to `double` or `float`.

### The Unreliability of Floating-Point Comparison

```cpp
#include <iostream>

int main() {
    double a = 0.1 + 0.2;
    if (a == 0.3) {
        std::cout << "Equal" << std::endl;
    } else {
        std::cout << "Not Equal" << std::endl;
    }
    return 0;
}
```

`0.1 + 0.2` does not equal `0.3`—because `0.1` and `0.2` cannot be represented precisely in binary floating-point, `0.3` can only be stored as an approximation. The correct approach is to determine whether the difference between two floating-point numbers is less than a sufficiently small threshold (epsilon).

### Integer Overflow — The Consequences of Exceeding the Range

```cpp
#include <iostream>
#include <limits>

int main() {
    int max = std::numeric_limits<int>::max();
    // Signed overflow is undefined behavior!
    int overflow = max + 1;
    std::cout << "Max + 1: " << overflow << std::endl;
    return 0;
}
```

Signed integer overflow is **undefined behavior** in C++—the compiler can do anything with such code. Although most implementations will wrap around to a negative number, you cannot rely on this behavior. Overflow of unsigned integers is a well-defined wrap-around behavior, which is sometimes used intentionally in embedded development (e.g., ring buffers), but it must be conscious.

## Comprehensive Example — conversion.cpp

Now let's integrate the previous knowledge into a complete program, covering implicit conversion, `static_cast`, integer division, floating-point comparison, and overflow. I suggest you read the code yourself first and predict the output of each line, then look at the running results.

```cpp
#include <iostream>
#include <limits>
#include <cmath>

// Helper function to check float equality
bool approx_equal(double a, double b, double epsilon = 1e-9) {
    return std::abs(a - b) < epsilon;
}

int main() {
    // 1. Implicit conversion and arithmetic
    std::cout << "=== Arithmetic & Promotion ===" << std::endl;
    char c1 = 100, c2 = 28;
    std::cout << "100 + 28 = " << c1 + c2 << " (type: int)" << std::endl;

    // 2. static_cast usage
    std::cout << "\n=== static_cast ===" << std::endl;
    double pi = 3.14159;
    std::cout << "static_cast<int>(" << pi << ") = " << static_cast<int>(pi) << std::endl;

    // 3. Signed vs Unsigned comparison
    std::cout << "\n=== Signed vs Unsigned ===" << std::endl;
    int x = -1;
    unsigned int y = 10;
    std::cout << "-1 < 10u ? " << (x < y ? "true" : "false") << std::endl;

    // 4. Integer division trap
    std::cout << "\n=== Integer Division ===" << std::endl;
    std::cout << "1 / 2 = " << 1 / 2 << std::endl;
    std::cout << "1.0 / 2 = " << 1.0 / 2 << std::endl;

    // 5. Float comparison
    std::cout << "\n=== Float Comparison ===" << std::endl;
    double val = 0.1 + 0.2;
    std::cout << "0.1 + 0.2 == 0.3 ? " << (val == 0.3 ? "true" : "false") << std::endl;
    std::cout << "approx_equal(0.1 + 0.2, 0.3) ? " << (approx_equal(val, 0.3) ? "true" : "false") << std::endl;

    // 6. Overflow
    std::cout << "\n=== Overflow ===" << std::endl;
    unsigned char u = 255;
    std::cout << "255 + 1 = " << static_cast<int>(u + 1) << " (int)" << std::endl;
    std::cout << "255 + 1 = " << static_cast<int>(u = u + 1) << " (unsigned char)" << std::endl;

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++20 -Wall -Wextra -o conversion conversion.cpp
./conversion
```

Output:

```text
=== Arithmetic & Promotion ===
100 + 28 = 128 (type: int)

=== static_cast ===
static_cast(3.14159) = 3

=== Signed vs Unsigned ===
-1 < 10u ? false

=== Integer Division ===
1 / 2 = 0
1.0 / 2 = 0.5

=== Float Comparison ===
0.1 + 0.2 == 0.3 ? false
approx_equal(0.1 + 0.2, 0.3) ? true

=== Overflow ===
255 + 1 = 256 (int)
255 + 1 = 0 (unsigned char)
```

Looking at it line by line, every output corresponds to a rule discussed earlier. Pay special attention to the comparison between line 3 and line 2—with the same `255 + 1`, the presence or absence of assignment back to `unsigned char` makes a completely different result.

## Run Online

Run the comprehensive example below online. First, predict the output of each line in your mind, then compare it with the actual result:

<OnlineCompilerDemo
  title="Type Conversion Comprehensive Demo"
  source-path="code/examples/vol1/03_type_conversion.cpp"
  description="Observe the actual behavior of implicit conversion, static_cast, integer division traps, floating-point precision, and overflow."
  allow-run
/>

## Try It Yourself

The theory is over. Now it's your turn. The following exercises are progressive; I suggest you write, compile, and run each one.

### Exercise 1: Predict the Output

Without compiling or running, write down the output of the following code on paper, then verify with a compiler:

```cpp
#include <iostream>

int main() {
    int a = -10;
    unsigned int b = 5;

    std::cout << (a < b) << std::endl; // Line 1
    std::cout << (a < 5) << std::endl; // Line 2
    std::cout << (-10 < 5u) << std::endl; // Line 3
    return 0;
}
```

The actual output of the third line is `0`—yes, intuitively `-10 < 5` should hold, but when mixing signed and unsigned comparisons, `-10` is implicitly converted to an unsigned number (becoming a huge value), so the actual comparison is `huge < 5`, which naturally results in `false`. If you predicted `0` correctly, congratulations, you have understood this trap; if you predicted `1`, go back to the section "The Collision of Signed and Unsigned."

### Exercise 2: Fix the Temperature Converter

The following code intends to convert Celsius to Fahrenheit, but the result is sometimes wrong. Find the problem and fix it:

```cpp
#include <iostream>

int main() {
    int celsius = 25;
    // Bug: Integer division happens here
    int fahrenheit = celsius * 9 / 5 + 32;
    std::cout << "Celsius: " << celsius << ", Fahrenheit: " << fahrenheit << std::endl;
    return 0;
}
```

Hint: Try changing `celsius` to `26`, and see if `fahrenheit` gets `78` or `78.6`.

### Exercise 3: Write a Safe Temperature Converter

Write a complete temperature conversion program that reads a Celsius temperature (supporting decimals) from user input, correctly converts it to Fahrenheit, and outputs it. Requirements: use correct types and `static_cast`, and keep one decimal place in the output. Expected effect:

```text
Enter Celsius: 26.5
Fahrenheit: 79.7
```

## Summary

In this chapter, we went through C++'s type conversion mechanism. Implicit conversion operates silently behind the compiler's curtain, covering integer promotion, arithmetic conversion, assignment conversion, and boolean conversion—when you don't understand the rules, it is an invisible source of bugs. `static_cast` is the main force for daily casting, safer and more explicit in intent than C-style casts. Regarding numerical precision, integer division truncation, the inability to directly compare floating-point numbers, and integer overflow are all high-frequency traps.

Remember a few core principles: when both sides of integer division are integers, the result must be an integer; never use `==` to compare floating-point numbers; use the difference and epsilon to judge approximate equality; be extra careful when mixing signed and unsigned operations, and turn on compiler warnings. In the next chapter, we learn the basic usage of `const`—how to let the compiler help us guard the bottom line of "values that shouldn't change."
