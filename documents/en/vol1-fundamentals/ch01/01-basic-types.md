---
chapter: 1
cpp_standard:
- 11
- 14
- 17
- 20
description: Master C++ integer, floating-point, character, and boolean types, and
  understand type sizes, value ranges, and platform differences.
difficulty: beginner
order: 1
platform: host
prerequisites:
- 第一个 C++ 程序
reading_time_minutes: 17
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: Basic Data Types
translation:
  source: documents/vol1-fundamentals/ch01/01-basic-types.md
  source_hash: 5619cb36a5e921cce92604d9d50f69f139a7dbc00a64649bbe5e48b5abcc0eaa
  translated_at: '2026-06-16T03:40:33.991613+00:00'
  engine: anthropic
  token_count: 3047
---
# Basic Data Types

In the previous chapter, we wrote our first C++ program, declared integer variables using `int`, and handled input/output with `std::cin` and `std::cout`. You might have thought: exactly how big of a number can `int` store? What about decimals? How do we represent text? These are excellent questions because they strike at the core of the C++ type system. In this chapter, we will thoroughly clarify what basic data types C++ provides, what each can store, how much, and where the boundaries lie.

You might say—why bother with this? Is it really necessary? Understanding data types isn't just about passing exams or acing interview questions; it is the foundation of writing correct programs. If you don't know the upper limit of `int`, you might suddenly overflow in a loop that looks perfectly normal. If you don't understand the precision pitfalls of floating-point numbers, your financial calculations might silently swallow a penny. If you are unclear about the signedness of `char`, your network protocols might inexplicably fail when ported across platforms. So, spending time solidifying this now will save you a massive amount of debugging time later. You might say—less nonsense, you're predicting my future. I used to think the same way, until I hand-wrote some code and got thoroughly screwed by `int`, realizing it should have been `unsigned long long`, and then I learned my lesson. Seriously, guys, you need to learn this.

## The Integer Family—How Many Choices Does C++ Give Us?

C++ integer types look numerous at first glance, but there is a clear pattern. Arranged from "small to large," the most basic integer types are `char`, `short`, `int`, and `long`. Each can be prefixed with `unsigned` to create an unsigned version. The C++ standard only mandates minimum ranges for them—for example, `short` is at least 16 bits—but on mainstream 64-bit platforms today, `int` is usually 32 bits and `long` is 64 bits. There is an easily confused point here: `long` is 64 bits on 64-bit Linux, but only 32 bits on 64-bit Windows. Yes, the exact same code, different operating system, `long` is different. This is why we need the fixed-width types we will discuss shortly.

Let's write some code to see the sizes of these types clearly. First, a simple program:

```cpp
#include <iostream>

int main() {
    std::cout << "char: " << sizeof(char) << '\n';
    std::cout << "short: " << sizeof(short) << '\n';
    std::cout << "int: " << sizeof(int) << '\n';
    std::cout << "long: " << sizeof(long) << '\n';
    std::cout << "long long: " << sizeof(long long) << '\n';
    std::cout << "bool: " << sizeof(bool) << '\n';

    return 0;
}
```

Compile and run:

```bash
g++ main.cpp -o main && ./main
```

On a typical 64-bit Linux system, the output is roughly:

```text
char: 1
short: 2
int: 4
long: 8
long long: 8
bool: 1
```

If you run the same code on Windows, the `long` line will show 4 instead of 8. (Probably, though I don't know the tiny differences in MSVC by heart—if I'm wrong about this, experts please correct me!) This is a platform difference, and a breeding ground for many cross-platform bugs.

> ⚠️ **Warning**: `sizeof` returns a type of `size_t`, which is an unsigned integer type. If you mix `size_t` and signed integers (like `int`) in an expression, the compiler might issue a "signed/unsigned comparison" warning. Do not ignore this warning, as it can indeed cause logic errors—we will explain this in detail when we cover type conversions.

## Fixed-Width Types—The Cross-Platform Anchor

Since the size of `long` varies by platform, how do we ensure an integer is exactly 32 bits when writing cross-platform code, parsing binary file formats, or manipulating network protocols? The answer is the **fixed-width types** provided by the `<cstdint>` header file.

These type names are straightforward: `int8_t` is an 8-bit signed integer, `uint32_t` is exactly a 32-bit unsigned integer, and so on. If your platform doesn't support a certain width (e.g., some embedded platforms lack 64-bit integers), the corresponding type won't exist—the compilation will simply error, which is much better than a runtime bug.

