---
title: 'Data Type Basics: Integers and Memory'
description: Understanding the C integer family from scratch, including the differences
  between signed and unsigned types, fixed-width types, and the `sizeof` operator,
  to build a solid foundation in the type system for future learning.
chapter: 1
order: 2
tags:
- host
- cpp-modern
- beginner
- 入门
- 基础
difficulty: beginner
platform: host
reading_time_minutes: 15
cpp_standard:
- 11
prerequisites:
- 程序结构与编译基础
translation:
  source: documents/vol1-fundamentals/c_tutorials/02A-data-types-basics.md
  source_hash: 2d46ab4d2c6c8c1703c01edf433403680b22d1726e3c6f5c3fc49a1d0df04528
  translated_at: '2026-04-20T03:12:30.456138+00:00'
  engine: anthropic
  token_count: 1971
---
# Data Type Basics: Integers and Memory

If you have used Python before, you might remember that writing `x = 42` just works — you don't need to tell Python whether `x` is an integer or a decimal. The interpreter figures it out on its own. But in C, the rules change: when a variable is born, we must explicitly tell the compiler exactly what type it is. This might seem like unnecessary busywork at first glance, but this act of "declaring a type" is the foundation of C's performance — because the compiler knows exactly how much memory each variable occupies and how the data is stored, it can generate the most efficient machine code.

The ultimate goal of this entire C tutorial is to lay the groundwork for learning C++, and C++ makes extensive improvements on top of C's type system. Once we understand "where C's types are prone to problems," learning "how C++ solves these problems" later will feel completely natural. So let's start by thoroughly mastering C's type system, beginning with the most fundamental topic: integers.

> **Learning Objectives**
> After completing this chapter, you will be able to:
>
> - [ ] Understand the hierarchy of C's integer family and the guaranteed ranges of each type
> - [ ] Distinguish between the storage methods and use cases for signed and unsigned integers
> - [ ] Proficiently use the fixed-width types provided by `stdint.h`
> - [ ] Use the `sizeof` operator to measure the memory footprint of types and variables

## Environment Setup

We will run all of the following experiments in this environment:

- Platform: Linux x86\_64 (WSL2 is also fine)
- Compiler: GCC 13+ or Clang 17+
- Compiler flags: `-std=c17 -Wall -Wextra -pedantic`

All code is standard C and does not depend on any platform-specific APIs. If you are using macOS or MinGW on Windows, most experiments will work, though the byte sizes of certain types might differ slightly — we will discuss this issue in detail later.

## Step 1 — Understanding How C Stores Integers

### Understanding Data Types with "Boxes"

We can think of memory as a long row of numbered boxes. Each box can hold one byte of data. When you declare a variable, the compiler allocates a series of consecutive boxes for you, and the variable name is simply the label attached to those boxes. A **data type** determines two things: how many boxes a variable occupies, and how the 0s and 1s inside those boxes are interpreted.

Here is the most intuitive example: `int` occupies four boxes on most modern platforms (4 bytes = 32 bits) and can store integers in a range of roughly plus or minus 2.1 billion. `char` occupies only one box (1 byte = 8 bits), which can store a much smaller range of numbers but saves memory.

### The Complete Integer Family

C provides five standard integer types, ordered from smallest to largest representable range:

| Type | Standard Guaranteed Minimum Bits | Common Actual Bits (32/64-bit Platforms) |
|------|----------------------------------|-------------------------------------------|
| `char` | 8 bits | 8 bits |
| `short` | 16 bits | 16 bits |
| `int` | 16 bits | 32 bits |
| `long` | 32 bits | 32 bits (Windows) / 64 bits (Linux/macOS) |
| `long long` | 64 bits | 64 bits |

Note a key point: the C standard only specifies the **minimum guaranteed bits** for each type. A compiler may provide more, but never less. This is why the same code might behave differently across platforms — the `long` you write is 32 bits on Windows but 64 bits on Linux. If your program relies on the exact width of `long`, you will most likely run into issues when porting across platforms.

