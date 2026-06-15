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
  engine: anthropic
  source: documents/vol1-fundamentals/ch01/01-basic-types.md
  source_hash: 1984d5a4e598c9335dbae2f9f51e4515cf9fb0e4e14444f22c4d1effdcb9d608
  token_count: 3051
  translated_at: '2026-05-26T10:42:30.550976+00:00'
---
# Fundamental Data Types

In the previous chapter, we wrote our first C++ program, declared integer variables with `int`, and handled input and output with `std::cin` and `std::cout`. You might have been wondering right then: how large a number can `int` actually hold? What about decimals? How do we represent text? These are great questions because they cut straight to the heart of the C++ type system. In this chapter, we will thoroughly explore the fundamental data types C++ provides, what each can store, how much it can hold, and where the boundaries lie.

Now, you might be thinking—why bother with this? Folks, understanding data types isn't just about passing exams or interview questions; it is the foundation of writing correct programs. If you don't know the upper limit of `int`, you might suddenly overflow in what looks like a perfectly normal loop. If you ignore the precision traps of floating-point numbers, your financial calculations might silently swallow a penny. If you get confused by the signedness of `char`, your network protocol might inexplicably break when ported to another platform. So, spending time solidifying this knowledge now will save you a ton of debugging time later. You might say—cut the crap, how do you know what will happen to me later? Honestly, I used to think the same way, until I was writing code and got absolutely burned by using `int` where it should have been `unsigned long long`. That humbled me real quick. You really need to learn this stuff, folks.

## The Integer Family—How Many Choices Does C++ Give Us

C++ integer types might seem overwhelming at first glance, but they follow a clear pattern. Arranged from smallest to largest, the most basic integer types are `short`, `int`, `long`, and `long long`. Each can be prefixed with `unsigned` to create an unsigned version. The C++ standard only specifies minimum ranges for these—for instance, `int` must be at least 16 bits—but on mainstream 64-bit platforms today, `int` is typically 32 bits, and `long long` is 64 bits. Here is a common point of confusion: `long` is 64 bits on 64-bit Linux systems, but only 32 bits on 64-bit Windows. That's right—the exact same code yields a different `sizeof(long)` just by switching the operating system. This is exactly why we need the fixed-width types we will discuss shortly.

Let's write some code to see the sizes of these types clearly. First, a simple program:

```cpp
// integer-type-sizes.cpp
// 打印 C++ 基本整数类型在当前平台上的大小

#include <iostream>

int main()
{
    std::cout << "=== 整数类型大小（字节） ===" << std::endl;
    std::cout << "short:          " << sizeof(short) << std::endl;
    std::cout << "int:            " << sizeof(int) << std::endl;
    std::cout << "long:           " << sizeof(long) << std::endl;
    std::cout << "long long:      " << sizeof(long long) << std::endl;
    std::cout << std::endl;

    std::cout << "=== 对应的无符号版本 ===" << std::endl;
    std::cout << "unsigned short: " << sizeof(unsigned short) << std::endl;
    std::cout << "unsigned int:   " << sizeof(unsigned int) << std::endl;
    std::cout << "unsigned long:  " << sizeof(unsigned long) << std::endl;
    std::cout << "unsigned long long: " << sizeof(unsigned long long)
              << std::endl;

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -o integer-type-sizes integer-type-sizes.cpp
./integer-type-sizes
```

On a typical 64-bit Linux system, the output looks roughly like this:

```text
=== 整数类型大小（字节） ===
short:          2
int:            4
long:           8
long long:      8

=== 对应的无符号版本 ===
unsigned short: 2
unsigned int:   4
unsigned long:  8
unsigned long long: 8
```

If you run the same code on Windows, the line for `long` will show 4 instead of 8. (Probably—I recall it being different, but I am completely unfamiliar with the minor quirks of MSVC. If I got this wrong, experts please correct me immediately!) This is a platform difference, and it is a breeding ground for many cross-platform bugs.

> ⚠️ **Pitfall Warning**: The return type of `sizeof` is `std::size_t`, which is an unsigned integer type. If you mix `std::size_t` with a signed integer (like `int`) in an expression, the compiler might issue a "signed/unsigned comparison" warning. Do not ignore this warning, because it can genuinely lead to logic errors—we will explain this in detail when we cover type conversions.