```cpp
#include <iostream>
#include <cstdint>

int main() {
    std::cout << "int8_t: " << sizeof(int8_t) << '\n';
    std::cout << "uint16_t: " << sizeof(uint16_t) << '\n';
    std::cout << "int32_t: " << sizeof(int32_t) << '\n';
    std::cout << "uint64_t: " << sizeof(uint64_t) << '\n';

    return 0;
}
```

Output:

```text
int8_t: 1
uint16_t: 2
int32_t: 4
uint64_t: 8
```

Whether you run this on Linux, Windows, or macOS, the results are identical. Yes. I didn't discuss whether we are on 32-bit or 64-bit. This is the charm of fixed-width types—it eliminates the uncertainty brought by platform differences. In embedded development, we almost always use types like `uint32_t` or `uint8_t` to manipulate registers, rather than `int` or `long`, because register widths are fixed and have nothing to do with the compiler's host platform.

## Type Limits—std::numeric_limits

Knowing how many bytes a type occupies, the next question is naturally: what is the maximum value it can actually store? C++ provides a very elegant tool to answer this—the `std::numeric_limits` template in the `<limits>` header.

```cpp
#include <iostream>
#include <limits>

int main() {
    std::cout << "int max: " << std::numeric_limits<int>::max() << '\n';
    std::cout << "int min: " << std::numeric_limits<int>::min() << '\n';
    std::cout << "uint32_t max: " << std::numeric_limits<uint32_t>::max() << '\n';
    std::cout << "double max: " << std::numeric_limits<double>::max() << '\n';
    std::cout << "double min: " << std::numeric_limits<double>::min() << '\n';

    return 0;
}
```

Output:

```text
int max: 2147483647
int min: -2147483648
uint32_t max: 4294967295
double max: 1.79769e+308
double min: 2.22507e-308
```

The maximum value of `int` is 2147483647, which is about 2.1 billion—this number actually overflows quite easily during accumulation operations. The maximum value of `unsigned int` doubles that to about 4.2 billion, which looks much larger, but is still insufficient when dealing with large file offsets or high-precision timestamps. So if you need to store numbers exceeding 2.1 billion, please use `long long`.

> ⚠️ **Warning**: Integer overflow in C++ is **undefined behavior** (except for unsigned types, which wrap around). This means that if you cause an `int` to exceed its maximum value by adding to it, the compiler can do anything—generate incorrect calculation results, optimize away your overflow check code, or even crash the program. Never assume "overflow just wraps around with modulo"; that is a guarantee only `unsigned` types provide.

## Floating-Point Numbers—The Game Between Precision and Approximation

Integers can only store whole values. Once decimals are involved, we need floating-point types. C++ provides three floating-point types: `float` (single precision, usually 4 bytes), `double` (double precision, usually 8 bytes), and `long double` (extended precision, size varies by platform, usually 16 bytes on x86-64 Linux).

`float` provides approximately 7 significant digits, while `double` provides about 15. This difference is critical in actual programming—if you are doing scientific calculations or finance-related operations, 7 digits of precision might not be enough, so you should go straight to `double`.

But floating-point numbers have a fundamental problem: they use binary to represent decimal fractions, so many "neat" decimal fractions are infinite loops in binary. This leads to floating-point operations being inherently approximate. Let's look at a classic example:

```cpp
#include <iostream>

int main() {
    float a = 0.1f;
    // a = a + 0.05f; // 0.15
    // a = a + 0.05f; // 0.2
    a += 0.05f; // 0.15
    a += 0.05f; // 0.2

    float b = 0.2f;

    std::cout << "a: " << a << '\n';
    std::cout << "b: " << b << '\n';
    std::cout << "a == b: " << (a == b) << '\n';

    return 0;
}
```

Output:

```text
a: 0.2
b: 0.2
a == b: 1
```

Interesting—in this specific case, they happen to be equal (because the error direction is consistent). But if we switch to `double`, the result might be different. What this example really illustrates is: the representation of a floating-point number in memory is not exactly the same as the literal value you write in code. So **never use `==` to compare two floating-point numbers**. The correct way is to judge whether their difference is within a sufficiently small range (epsilon):

```cpp
#include <cmath>
#include <iostream>

int main() {
    float a = 0.1f + 0.2f;
    float b = 0.3f;

    // Correct way: check if difference is less than a small threshold
    if (std::abs(a - b) < 0.0001f) {
        std::cout << "Approximately equal\n";
    }

    return 0;
}
```