> ⚠️ **Pitfall Warning**
> The width of `long` varies across operating systems — 32 bits on Windows, 64 bits on Linux/macOS. If your code requires precise control over integer width, never use `long`. Instead, use the fixed-width types we will discuss shortly.

Another detail worth noting: `sizeof(char)` is always exactly 1, as mandated by the standard. However, on some exotic DSP platforms, a "byte" might not be 8 bits. On the x86 and ARM platforms we use daily, a byte is always 8 bits, so we don't need to worry about this for now.

### Let's Verify — How Many Bytes Does Each Type Actually Occupy?

Let's write a small program to see exactly how large each type is on your machine:

```c
#include <stdio.h>

int main(void)
{
    printf("char:      %zu 字节\n", sizeof(char));
    printf("short:     %zu 字节\n", sizeof(short));
    printf("int:       %zu 字节\n", sizeof(int));
    printf("long:      %zu 字节\n", sizeof(long));
    printf("long long: %zu 字节\n", sizeof(long long));

    return 0;
}
```

Compile and run:

```bash
gcc -Wall -Wextra -std=c17 sizeof_demo.c -o sizeof_demo && ./sizeof_demo
```

Here are the results on my Linux x86\_64 machine:

```text
char:      1 字节
short:     2 字节
int:       4 字节
long:      8 字节
long long: 8 字节
```

If you run this on Windows, the `sizeof(long)` line will likely show `4` — this is the cross-platform difference we just mentioned.

## Step 2 — Signed or Unsigned?

### What Does "Signed" Mean?

Every member of the integer family (except for `char`, which is a special case) has two variants: `signed` and `unsigned`. This "sign" refers to the positive or negative sign — signed types can store both positive and negative numbers, while unsigned types can only store non-negative numbers, but the representable range doubles for the same amount of memory.

To use an analogy: imagine eight light bulbs lined up in a row. If we agree that "the first bulb being lit means negative," the remaining seven bulbs can represent a range of -128 to 127. If we don't need negative numbers, all eight bulbs are used to represent the number, and the range becomes 0 to 255.

```c
int signed_num = -42;           // 有符号，可以存负数
unsigned int unsigned_num = 42; // 无符号，只能存非负数
```

### The Sign Issue with char

`char` is special — the standard does not specify whether it is signed or unsigned; this depends on the compiler. On ARM platforms, `char` is typically unsigned, while on x86 it is typically signed. This difference might seem insignificant, but if you use `char` as a "small integer," you might run into cross-platform issues:

```c
char c = 200;           // 如果 char 是有符号的，实际存储的是 -56
unsigned char uc = 200; // 无论平台如何，值都是 200
```

> ⚠️ **Pitfall Warning**
> When you need a "small integer" (in the range 0–255), use `uint8_t`, not `char`. The signedness of `char` depends on the compiler and platform, and using it as an integer will inevitably cause problems.

### Unsigned Integer Wrap-Around

Unsigned integers have a clear rule: on overflow, they **wrap around**. In other words, if you store an unsigned number and add 1 beyond its maximum value, it restarts from 0. For example, the maximum value of an 8-bit unsigned number is 255, so `255U + 1` becomes `0`.

However, signed integer overflow is dangerous — it is **undefined behavior (UB)**. Simply put, the standard dictates "you must not do this." If your program does it anyway, the compiler can handle it in any way it chooses — it might seem to work fine, it might produce incorrect results, or it might crash outright. What's more insidious is that during optimization, the compiler might assume "overflow never happens" and silently remove the overflow-checking code you wrote. We will dive deep into UB in the chapter on operators.

> ⚠️ **Pitfall Warning**
> Signed integer overflow is undefined behavior. The result of `INT_MAX + 1` is unpredictable; it does not "wrap around to a negative number." Never rely on the behavior of signed overflow.