## Fixed-Width Types—The Cross-Platform Anchor

Since the size of `long` varies by platform, how do we ensure an integer is exactly 32 bits when writing cross-platform code, parsing binary file formats, or working with network protocols? The answer is the **fixed-width types** provided by the `<cstdint>` header.

These type names are very straightforward: `int8_t` is an exactly 8-bit signed integer, `uint32_t` is an exactly 32-bit unsigned integer, and so on. If your platform does not support a particular width (for example, certain embedded platforms lack 64-bit integers), the corresponding type simply will not exist—the compiler will error out directly, which is far better than hitting a bug at runtime.

```cpp
#include <cstdint>
#include <iostream>

int main()
{
    std::cout << "=== 固定宽度类型大小（字节） ===" << std::endl;
    std::cout << "int8_t:   " << sizeof(int8_t) << std::endl;
    std::cout << "int16_t:  " << sizeof(int16_t) << std::endl;
    std::cout << "int32_t:  " << sizeof(int32_t) << std::endl;
    std::cout << "int64_t:  " << sizeof(int64_t) << std::endl;
    std::cout << std::endl;
    std::cout << "uint8_t:  " << sizeof(uint8_t) << std::endl;
    std::cout << "uint16_t: " << sizeof(uint16_t) << std::endl;
    std::cout << "uint32_t: " << sizeof(uint32_t) << std::endl;
    std::cout << "uint64_t: " << sizeof(uint64_t) << std::endl;

    return 0;
}
```

Output:

```text
=== 固定宽度类型大小（字节） ===
int8_t:   1
int16_t:  2
int32_t:  4
int64_t:  8

uint8_t:  1
uint16_t: 2
uint32_t: 4
uint64_t: 8
```

Whether you run this on Linux, Windows, or macOS, the result is the same. Yes. I didn't even mention whether we are on a 32-bit or 64-bit system. That is the charm of fixed-width types—they eliminate the uncertainty brought by platform differences. In embedded development, we almost always use types like `uint8_t` and `uint32_t` to manipulate registers, rather than `int` or `unsigned long`, because register widths are fixed and have nothing to do with the compiler's host platform.

## Type Limits—std::numeric_limits

Once we know how many bytes a type occupies, the next natural question is: what is the largest number it can actually hold? C++ provides a very elegant tool to answer this question—the `std::numeric_limits` template in the `<limits>` header.

```cpp
#include <cstdint>
#include <iostream>
#include <limits>

int main()
{
    std::cout << "=== int32_t 的范围 ===" << std::endl;
    std::cout << "最小值: " << std::numeric_limits<int32_t>::min()
              << std::endl;
    std::cout << "最大值: " << std::numeric_limits<int32_t>::max()
              << std::endl;
    std::cout << std::endl;

    std::cout << "=== uint32_t 的范围 ===" << std::endl;
    std::cout << "最小值: " << std::numeric_limits<uint32_t>::min()
              << std::endl;
    std::cout << "最大值: " << std::numeric_limits<uint32_t>::max()
              << std::endl;

    return 0;
}
```

Output:

```text
=== int32_t 的范围 ===
最小值: -2147483648
最大值: 2147483647

=== uint32_t 的范围 ===
最小值: 0
最大值: 4294967295
```

The maximum value of `int32_t` is 2147483647, which is about 2.1 billion—this number is actually quite easy to overflow when doing cumulative operations. The maximum value of `uint32_t` doubles to about 4.2 billion, which looks much larger, but it is still insufficient when handling large file offsets or high-precision timestamps. So, if you need to store a number larger than 2.1 billion, please use `int64_t`.

> ⚠️ **Pitfall Warning**: Integer overflow in C++ is **undefined behavior** (except for unsigned types, which wrap around). This means that if you add a value to an `int` causing it to exceed its maximum, the compiler is free to do anything—generate incorrect calculation results, optimize away your overflow checks, or even crash the program. Never assume that "overflow just takes the modulus"; that is a guarantee only `unsigned` provides.

## Floating-Point Numbers—The Trade-off Between Precision and Approximation