`long double` is a special case—its size and precision vary significantly across platforms. On x86-64 Linux, it is usually 80-bit extended precision (occupying 16 bytes due to alignment padding), while on some ARM platforms it might be identical to `double`. So unless you know exactly what your target platform provides, don't rely too heavily on `long double`.

## Character Types—Not Just a Letter

Character types are likely the most confusing part of C++ basic types because they sit at the intersection of integers and text. The most basic `char` occupies exactly 1 byte (8 bits); it can store an ASCII character or be used as a small-range integer. But it doesn't end there—`char`, `signed char`, and `unsigned char` are **three distinct types** in C++. Whether a plain `char` is signed or unsigned is decided by the compiler. GCC defaults `char` to signed, but on ARM platforms it is usually unsigned.

```cpp
#include <iostream>

int main() {
    char c = 127;
    c = c + 1; // Overflow behavior depends on whether char is signed or unsigned

    std::cout << "c as int: " << +c << '\n'; // Use unary + to force integer promotion
    std::cout << "c as char: " << c << '\n';

    return 0;
}
```

Output:

```text
c as int: -128
c as char: �
```

You might have noticed that I used `+c` instead of directly `c` for output. This is because `std::cout` seeing a `char` type outputs the character directly instead of the number—if we output `c` directly, the terminal might display a garbled character.

Besides the classic `char`, C++ has several character types designed for Unicode. `wchar_t` is the "wide character," 2 bytes (UTF-16) on Windows and 4 bytes (UTF-32) on Linux, so it isn't cross-platform either. C++11 introduced `char16_t` (2 bytes, corresponding to UTF-16) and `char32_t` (4 bytes, corresponding to UTF-32), and C++20 added `char8_t` (1 byte, corresponding to UTF-8). For this stage of the tutorial, just knowing they exist is enough; we will go deeper when we handle strings later.

## Boolean Type—True and False, No Gray Areas

`bool` is the simplest type in C++, with only two values: `true` and `false`. How much memory does it occupy? Usually 1 byte, although theoretically 1 bit would suffice—but modern CPUs' smallest addressable unit is the byte, so `bool` is 1 on mainstream platforms.

There is a set of implicit conversion rules between `bool` and integers: zero converts to `false`, and any non-zero value converts to `true`. Conversely, `true` converts to `1`, and `false` converts to `0`. These rules look simple but hide some easy-to-fall-into traps.

> ⚠️ **Warning**: Do not write code like `if (a = 5)`. Here `a = 5` is assignment, not comparison; `a` is assigned 5, then 5 is implicitly converted to `true`, so this `if` is always true. The compiler with `-Wall` will give a warning, so again—compiler warnings aren't decorations, treat every one seriously.

Another point worth noting is the behavior of `false` and `true` in mathematical operations:

```cpp
#include <iostream>

int main() {
    int count = true + true + false; // 1 + 1 + 0 = 2
    std::cout << "count: " << count << '\n';

    return 0;
}
```

Output:

```text
count: 2
```

When `false` participates in arithmetic operations, it is treated as `0`, and `true` is treated as `1`. This can sometimes be used for concise counting—like counting how many boolean conditions in a set are true—but if you find yourself writing this kind of "clever" code, stop and think: is there a clearer way? Code readability is usually more important than brevity.

## sizeof Revealed—How Much Memory Does a Type Actually Occupy

So far we have been using `sizeof`, but haven't formally introduced it. `sizeof` is a C++ operator (not a function) that can calculate the number of bytes occupied by a type or variable at **compile time**. This means it has zero runtime overhead—the compiler embeds the result directly into the code as a constant.

```cpp
#include <iostream>

int main() {
    int x[10];

    std::cout << "sizeof(int): " << sizeof(int) << '\n';
    std::cout << "sizeof(x): " << sizeof(x) << '\n';
    std::cout << "sizeof(x) / sizeof(x[0]): " << sizeof(x) / sizeof(x[0]) << '\n';

    return 0;
}
```

Typical output on 64-bit Linux:

```text
sizeof(int): 4
sizeof(x): 40
sizeof(x) / sizeof(x[0]): 10
```

Remember these numbers—of course, don't rote memorize them; you can always write a small program to test them. We are learning programming here. This is a requirement—verifying the size of our types. Don't memorize it, but think about how to achieve it! What really needs to be etched into your brain is this understanding: the size of a type isn't arbitrary; it directly affects the program's memory layout and performance. On embedded systems, SRAM might be only a few dozen KB, so the choice between `int` and `int8_t` isn't a matter of style preference, but of whether you can save the space.

