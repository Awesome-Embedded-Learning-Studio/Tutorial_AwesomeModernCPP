---
chapter: 1
cpp_standard:
- 11
- 14
- 17
- 20
description: Master C++ integer, floating-point, character, and boolean types, and understand type sizes, value ranges, and platform differences
difficulty: beginner
order: 1
platform: host
prerequisites:
- Your First C++ Program
reading_time_minutes: 22
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: Basic Data Types
translation:
  source: documents/vol1-fundamentals/ch01/01-basic-types.md
  source_hash: e8b0d6f942bb3c0334f645ab5d12cc14b7b3206353ad13b8970dbea8e3757f69
  translated_at: '2026-09-25T09:56:19+00:00'
  engine: anthropic
  token_count: 14000
---
# Our First Step: Basic Data Types in C++

In the previous chapter we wrote our first C++ program: we declared integer variables with `int` and did input and output with `std::cin` and `std::cout`. You may well have been wondering at the time: how large a number can `int` actually hold? What about decimals? How do we represent text? These are excellent questions, because they go straight to the core of the C++ type system. **Put another way, C++'s basic data types directly describe what it is we are storing.**

Oh, you might say—what's the point of all this? Bros, **understanding data types is not just about passing exams or interview questions; it is the foundation of writing correct programs.** If you don't know the upper limit of `int`, you can suddenly overflow in a loop that looks perfectly normal; if you are unaware of the precision traps of floating-point numbers, your financial calculations may silently swallow a penny (uh-oh—careful, or the on-call group chat will drag you in for a public roasting~); if you are fuzzy on the signedness of `char`, your network protocol may break inexplicably the moment it crosses platforms. So the time we spend pounding these things solid now will save us a heap of debugging time later. And if you are thinking—cut the crap, like you could possibly know what happens to me down the road—**hmm, that is exactly how I used to be, until the code I wrote with my own two hands got thoroughly wrecked by an `int`, and I discovered that spot should have been `unsigned long long` all along. That humbled me real quick. You really do need to learn this, folks.**

## The Integer Family: How Many Choices Does C++ Give Us

C++'s integer types look like a lot at first glance, but there is a clear pattern to them. Ordered from smallest to largest, the most basic integer types are `short`, `int`, `long`, and `long long`, and each one can take the `unsigned` prefix to become an unsigned version. The C++ standard only specifies minimum ranges for them—for example, `int` must be at least 16 bits—but on today's mainstream 64-bit platforms, `int` is usually 32 bits and `long long` is 64 bits. Here is a spot that trips people up easily: `long` is 64 bits on 64-bit Linux, but only 32 bits on 64-bit Windows. That's right—the very same code, a different operating system, and `sizeof(long)` changes. This is exactly why we need the fixed-width types, which we will cover later.

Let's make the sizes of these types crystal clear with code. First, a simple program:

```cpp
// integer-type-sizes.cpp
// Print the sizes of C++'s basic integer types on the current platform

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

On a typical 64-bit Linux system, the output looks roughly like:

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

If you run the same code on Windows, the `long` line will show 4 instead of 8. (Probably—I remember it being different, but I am completely unfamiliar with the little quirks of MSVC; if I've got this wrong, experts, please come criticize me at once!) This is platform variance, and it is the breeding ground for many a cross-platform bug.

> The type `sizeof` returns is `std::size_t`, which is an unsigned integer type. If you mix `std::size_t` and a signed integer (say, `int`) in one expression, the compiler may emit a "signed/unsigned comparison" warning. **Do not ignore that kind of warning—it genuinely can lead to logic errors. We will explain in detail when we get to type conversion later on.**

## Fixed-Width Types — Cross-Platform Peace of Mind

"Oh shit! Charliechen114514, what if—and I'm just saying what if—we need fixed-width types? What then? My code is awesome; it has to run all the way from a 16-bit MCU to 64-bit Windows!" Some readers have exactly this worry. If you don't have it yet, I recommend acquiring it. It's useful.

We just said that the size of `long` changes with the platform. So when we write cross-platform code, parse binary file formats, or work with network protocols, how do we make sure an integer is exactly 32 bits? The answer is the **fixed-width types** provided by the `<cstdint>` header.

These type names are refreshingly plain: `int8_t` is a signed integer of exactly 8 bits, `uint32_t` is an unsigned integer of exactly 32 bits, and so on. If your platform does not support a given width (for example, some embedded platforms have no 64-bit integer), the corresponding type simply will not exist—the compile fails right then and there, which beats shipping a runtime bug by a mile.

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

Whether you run it on Linux, Windows, or macOS, the result is the same. Yes. And notice I said nothing about whether we are on a 32-bit or a 64-bit machine. That is the charm of fixed-width types—they wipe out the uncertainty that platform differences bring. In embedded development, we almost always use types like `uint8_t` and `uint32_t` to operate on registers instead of `int` or `unsigned long`, because a register's width is fixed and has nothing to do with the platform the compiler runs on.

> The limits of these types can also be inspected through the `numeric_limits` header file, but that drags templates into the picture, so we've left it out here~

## Floating-Point Numbers — The Tug-of-War Between Exact and Approximate

Integers can only store whole values; the moment decimals enter the picture, we need floating-point types. C++ provides three: `float` (single precision, usually 4 bytes), `double` (double precision, usually 8 bytes), and `long double` (extended precision; the size varies by platform, usually 16 bytes on x86-64 Linux).

`float` provides roughly 7 significant digits, `double` roughly 15. This difference is critical in real-world programming—if you are doing scientific computation or finance-related arithmetic, 7 digits of precision may well not be enough, and you should go straight to `double`.

But floating-point numbers have a fundamental problem: they represent decimal fractions in binary, so many decimal fractions that look "neat and tidy" repeat infinitely in binary. As a result, floating-point arithmetic is approximate by nature. Here is a classic example:

```cpp
#include <iomanip>
#include <iostream>