Integers can only store whole values; once decimals are involved, we need floating-point types. C++ provides three floating-point types: `float` (single precision, typically 4 bytes), `double` (double precision, typically 8 bytes), and `long double` (extended precision, size varies by platform, usually 16 bytes on x86-64 Linux).

`float` provides about 7 significant digits of precision, while `double` provides about 15. This difference is critical in practical programming—if you are doing scientific calculations or financial computations, 7 digits of precision is likely not enough, and you should jump straight to `double`.

But floating-point numbers have a fundamental issue: they use binary to represent decimal fractions, so many decimal numbers that look "neat and tidy" are actually infinite repeating fractions in binary. This means floating-point arithmetic is inherently approximate. Let's look at a classic example:

```cpp
#include <iomanip>
#include <iostream>

int main()
{
    float a = 0.1f;
    float b = 0.2f;
    float c = a + b;

    // 用高精度输出，看清楚浮点数的真面目
    std::cout << std::setprecision(20);
    std::cout << "0.1f  = " << a << std::endl;
    std::cout << "0.2f  = " << b << std::endl;
    std::cout << "a + b = " << c << std::endl;
    std::cout << "0.3f  = " << 0.3f << std::endl;
    std::cout << std::endl;

    // 比较结果
    if (c == 0.3f) {
        std::cout << "a + b == 0.3f (相等)" << std::endl;
    }
    else {
        std::cout << "a + b != 0.3f (不相等!)" << std::endl;
        std::cout << "差值: " << (c - 0.3f) << std::endl;
    }

    return 0;
}
```

Output:

```text
0.1f  = 0.10000000149011611938
0.2f  = 0.20000000298023223877
a + b = 0.30000001192092895508
0.3f  = 0.30000001192092895508

a + b == 0.3f (相等)
```

Interesting—in this specific example, they happen to be equal (because the direction of the error is consistent). But if we switch to `double`, the result might be different. What this example truly illustrates is that a floating-point number's in-memory representation is not perfectly identical to the literal value you write in your code. Therefore, **never use `==` to compare two floating-point numbers**. The correct approach is to check whether their difference falls within a sufficiently small range:

```cpp
bool is_approximately_equal(double x, double y, double epsilon)
{
    // epsilon 通常取 1e-9 或更小，具体看你的精度需求
    return (x - y) < epsilon && (y - x) < epsilon;
}
```

The case of `long double` is rather special—its size and precision vary significantly across different platforms. On x86-64 Linux, it is usually 80-bit extended precision (actually taking up 16 bytes due to alignment padding), while on certain ARM platforms it might be exactly the same as `double`. So, unless you know exactly what your target platform provides, do not rely too heavily on `long double`.

## Character Types—More Than Just a Letter

Character types might be the most confusing of C++'s fundamental types, because they sit right at the boundary between integers and text. The most basic `char` takes up exactly 1 byte (8 bits); it can store an ASCII character or be used as a small-range integer. But the story doesn't end there—in C++, `char`, `signed char`, and `unsigned char` are **three distinct types**. Whether a plain `char` is signed or unsigned is decided by the compiler. GCC defaults `char` to signed, but on ARM platforms it is typically unsigned.

```cpp
#include <iostream>

int main()
{
    char c = 'A';
    signed char sc = -1;
    unsigned char uc = 255;

    std::cout << "char 'A' 的整数值: " << static_cast<int>(c) << std::endl;
    std::cout << "signed char -1 的整数值: " << static_cast<int>(sc)
              << std::endl;
    std::cout << "unsigned char 255 的整数值: " << static_cast<int>(uc)
              << std::endl;

    return 0;
}
```

Output:

```text
char 'A' 的整数值: 65
signed char -1 的整数值: -1
unsigned char 255 的整数值: 255
```

You might have noticed that I used `static_cast<int>(c)` for the output instead of directly using `std::cout << c`. This is because when `std::cout` sees a `char` type, it outputs the character directly rather than the number—if we output `sc` directly, the terminal might display a garbled character.

Beyond the classic `char`, C++ has several character types designed for Unicode. `wchar_t` is the "wide character," which is 2 bytes (UTF-16) on Windows and 4 bytes (UTF-32) on Linux, so it is not cross-platform either. C++11 introduced `char16_t` (2 bytes, corresponding to UTF-16) and `char32_t` (4 bytes, corresponding to UTF-32), and C++20 added `char8_t` (1 byte, corresponding to UTF-8). For this stage of the tutorial, just knowing they exist is enough; we will dive deeper when we handle strings later on.