## Step 3 — What About Cross-Platform? Fixed-Width Types to the Rescue

### What's the Problem?

Earlier we saw the issue where `long` is 32 bits on Windows but 64 bits on Linux. If you are writing a program that requires precise control over data width — for example, when interfacing with hardware and needing to ensure a variable is exactly 32 bits — using `long` or `int` directly is unsafe because their actual widths vary by platform.

The solution provided by the C99 standard is the `stdint.h` header. It provides a set of type aliases whose names directly include their bit widths:

```c
#include <stdint.h>

int8_t   i8  = -128;          // 精确 8 位有符号
uint8_t  u8  = 255;           // 精确 8 位无符号
int16_t  i16 = -32768;        // 精确 16 位有符号
uint16_t u16 = 65535;         // 精确 16 位无符号
int32_t  i32 = -2147483648;   // 精确 32 位有符号
uint32_t u32 = 4294967295U;   // 精确 32 位无符号
int64_t  i64 = 9223372036854775807LL;  // 精确 64 位有符号
uint64_t u64 = 18446744073709551615ULL; // 精确 64 位无符号
```

The beauty of these types is "what you see is what you get" — `int32_t` is exactly 32 bits on any platform that supports it, and `uint8_t` is always an 8-bit unsigned integer. They are virtually mandatory in embedded development and cross-platform code.

Note that the standard does not guarantee all platforms provide every exact-width type. For example, certain DSPs might lack 8-bit addressing capability, meaning `uint8_t` would not exist — it would result in a compilation error. However, on the x86 and ARM platforms we use daily, all exact-width types are available.

### size_t — A Type Found Everywhere in the Standard Library

Before moving on, we need to get acquainted with a type that appears everywhere in the standard library: `size_t`. It is the return type of the `sizeof` operator, and it is the type used by functions like `strlen` and `malloc`. `size_t` is unsigned, and its size varies by platform — 32 bits on 32-bit platforms, 64 bits on 64-bit platforms.

```c
#include <stddef.h>

size_t len = 100;       // 足以表示任何对象的大小
```

We will frequently interact with `size_t` going forward. For now, just remember one thing: **when you need to represent a "count" or "size," `size_t` is the right choice**.

### Let's Verify — The Size of Fixed-Width Types

```c
#include <stdio.h>
#include <stdint.h>

int main(void)
{
    printf("int8_t:    %zu 字节\n", sizeof(int8_t));
    printf("uint8_t:   %zu 字节\n", sizeof(uint8_t));
    printf("int32_t:   %zu 字节\n", sizeof(int32_t));
    printf("uint32_t:  %zu 字节\n", sizeof(uint32_t));
    printf("int64_t:   %zu 字节\n", sizeof(int64_t));
    printf("size_t:    %zu 字节\n", sizeof(size_t));

    return 0;
}
```

Compile and run:

```bash
gcc -Wall -Wextra -std=c17 stdint_demo.c -o stdint_demo && ./stdint_demo
```

Output:

```text
int8_t:    1 字节
uint8_t:   1 字节
int32_t:   4 字节
uint32_t:  4 字节
int64_t:   8 字节
size_t:    8 字节
```

Great, the byte size of each type matches our expectations.

## Step 4 — sizeof: The Ruler for Measuring Memory

### sizeof Is Not a Function

`sizeof` is a compile-time operator, not a function. It completes its calculation at compile time, so there is zero runtime overhead. Its return type is `size_t`, and we use the `%zu` format specifier when printing it.

```c
int x = 42;
printf("%zu\n", sizeof(x));     // 变量：输出 4（在 int 是 4 字节的平台上）
printf("%zu\n", sizeof(int));   // 类型名：同样输出 4
```

`sizeof` has a classic use case with arrays — calculating the number of elements:

```c
int arr[] = {10, 20, 30, 40, 50};
size_t count = sizeof(arr) / sizeof(arr[0]);  // 20 / 4 = 5
printf("数组有 %zu 个元素\n", count);
```

The principle is simple: `sizeof(arr)` is the total number of bytes occupied by the entire array, `sizeof(arr[0])` is the number of bytes for a single element, and dividing the two gives the number of elements.

> ⚠️ **Pitfall Warning**
> This trick of "using `sizeof` to calculate element count" **only works within the scope where the array is defined**. Once an array is passed to a function, it decays into a pointer, and `sizeof(arr)` returns the size of the pointer (4 or 8), not the size of the array:

```c
void bad_sizeof(int arr[])
{
    // arr 在这里已经是指针了！
    printf("%zu\n", sizeof(arr));  // 输出 4 或 8（指针大小），不是数组大小
}
```

We will explore the mechanism of array-to-pointer decay in detail in the chapter on pointers. For now, just remember the takeaway: "arrays become pointers when passed to functions."

## Bridging to C++

C++ fully inherits all of C's integer types, while making several important improvements to make the type system safer.

First, C++11 introduced the `<cstdint>` header (note the absence of the `.h` suffix), which provides the same functionality as C's `<stdint.h>`, but places the types inside the `std` namespace. Second, C++'s `{}` initialization prohibits "narrowing conversions" — you cannot initialize a variable with a value that falls outside the target type's range:

```cpp
int x = 3.14;      // C/C++ 都允许，隐式截断为 3（编译器可能警告）
int y{3.14};        // C++ 编译错误！窄化转换被禁止
uint8_t z{1000};    // C++ 编译错误！1000 超出 uint8_t 范围
```

This feature is highly effective at eliminating an entire class of implicit conversion bugs. If you write C++ code in the future, we strongly recommend building the habit of using `{}` initialization.

## Summary

At this point, we have a clear understanding of the basic mechanisms of integer storage in C. The core takeaways can be summarized in a few sentences: the C standard only specifies a minimum guaranteed bit count for each integer type, and actual widths vary by platform, so cross-platform code should use the fixed-width types from `stdint.h`. The difference between signed and unsigned is not simply "whether it can store negative numbers" — their overflow behaviors are completely different. Unsigned wrap-around is well-defined, while signed overflow is undefined behavior. `sizeof` is our tool for measuring memory at compile time, and when combined with arrays, it can calculate element counts, but we must be careful that arrays decay into pointers when passed to functions.

This raises the next question: we've covered integers, but what about decimals? How are characters stored? Once a variable is declared, can we protect it from accidental modification? These are the topics we will discuss in the next chapter.

## Exercises

### Exercise 1: Type Detector

Write a program that prints the `sizeof` value for all of the following types, and verify against the standard that they meet the minimum guarantees:

```c
// 请补全代码，对以下所有类型打印 sizeof
// char, short, int, long, long long
// int8_t, uint8_t, int32_t, uint32_t, int64_t
// size_t
```

Hint: you can use a macro to reduce code repetition.

### Exercise 2: Overflow Observation

Perform overflow experiments on both a signed `int32_t` and an unsigned `uint32_t`:

```c
#include <stdio.h>
#include <limits.h>

int main(void)
{
    int i = INT_MAX;
    unsigned int u = UINT_MAX;

    printf("INT_MAX  = %d,  INT_MAX + 1  = %d\n", i, i + 1);
    printf("UINT_MAX = %u, UINT_MAX + 1 = %u\n", u, u + 1);

    return 0;
}
```

Compile and run, and observe the behavioral differences between the two. Then recompile with the `-fsanitize=undefined` flag and see what changes.

## References

- [cppreference: C integer constants](https://en.cppreference.com/w/c/language/integer_constant)
- [cppreference: Fixed-width integer types](https://en.cppreference.com/w/c/types/integer)
- [Summary of C/C++ integer rules](https://www.nayuki.io/page/summary-of-c-cpp-integer-rules)