int main()
{
    float a = 0.1f;
    float b = 0.2f;
    float c = a + b;

    // Print with high precision to see the true face of floating-point numbers
    std::cout << std::setprecision(20);
    std::cout << "0.1f  = " << a << std::endl;
    std::cout << "0.2f  = " << b << std::endl;
    std::cout << "a + b = " << c << std::endl;
    std::cout << "0.3f  = " << 0.3f << std::endl;
    std::cout << std::endl;

    // Compare the results
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

Interesting—in this particular example they happen to be equal: the rounding errors of `0.1f` and `0.2f` point in the same direction, and the sum lands exactly on `0.3f`'s own rounded value. But swap `float` for `double` in the same code, and that luck runs out:

```cpp
#include <iomanip>
#include <iostream>

int main()
{
    double a = 0.1;
    double b = 0.2;
    double c = a + b;

    // Same trick, only swapping float for double
    std::cout << std::setprecision(20);
    std::cout << "0.1  = " << a << std::endl;
    std::cout << "0.2  = " << b << std::endl;
    std::cout << "a + b = " << c << std::endl;
    std::cout << "0.3  = " << 0.3 << std::endl;
    std::cout << std::endl;

    if (c == 0.3) {
        std::cout << "a + b == 0.3 (相等)" << std::endl;
    }
    else {
        std::cout << "a + b != 0.3 (不相等!)" << std::endl;
        std::cout << "差值: " << (c - 0.3) << std::endl;
    }

    return 0;
}
```

Output:

```text
0.1  = 0.10000000000000000555
0.2  = 0.2000000000000000111
a + b = 0.30000000000000004441
0.3  = 0.2999999999999999889

a + b != 0.3 (不相等!)
差值: 5.5511151231257827021e-17
```

This time `a + b` is about 5.55e-17 larger than `0.3`—both `0.1` and `0.2` sit a touch high in memory, while `0.3` sits low; add them up, round once more, and the gap between the two sides opens wide. Taken together, the two examples tell the same story: a floating-point number's representation in memory does not exactly match the literal you wrote, and whether `==` rules them equal or not depends entirely on whether the rounding errors happen to cancel out. So **never compare two floating-point numbers with `==`**. The correct approach is to check whether their difference falls within a sufficiently small range:

```cpp
#include <cmath>  // std::fabs

bool is_approximately_equal(double x, double y, double epsilon)
{
    // epsilon is usually 1e-9 or smaller, depending on your precision needs
    return std::fabs(x - y) < epsilon;
}
```

The situation with `long double` is rather special—its size and precision vary a lot across platforms. On x86-64 Linux it is usually 80-bit extended precision (actually occupying 16 bytes because of alignment padding), while on some ARM platforms it may be exactly the same as `double`. So unless you know exactly what your target platform provides, do not lean too heavily on `long double`.

## Character Types — More Than a Letter

The character types are probably the most confusing of C++'s basic types, because they sit right on the boundary between integers and text. The most basic one, `char`, occupies exactly 1 byte (8 bits); it can hold an ASCII character, or double as a small-range integer. But it does not end there—`char`, `signed char`, and `unsigned char` are **three distinct types** in C++. Whether plain `char` is signed or unsigned is decided by the compiler. GCC defaults `char` to signed, but on ARM platforms it is usually unsigned.

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

You may have noticed that I used `static_cast<int>(c)` in the output rather than `std::cout << c` directly. That is because when `std::cout` sees a `char`, it prints the character itself rather than a number—if we printed `sc` directly, the terminal might well show a garbled character.

Beyond the classic `char`, C++ also has several character types designed for Unicode. `wchar_t` is the "wide character"—2 bytes on Windows (UTF-16), 4 bytes on Linux (UTF-32)—so it is not cross-platform either. C++11 introduced `char16_t` (2 bytes, for UTF-16) and `char32_t` (4 bytes, for UTF-32), and C++20 added `char8_t` (1 byte, for UTF-8). For where we are at this stage of the tutorial, just knowing they exist is enough; we will dig deeper when we get to strings later on.

## The Boolean Type — True and False, No Gray Zone

`bool` is the simplest type in C++, with only two values: `true` and `false`. How much memory does it take? Usually 1 byte, even though in theory 1 bit would be enough—but the smallest unit a modern CPU can address is a byte, so `sizeof(bool)` is 1 on all mainstream platforms.

Between `bool` and integers there is a set of implicit conversion rules: zero converts to `false`, and any nonzero value converts to `true`. In the other direction, `false` converts to `0`, and `true` converts to `1`. The rules look simple, but they hide some pits that are easy to fall into.

Never write code like `if (x = 5)`: the `=` here is assignment, not comparison—`x` gets assigned 5, then 5 implicitly converts to `true`, and this `if` is always true. With `-Wall`, the compiler will warn about it—and once again, compiler warnings are not decoration. Take every single one of them seriously.

Another point worth noting is how the `bool`-to-`int` conversion behaves inside arithmetic:

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

When `true` participates in arithmetic it is treated as `1`, and `false` as `0`. Sometimes that can make for a tidy little counter—for example, tallying how many of a set of boolean conditions hold—but if you catch yourself writing this kind of "clever" code, stop for a moment and think: **is there a clearer way to write it? Readability usually matters more than clever brevity.**

## sizeof Demystified — How Much Memory Does a Type Actually Take

We have been using `sizeof` all along without formally introducing it. `sizeof` is an operator in C++ (not a function), and it can compute the number of bytes a type or variable occupies at **compile time**. That means it carries no runtime overhead at all—the compiler simply embeds the result into the code as a constant.

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

Remember these numbers—**not by rote memorization, of course; you can always write a little program to test them. We are learning programming. This is nothing more than a task: verify the sizes of our types. Don't memorize it—think about how to get it done!**

What truly deserves to be carved into your brain is this insight: **a type's size is not a free-for-all; it directly affects your program's memory layout and performance.** On an embedded system, SRAM may be only a few dozen KB; there, the choice between `int` and `int8_t` is no longer a matter of style preference, but of whether the bytes can be spared at all.

## The Wisdom of Choosing — Which Type to Use When

That is a lot of types covered, so how do you actually choose? Here are a few rules of thumb from practice. They will not cover every scenario, but they will at least get you within arm's reach of the right call.

For general-purpose integers, use `int`. It is the type the compiler "likes" best—operations on it are usually the fastest, and code generation for it is the most optimized. Loop variables, array indices, simple counters—`int` for all of them. Only consider switching to `long long` or `unsigned` when you are certain the data range will exceed `int`'s limits (roughly ±2.1 billion), or when you need to handle unsigned values.

When the size must be pinned down, use the fixed-width types from `<cstdint>`. Parsing binary files, network communication protocols, operating hardware registers, serializing data structures—any requirement of the "bytes N through M must be an integer of exactly this size" kind should use types like `int32_t` and `uint16_t`. Do not assume `int` is necessarily 32 bits; nearly every platform today makes it so, but the standard makes no such guarantee.

For floating-point arithmetic, use `double`, unless you have a concrete reason to choose `float`. `double` offers more than twice the precision of `float`, and on modern CPUs there is practically no speed difference between the two (both have hardware FPU support). Only when storage is extremely tight—say, an embedded device that must store large amounts of measurement data—is it worth sacrificing precision for `float`'s 4 bytes. As for `long double`, unless you are doing extremely high-precision scientific computation, you will essentially never need it.

For boolean logic, use `bool`; do not press `int` into service as a boolean. The C era did have the "zero is false, nonzero is true" habit (and of course, C23 now has a proper, respectable `bool` too—friends who didn't know, go try it!), but in C++ we have a real `bool` type. Using it makes your intent clearer and lets the compiler do better type checking.

## Run Online

Run it for real on your platform and see exactly how many bytes each type takes:

<OnlineCompilerDemo
  title="Basic Data Types: sizeof and Ranges at a Glance"
  source-path="code/examples/vol1/02_basic_types.cpp"
  description="Run online and observe the sizes and value ranges of the various C++ basic types on your platform."
  allow-run
/>

## Try It Yourself

### Exercise 1: A Complete Size and Range Report

Write a program that prints, for every basic integer type (`short`, `int`, `long`, `long long` plus their `unsigned` versions, and `int8_t`, `int16_t`, `int32_t`, `int64_t` plus their `unsigned` versions), its `sizeof` together with the minimum and maximum obtained via `std::numeric_limits`. Format the output so the result reads at a glance.

::: details Reference answer

**main.cpp**

```cpp
#include <iostream>
#include <limits>
#include <cstdint>

int main()
{
    std::cout << "=== 完整的大小和范围报告 ===" << std::endl;
    std::cout << std::endl;

    std::cout << "--- 基本整数类型sizeof汇总 ---" << std::endl;
    std::cout << "short:          sizeof " << sizeof(short) << " 字节, "
              << "min " << std::numeric_limits<short>::min() << ", "
              << "max " << std::numeric_limits<short>::max() << std::endl;
    std::cout << "unsigned short: sizeof " << sizeof(unsigned short) << " 字节, "
              << "min " << std::numeric_limits<unsigned short>::min() << ", "
              << "max " << std::numeric_limits<unsigned short>::max() << std::endl;
    std::cout << "int:            sizeof " << sizeof(int) << " 字节, "
              << "min " << std::numeric_limits<int>::min() << ", "
              << "max " << std::numeric_limits<int>::max() << std::endl;
    std::cout << "unsigned int:   sizeof " << sizeof(unsigned int) << " 字节, "
              << "min " << std::numeric_limits<unsigned int>::min() << ", "
              << "max " << std::numeric_limits<unsigned int>::max() << std::endl;
    std::cout << "long:           sizeof " << sizeof(long) << " 字节, "
              << "min " << std::numeric_limits<long>::min() << ", "
              << "max " << std::numeric_limits<long>::max() << std::endl;
    std::cout << "unsigned long:  sizeof " << sizeof(unsigned long) << " 字节, "
              << "min " << std::numeric_limits<unsigned long>::min() << ", "
              << "max " << std::numeric_limits<unsigned long>::max() << std::endl;
    std::cout << "long long:      sizeof " << sizeof(long long) << " 字节, "
              << "min " << std::numeric_limits<long long>::min() << ", "
              << "max " << std::numeric_limits<long long>::max() << std::endl;
    std::cout << "unsigned long long: sizeof " << sizeof(unsigned long long) << " 字节, "
              << "min " << std::numeric_limits<unsigned long long>::min() << ", "
              << "max " << std::numeric_limits<unsigned long long>::max() << std::endl;

    std::cout << std::endl;
    std::cout << "--- <cstdint> 固定宽整数类型 ---" << std::endl;
    std::cout << "int8_t:         sizeof " << sizeof(int8_t) << " 字节, "
              << "min " << (int)std::numeric_limits<int8_t>::min() << ", "
              << "max " << (int)std::numeric_limits<int8_t>::max() << std::endl;
    std::cout << "uint8_t:        sizeof " << sizeof(uint8_t) << " 字节, "
              << "min " << (unsigned)std::numeric_limits<uint8_t>::min() << ", "
              << "max " << (unsigned)std::numeric_limits<uint8_t>::max() << std::endl;
    std::cout << "int16_t:        sizeof " << sizeof(int16_t) << " 字节, "
              << "min " << std::numeric_limits<int16_t>::min() << ", "
              << "max " << std::numeric_limits<int16_t>::max() << std::endl;
    std::cout << "uint16_t:       sizeof " << sizeof(uint16_t) << " 字节, "
              << "min " << std::numeric_limits<uint16_t>::min() << ", "
              << "max " << std::numeric_limits<uint16_t>::max() << std::endl;
    std::cout << "int32_t:        sizeof " << sizeof(int32_t) << " 字节, "
              << "min " << std::numeric_limits<int32_t>::min() << ", "
              << "max " << std::numeric_limits<int32_t>::max() << std::endl;
    std::cout << "uint32_t:       sizeof " << sizeof(uint32_t) << " 字节, "
              << "min " << std::numeric_limits<uint32_t>::min() << ", "
              << "max " << std::numeric_limits<uint32_t>::max() << std::endl;
    std::cout << "int64_t:        sizeof " << sizeof(int64_t) << " 字节, "
              << "min " << std::numeric_limits<int64_t>::min() << ", "
              << "max " << std::numeric_limits<int64_t>::max() << std::endl;
    std::cout << "uint64_t:       sizeof " << sizeof(uint64_t) << " 字节, "
              << "min " << std::numeric_limits<uint64_t>::min() << ", "
              << "max " << std::numeric_limits<uint64_t>::max() << std::endl;

    return 0;
}

```

Compile and run:

```bash
g++ -std=c++20 -Wall -Wextra main.cpp -o main && ./main
```

Output:

```text
=== 完整的大小和范围报告 ===

--- 基本整数类型sizeof汇总 ---
short:          sizeof 2 字节, min -32768, max 32767
unsigned short: sizeof 2 字节, min 0, max 65535
int:            sizeof 4 字节, min -2147483648, max 2147483647
unsigned int:   sizeof 4 字节, min 0, max 4294967295
long:           sizeof 8 字节, min -9223372036854775808, max 9223372036854775807
unsigned long:  sizeof 8 字节, min 0, max 18446744073709551615
long long:      sizeof 8 字节, min -9223372036854775808, max 9223372036854775807
unsigned long long: sizeof 8 字节, min 0, max 18446744073709551615

--- <cstdint> 固定宽整数类型 ---
int8_t:         sizeof 1 字节, min -128, max 127
uint8_t:        sizeof 1 字节, min 0, max 255
int16_t:        sizeof 2 字节, min -32768, max 32767
uint16_t:       sizeof 2 字节, min 0, max 65535
int32_t:        sizeof 4 字节, min -2147483648, max 2147483647
uint32_t:       sizeof 4 字节, min 0, max 4294967295
int64_t:        sizeof 8 字节, min -9223372036854775808, max 9223372036854775807
uint64_t:       sizeof 8 字节, min 0, max 18446744073709551615
```

> ℹ️ **Platform note**: the above is the result on a typical 64-bit Linux platform. On Windows, `long` and `unsigned long` are 4 bytes (`long` spans `-2147483648` ~ `2147483647`, and `unsigned long` spans `0` ~ `4294967295`); the remaining types are unchanged.

:::

### Exercise 2: Predict the sizeof Results

Before looking at the answer, first predict what each of these expressions yields on your platform, then write a program to verify: `sizeof('A')`, `sizeof(true)`, `sizeof(3.14)`, `sizeof(3.14f)`, `sizeof(3.14L)`. Extra challenge: write a `.c` file compiled as a C program and a `.cpp` file compiled as a C++ program, both printing `sizeof('A')`, and observe how the results differ. Hint: in C++, the character literal `'A'` has type `char` (`sizeof` is 1), whereas in C the character constant `'A'` has type `int` (`sizeof` is usually 4)—a subtle but important difference between the two languages.

::: details Reference answer

**main.cpp**

```cpp
#include <iostream>

int main()
{
    std::cout << "=== C++: sizeof 表达式验证 ===" << std::endl;
    std::cout << "sizeof('A')   = " << sizeof('A') << std::endl;
    std::cout << "sizeof(true)  = " << sizeof(true) << std::endl;
    std::cout << "sizeof(3.14)  = " << sizeof(3.14) << std::endl;
    std::cout << "sizeof(3.14f) = " << sizeof(3.14f) << std::endl;
    std::cout << "sizeof(3.14L) = " << sizeof(3.14L) << std::endl;
    return 0;
}

```

Compile and run:

```bash
g++ -std=c++20 -Wall -Wextra main.cpp -o main && ./main
```

Output:

```text
=== C++: sizeof 表达式验证 ===
sizeof('A')   = 1
sizeof(true)  = 1
sizeof(3.14)  = 8
sizeof(3.14f) = 4
sizeof(3.14L) = 16
```

```c
#include <stdio.h>
#include <stdbool.h>

int main(void)
{
    printf("=== C: sizeof 表达式验证 ===\n");
    printf("sizeof('A')   = %zu\n", sizeof('A'));
    printf("sizeof(true)  = %zu\n", sizeof(true));
    printf("sizeof(3.14)  = %zu\n", sizeof(3.14));
    printf("sizeof(3.14f) = %zu\n", sizeof(3.14f));
    printf("sizeof(3.14L) = %zu\n", sizeof(3.14L));
    return 0;
}

```

```bash
gcc -std=c17 -Wall -Wextra -pedantic main.c -o main && ./main
```

Output:

```text
=== C: sizeof 表达式验证 ===
sizeof('A')   = 4
sizeof(true)  = 4
sizeof(3.14)  = 8
sizeof(3.14f) = 4
sizeof(3.14L) = 16
```

`sizeof(char)` is always 1 byte, but the type of the character literal `'A'` differs between C and C++, and the type of the boolean literal `true` also shifts with the language and standard version: in C++, `true` is a `bool` literal, so `sizeof(true)` = `sizeof(bool)` = 1; in C with `<stdbool.h>` (C17 and earlier), `true` is a macro that expands to the `int` literal `1`, so `sizeof(true)` = `sizeof(int)` = 4; and in C23, `true` / `false` become genuine keywords of type `bool` (that is, `_Bool`), so `sizeof(true)` comes back to 1.

:::

### Exercise 3: Experience the Floating-Point Precision Trap

Write a program that starts a `float` variable at 0, adds 0.1 each time, ten times total, and then checks whether the result equals 1.0. Then do the same thing with `double`. Observe the difference in behavior between the two, and use `std::setprecision` to print the exact value after each accumulation step.

::: details Reference answer

**main.cpp**

```cpp
#include <iostream>
#include <iomanip>
//std::setw  sets the field width; it only affects the next output and expires once used

//std::setprecision sets the precision; it stays in effect until a later call changes it
//In the default mode it means significant digits (20 significant digits).
//Combined with std::fixed, it becomes digits after the decimal point: std::fixed << std::setprecision(2) → 3.14

int main()
{
    std::cout << "=== float: 从 0 开始，每次加 0.1，共 10 次 ===" << std::endl;
    float f = 0.0f;
    for (int i = 1; i <= 10; ++i)
    {
        f += 0.1f;
        std::cout << std::setw(2) << i << " 次后 = "
                  << std::setprecision(20) << f << std::endl;
    }
    std::cout << "最终 f == 1.0f ?  " << (f == 1.0f ? "true" : "false")
              << "   差值 = " << (f - 1.0f) << std::endl;
    std::cout << std::endl;

    std::cout << "=== double: 从 0 开始，每次加 0.1，共 10 次 ===" << std::endl;
    double d = 0.0;
    for (int i = 1; i <= 10; ++i)
    {
        d += 0.1;
        std::cout << std::setw(2) << i << " 次后 = "
                  << std::setprecision(20) << d << std::endl;
    }
    std::cout << "最终 d == 1.0 ?  " << (d == 1.0 ? "true" : "false")
              << "   差值 = " << std::setprecision(20) << (d - 1.0) << std::endl;

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++20 -Wall -Wextra main.cpp -o main && ./main
```

Output:

```text
=== float: 从 0 开始，每次加 0.1，共 10 次 ===
 1 次后 = 0.10000000149011611938
 2 次后 = 0.20000000298023223877
 3 次后 = 0.30000001192092895508
 4 次后 = 0.40000000596046447754
 5 次后 = 0.5
 6 次后 = 0.60000002384185791016
 7 次后 = 0.70000004768371582031
 8 次后 = 0.80000007152557373047
 9 次后 = 0.90000009536743164062
10 次后 = 1.0000001192092895508
最终 f == 1.0f ?  false   差值 = 1.1920928955078125e-07

=== double: 从 0 开始，每次加 0.1，共 10 次 ===
 1 次后 = 0.10000000000000000555
 2 次后 = 0.2000000000000000111
 3 次后 = 0.30000000000000004441
 4 次后 = 0.4000000000000000222
 5 次后 = 0.5
 6 次后 = 0.5999999999999999778
 7 次后 = 0.69999999999999995559
 8 次后 = 0.79999999999999993339
 9 次后 = 0.89999999999999991118
10 次后 = 0.99999999999999988898
最终 d == 1.0 ?  false   差值 = -1.1102230246251565404e-16
```

:::