## Boolean Type—True or False, No Gray Area

`bool` is the simplest type in C++, with only two values: `true` and `false`. How much memory does it take up? Usually 1 byte, even though theoretically 1 bit would suffice—but the smallest addressable unit on modern CPUs is the byte, so `sizeof(bool)` is 1 on all mainstream platforms.

There is a set of implicit conversion rules between `bool` and integers: zero converts to `false`, and any non-zero value converts to `true`. Conversely, `false` converts to `0`, and `true` converts to `1`. These rules look simple, but they hide some easy-to-fall-into traps.

> ⚠️ **Pitfall Warning**: Do not write code like `if (x = 5)`. Here, `=` is assignment, not comparison. `x` is assigned the value 5, and then 5 is implicitly converted to `true`, so this `if` is always true. The compiler will issue a warning if you add `-Wall`, so to emphasize this once again—compiler warnings are not decorations; take every single one seriously.

Another thing worth noting is the behavior of `bool` to `int` conversions in mathematical operations:

```cpp
#include <iostream>

int main()
{
    bool flag = true;
    int count = flag + flag + flag;

    std::cout << "true + true + true = " << count << std::endl;
    std::cout << "sizeof(bool) = " << sizeof(bool) << std::endl;

    return 0;
}
```

Output:

```text
true + true + true = 3
sizeof(bool) = 1
```

When `true` participates in arithmetic operations, it is treated as `1`, and `false` is treated as `0`. This can sometimes be used for concise counting—like counting how many boolean conditions in a set are true—but if you find yourself writing this kind of "clever" code, stop and think: is there a clearer way to write it? Code readability is usually more important than conciseness.

## Demystifying sizeof—How Much Memory Does a Type Actually Occupy

So far we have been using `sizeof`, but we haven't formally introduced it yet. `sizeof` is a C++ operator (not a function) that can calculate the number of bytes occupied by a type or variable at **compile time**. This means it has zero runtime overhead—the compiler directly embeds the result as a constant into the code.

```cpp
#include <iostream>

int main()
{
    std::cout << "=== 基本类型 sizeof 汇总 ===" << std::endl;
    std::cout << "bool:          " << sizeof(bool) << " 字节" << std::endl;
    std::cout << "char:          " << sizeof(char) << " 字节" << std::endl;
    std::cout << "short:         " << sizeof(short) << " 字节" << std::endl;
    std::cout << "int:           " << sizeof(int) << " 字节" << std::endl;
    std::cout << "long:          " << sizeof(long) << " 字节" << std::endl;
    std::cout << "long long:     " << sizeof(long long) << " 字节" << std::endl;
    std::cout << "float:         " << sizeof(float) << " 字节" << std::endl;
    std::cout << "double:        " << sizeof(double) << " 字节" << std::endl;
    std::cout << "long double:   " << sizeof(long double) << " 字节"
              << std::endl;

    return 0;
}
```

Typical output on 64-bit Linux:

```text
=== 基本类型 sizeof 汇总 ===
bool:          1 字节
char:          1 字节
short:         2 字节
int:           4 字节
long:          8 字节
long long:     8 字节
float:         4 字节
double:        8 字节
long double:   16 字节
```

Remember these numbers—of course, you don't need to memorize them by rote; you can always write a small program to test them. We are learning programming. This is simply a requirement—verifying the sizes of our types. We don't memorize it; we think about how to accomplish it! What truly needs to be etched into your mind is this realization: a type's size is not arbitrary; it directly affects the memory layout and performance of your program. On embedded systems, SRAM might only be a few dozen KB, and at that point, choosing between `int` and `int8_t` is no longer a matter of stylistic preference, but a matter of whether you can fit it in memory.

## The Wisdom of Type Selection—When to Use What

After discussing so many types, how do we actually choose? Here are a few pieces of practical experience. They might not cover every scenario, but they can at least help you make the right call eight or nine times out of ten.