## The Wisdom of Selection—When to Use Which Type

We've covered so many types, so how do we choose? Here are some practical experiences. They might not cover every scenario, but they can at least help you make a decision that's right nine times out of ten.

For general-purpose integers, use `int`. It is the type the compiler "likes" best—operations are usually fastest, and code generation is most optimized. Loop variables, array indices, simple counters—just use `int` for all of them. Only when you are certain the data range will exceed `int`'s limit (about plus/minus 2.1 billion), or you need to handle unsigned values, should you consider switching to `long` or `long long`.

For scenarios where size must be deterministic, use fixed-width types from `<cstdint>`. Parsing binary files, network communication protocols, manipulating hardware registers, serializing data structures—whenever you have a requirement like "byte N to byte M must be an integer of X length," you should use types like `uint32_t` or `int8_t`. Don't assume `int` is definitely 32 bits, although almost all platforms today are like this, the standard doesn't guarantee it.

For floating-point operations, use `double`, unless you have a specific reason to choose `float`. `double`'s precision is more than double that of `float`, and on modern CPUs there is almost no difference in calculation speed (both have hardware FPU support). Only in scenarios where storage space is extremely tight—like storing large amounts of measurement data on embedded devices—is it worth sacrificing precision for the 4 bytes of `float`. As for `long double`, unless you are doing extremely high-precision scientific calculations, you basically won't use it.

For boolean logic, use `bool`, don't use `int` as a boolean value. In the C era, there was indeed a habit of "zero is false, non-zero is true" (of course, C23 now has a proper `bool` too, friends who don't know should go try it!), but in C++ we have a proper `bool` type. Using it makes code intent clearer and allows the compiler to do better type checking.

## Run Online

Actually run this on your platform to see how many bytes each type occupies:

<OnlineCompilerDemo
  title="Basic Data Types: sizeof and Ranges Overview"
  source-path="code/examples/vol1/02_basic_types.cpp"
  description="Run online and observe the size and value ranges of various C++ basic types on your platform."
  allow-run
/>

## Try It Yourself

### Exercise 1: Complete Size and Range Report

Write a program that prints the `sizeof` and the minimum/maximum values obtained via `std::numeric_limits` for all basic integer types (`char`, `short`, `int`, `long`, `long long` and their `unsigned` versions, plus `int8_t`, `int16_t`, `int32_t`, `int64_t` and their `unsigned` versions). Format the output to make the results clear at a glance.

### Exercise 2: Predict sizeof Results

Before looking at the answer, predict the results of the following expressions on your platform, then write a program to verify them: `sizeof(int)`, `sizeof(int*)`, `sizeof(double*)`, `sizeof(int[10])`, `sizeof("hello")`. Extra challenge: write a `.c` file compiled as a C program, and a `.cpp` file compiled as a C++ program, both printing `sizeof('a')`, and observe the difference in results. Hint: In C++, the type of the character literal `'a'` is `char` (`sizeof` is 1), while in C the character constant `'a'` is of type `int` (`sizeof` is usually 4). This is a subtle but important difference between the two languages.

### Exercise 3: Experience the Floating-Point Precision Trap

Write a program that uses a `float` variable starting from 0, adding 0.1 ten times, then judge if the result equals 1.0. Do the same with `double`. Observe the difference in behavior, and use `std::cout` to print the exact value after each accumulation step.

## Summary

In this chapter, we went through C++'s basic data types from start to finish. Integer types include `char`, `short`, `int`, `long`, `long long` and their unsigned versions, with sizes varying by platform; fixed-width types like `int32_t` solve the cross-platform consistency issue. Floating-point types include `float`, `double`, and `long double`, with increasing precision, but always remember that floating-point numbers are approximate representations and cannot be compared directly with `==`. Character types sit at the intersection of integers and text; `char`, `signed char`, and `unsigned char` are three distinct types. The boolean type is simple, but implicit conversion rules can easily create subtle bugs. The `sizeof` operator calculates type size at compile time, and `std::numeric_limits` provides value ranges for types.

In the next chapter, we will look at how these types convert between each other—when implicit conversions are safe or dangerous, and how to properly use `static_cast` and other casts. Type conversion is one of the most problematic areas in the C++ type system; understanding it will give us much more peace of mind when writing code.