For general-purpose integers, use `int`. It is the compiler's "favorite" type—arithmetic operations are usually fastest, and code generation is most optimized. Loop variables, array indices, simple counters—just use `int` for all of them. Only consider switching to `long long` or `unsigned` when you are certain the data range will exceed the limit of `int` (about plus or minus 2.1 billion), or when you need to handle unsigned values.

For scenarios where the size must be guaranteed, use the fixed-width types from `<cstdint>`. Parsing binary files, network communication protocols, manipulating hardware registers, serializing data structures—whenever you have a requirement like "bytes N through M must be an integer of exactly this length," you should use types like `int32_t` and `uint16_t`. Do not assume `int` is definitely 32 bits; although this is true on almost all platforms today, the standard does not guarantee it.

For floating-point arithmetic, use `double`, unless you have a specific reason to choose `float`. The precision of `double` is more than double that of `float`, and on modern CPUs, there is almost no difference in their calculation speeds (both have hardware FPU support). Only in scenarios where storage space is extremely tight—like storing large amounts of measurement data on embedded devices—is it worth sacrificing precision to save the 4 bytes of `float`. As for `long double`, unless you are doing extremely high-precision scientific calculations, you basically will never use it.

For boolean logic, use `bool`; do not use `int` as a stand-in for boolean values. The C language era确实 had the habit of "zero is false, non-zero is true" (of course, C23 now has a proper `bool` too, go try it out if you didn't know!), but in C++ we have a proper `bool` type. Using it makes the code's intent clearer and allows the compiler to perform better type checking.

## Run Online

Actually run this on your platform to see exactly how many bytes each type occupies:

<OnlineCompilerDemo
  title="Fundamental Data Types: sizeof and Ranges at a Glance"
  source-path="code/examples/vol1/02_basic_types.cpp"
  description="Run online and observe the sizes and value ranges of various fundamental C++ types on your platform."
  allow-run
/>

## Try It Yourself

### Exercise 1: Complete Size and Range Report

Write a program that prints the `sizeof` and the minimum and maximum values obtained via `std::numeric_limits` for all fundamental integer types (`short`, `int`, `long`, `long long` and their `unsigned` versions, plus `int8_t`, `int16_t`, `int32_t`, `int64_t` and their `unsigned` versions). Format the output so the results are clear at a glance.

### Exercise 2: Predict the sizeof Results

Before looking at the answers, predict the results of the following expressions on your platform, then write a program to verify them: `sizeof('A')`, `sizeof(true)`, `sizeof(3.14)`, `sizeof(3.14f)`, `sizeof(3.14L)`. Extra challenge: write a `.c` file compiled as a C program, and a `.cpp` file compiled as a C++ program, both printing `sizeof('A')`. Observe the difference in results. Hint: in C++, the type of a character literal `'A'` is `char` (`sizeof` is 1), whereas in C, the type of a character constant `'A'` is `int` (`sizeof` is typically 4). This is a subtle but important difference between the two languages.

### Exercise 3: Experience the Floating-Point Precision Trap

Write a program that uses a `float` variable starting at 0, adds 0.1 ten times, and then checks whether the result equals 1.0. Do the same thing with `double`. Observe the difference in behavior between the two, and use `std::setprecision` to print the exact value after each accumulation step.

## Summary

In this chapter, we went through C++'s fundamental data types from start to finish. The integer types include `short`, `int`, `long`, `long long`, and their unsigned versions, with sizes varying by platform; the fixed-width types `<cstdint>` solve the problem of cross-platform consistency. The floating-point types include `float`, `double`, and `long double`, with precision increasing at each level, but we must always keep in mind that floating-point numbers are approximate representations and cannot be compared directly with `==`. Character types sit at the boundary between integers and text, and `char`, `signed char`, and `unsigned char` are three distinct types. Although the boolean type is simple, its implicit conversion rules can easily create hidden bugs. The `sizeof` operator calculates type sizes at compile time, and `std::numeric_limits` provides the value ranges of types.

In the next chapter, we will look at how these types convert into one another—when implicit conversions are safe, when they are dangerous, and how to properly use `static_cast` and other forms of casting. Type conversion is one of the most error-prone areas in the C++ type system; once we understand it clearly, we will feel much more at ease when writing code.
