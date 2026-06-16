---
chapter: 1
cpp_standard:
- 11
description: Understanding the C integer family from scratch, the differences between
  signed and unsigned types, fixed-width types, and the sizeof operator, to build
  a foundation for the type system in future studies.
difficulty: beginner
order: 2
platform: host
prerequisites:
- 程序结构与编译基础
reading_time_minutes: 12
tags:
- host
- cpp-modern
- beginner
- 入门
- 基础
title: 'Data Type Basics: Integers and Memory'
translation:
  source: documents/vol1-fundamentals/c_tutorials/02A-data-types-basics.md
  source_hash: cfd8e7632c4612c61026b8b55e9a760ca0ed50aac4496ad86814d383ee381acf
  translated_at: '2026-06-16T03:32:50.237062+00:00'
  engine: anthropic
  token_count: 1967
---
# Data Type Basics: Integers and Memory

If you have used Python before, you might remember that writing `x = 42` is all it takes—you don't need to tell Python whether this variable is an integer or a decimal; the interpreter figures it out. In C, however, the rules change: when every variable is born, we must explicitly tell the compiler, "What type is this guy?" At first glance, this might seem redundant, but this act of "declaring types" is actually the foundation of C's performance power—because the compiler knows exactly how much memory each variable occupies and how the data is stored, it can generate the most efficient machine code.

The ultimate goal of this entire C tutorial is to pave the way for learning C++, and C++ has done significant work to strengthen the type system of C. Once you understand "where C's types are prone to problems," learning "how C++ solves these problems" later will feel very natural. So, let's thoroughly master C's type system, starting with the most basic integers.

> **Learning Objectives**
> After completing this chapter, you will be able to:
>
> - [ ] Understand the hierarchy of the C integer family and the guaranteed ranges of each type.
> - [ ] Distinguish between the storage methods and use cases for signed and unsigned integers.
> - [ ] Skillfully use fixed-width types provided by `stdint.h`.
> - [ ] Use the `sizeof` operator to measure the memory size of types and variables.

## Environment Setup

We will conduct all subsequent experiments in the following environment:

- Platform: Linux x86_64 (WSL2 is also acceptable)
- Compiler: GCC 13+ or Clang 17+
- Compiler flags: `-std=c17 -Wall -Wextra`

All code is standard C and does not rely on any platform-specific APIs. If you are using macOS or MinGW on Windows, most experiments will run, although the byte size of certain types might vary slightly—we will discuss this issue specifically later on.

## Step 1 — Figure Out How C Stores Integers

### Understanding Data Types with "Boxes"

We can imagine memory as a long row of numbered boxes. Each box can hold one byte of data. When you declare a variable, the compiler allocates several consecutive boxes for you, and the variable name is the label attached to these boxes. The **data type** determines two things: how many boxes this variable occupies, and how the 0s and 1s inside the boxes are interpreted.

Here is the most intuitive example: `int` occupies 4 boxes (4 bytes = 32 bits) on most modern platforms and can store integers in the range of approximately positive and negative 2.1 billion. `char` occupies only 1 box (1 byte = 8 bits), so the range of numbers it can store is much smaller, but it saves space.

### The Integer Family Portrait

The C language provides five standard integer types, arranged from smallest to largest by representable range:

| Type | Minimum Guaranteed Bits (Standard) | Common Actual Bits (32/64-bit platforms) |
|------|-----------------------------------|------------------------------------------|
| `char` | 8 bits | 8 bits |
| `short` | 16 bits | 16 bits |
| `int` | 16 bits | 32 bits |
| `long` | 32 bits | 32 bits (Windows) / 64 bits (Linux/macOS) |
| `long long` | 64 bits | 64 bits |

Note a key point: the C standard only specifies the **minimum guaranteed bits** for each type; compilers may provide more but cannot provide less. This is why the same code might behave differently on different platforms—the `long` you write is 32-bit on Windows and 64-bit on Linux. If your program relies on the precise width of `long`, you will likely run into trouble when porting across platforms.

> ⚠️ **Pitfall Warning**
> The width of `long` varies across different operating systems—32-bit on Windows, 64-bit on Linux/macOS. If your code requires precise control over integer width, never use `long`; use the fixed-width types we discuss later.

Another detail worth noting: `sizeof(char)` is always equal to 1, as mandated by the standard. However, on some exotic DSP platforms, a "byte" might not be 8 bits. On the x86 and ARM platforms we use daily, a byte is always 8 bits, so we don't need to worry about this for now.

### Let's Verify — How Many Bytes Does Each Type Actually Occupy?

Let's write a small program to see exactly how large each type is on your machine:

```c
#include <stdio.h>

int main(void) {
    printf("char      : %zu byte(s)\n", sizeof(char));
    printf("short     : %zu byte(s)\n", sizeof(short));
    printf("int       : %zu byte(s)\n", sizeof(int));
    printf("long      : %zu byte(s)\n", sizeof(long));
    printf("long long : %zu byte(s)\n", sizeof(long long));
    return 0;
}
```

Compile and run:

```text
gcc -std=c17 -Wall -Wextra sizes.c -o sizes
./sizes
```

My results on Linux x86_64:

```text
char      : 1 byte(s)
short     : 2 byte(s)
int       : 4 byte(s)
long      : 8 byte(s)
long long : 8 byte(s)
```

If you run this on Windows, the `long` line will likely be `4 byte(s)`—this is the cross-platform difference we just mentioned.

## Step 2 — Signed or Unsigned?

### What Does "Signed" Mean?

Every member of the integer family (except `char`, which is a bit special) has two variants: `signed` (signed) and `unsigned` (unsigned). This "signature" refers to the positive or negative sign—signed types can store positive and negative numbers, while unsigned types can only store non-negative numbers, but for the same memory size, the representable range doubles.

To use an analogy: if we have 8 light bulbs in a row, and we agree that "the first bulb being lit means a negative sign," the remaining 7 bulbs can represent a number range from -128 to 127. If we don't need a negative sign, all 8 bulbs are used to represent the number, and the range becomes 0 to 255.

```c
#include <stdio.h>
#include <limits.h>

int main(void) {
    // Signed 8-bit integer: -128 to 127
    signed char sc = -5;
    // Unsigned 8-bit integer: 0 to 255
    unsigned char uc = 250;

    printf("Signed char   : %d\n", sc);
    printf("Unsigned char : %u\n", uc);

    return 0;
}
```

### The Sign Issue with `char`

`char` is quite special—the standard does not specify whether it is signed or unsigned; this depends on the compiler. On ARM platforms, `char` is usually unsigned; on x86, it is usually signed. This difference might seem insignificant, but if you use `char` as a "small integer," you might run into cross-platform issues:

```c
#include <stdio.h>

int main(void) {
    char c = 0xFF; // Binary: 11111111

    if (c == -1) {
        printf("char is signed on this platform\n");
    } else if (c == 255) {
        printf("char is unsigned on this platform\n");
    }

    return 0;
}
```

> ⚠️ **Pitfall Warning**
> When you need a "small integer" (range 0~255), please use `unsigned char`, not `char`. The signedness of `char` depends on the compiler and platform, and using it as an integer will inevitably cause problems.

### Wrapping of Unsigned Integers

Unsigned integers have a clear rule: they **wrap around** on overflow. This means if you store an unsigned number and add 1 exceeding its maximum value, it restarts from 0. For example, the maximum value of an 8-bit unsigned number is 255, so `255 + 1` becomes 0.

However, signed integer overflow is dangerous—it is **Undefined Behavior** (UB). Simply put: the standard states "you are not allowed to do this." If your program does this, the compiler can handle it in any way—it might look normal, it might calculate the wrong result, or it might crash immediately. More insidiously, during optimization, the compiler might assume "overflow never happens" and silently delete your overflow check code. We will expand on UB in the article on operators.

> ⚠️ **Pitfall Warning**
> Signed integer overflow is undefined behavior. The result of `INT_MAX + 1` is unpredictable, not "wrap to negative." Never rely on signed overflow behavior.

## Step 3 — Cross-Platform? Fixed-Width Types to the Rescue

### Where the Problem Lies

We just saw the issue where `long` is 32-bit on Windows and 64-bit on Linux. If you are writing a program that requires precise control over data width—for example, when dealing with hardware, you need to ensure a variable is exactly 32 bits—using `long` or `int` directly is unsafe because their actual width varies by platform.

The solution provided by the C99 standard is the `<stdint.h>` header file. It provides a set of type aliases that directly include the bit count in their names:

```c
#include <stdio.h>
#include <stdint.h>

int main(void) {
    // Exactly 8 bits, guaranteed
    int8_t  a = 100;
    uint8_t b = 200;

    // Exactly 32 bits, guaranteed
    int32_t  c = 1000000;
    uint32_t d = 3000000;

    printf("int8_t  size: %zu bytes\n", sizeof(int8_t));
    printf("uint32_t size: %zu bytes\n", sizeof(uint32_t));

    return 0;
}
```

The benefit of these types is "what you see is what you get"—`int32_t` is exactly 32 bits on any platform that supports it, and `uint8_t` is always 8-bit unsigned. They are almost essential in embedded and cross-platform code.

Note that the standard does not guarantee that all platforms provide all exact-width types. For example, some DSPs might not have 8-bit addressing capability, so `uint8_t` wouldn't exist—the compiler will error out directly. However, on the x86 and ARM platforms we use daily, all exact-width types are available.

### `size_t` — The Guy Found Everywhere in the Standard Library

Before proceeding, we need to meet a type that appears everywhere in the standard library: `size_t`. It is the return type of the `sizeof` operator and is used by functions like `malloc`, `strlen`, etc. `size_t` is unsigned and varies in size by platform—32-bit on 32-bit platforms, 64-bit on 64-bit platforms.

```c
#include <stdio.h>

int main(void) {
    size_t array_size = 10;
    printf("Size of size_t: %zu bytes\n", sizeof(size_t));
    return 0;
}
```

We will frequently interact with `size_t` later. For now, just remember one thing: **when you need to represent a "quantity" or "size," use `size_t`.**

### Let's Verify — Size of Fixed-Width Types

```c
#include <stdio.h>
#include <stdint.h>

int main(void) {
    printf("int8_t   : %zu byte(s)\n", sizeof(int8_t));
    printf("uint16_t : %zu byte(s)\n", sizeof(uint16_t));
    printf("int32_t  : %zu byte(s)\n", sizeof(int32_t));
    printf("uint64_t : %zu byte(s)\n", sizeof(uint64_t));
    return 0;
}
```

Compile and run:

```text
gcc -std=c17 -Wall -Wextra test_sizes.c -o test_sizes
./test_sizes
```

Result:

```text
int8_t   : 1 byte(s)
uint16_t : 2 byte(s)
int32_t  : 4 byte(s)
uint64_t : 8 byte(s)
```

Great, the byte count for each type matches our expectations.

## Step 4 — `sizeof`: The Ruler for Measuring Memory

### `sizeof` Is Not a Function

`sizeof` is a compile-time operator, not a function. It completes its calculation during compilation, so there is no runtime overhead. Its return type is `size_t`, so use the `%zu` format specifier when printing.

```c
int x = 10;
size_t size = sizeof x; // Parentheses are optional for variables
size = sizeof(int);     // Parentheses are required for type names
```

`sizeof` has a classic usage with arrays—calculating the number of elements:

```c
int arr[100];
size_t n = sizeof(arr) / sizeof(arr[0]);
printf("Array has %zu elements\n", n);
```

The principle is simple: `sizeof(arr)` is the total bytes occupied by the entire array, and `sizeof(arr[0])` is the bytes of a single element. Dividing them gives the number of elements.

> ⚠️ **Pitfall Warning**
> This trick of "using `sizeof` to calculate element count" **only works within the scope where the array is defined**. Once the array is passed to a function, it decays into a pointer, and `sizeof(arr)` returns the size of the pointer (4 or 8), not the size of the array:

```c
#include <stdio.h>

// Wrong! arr is just a pointer here
void print_size(int arr[100]) {
    printf("In function: %zu bytes\n", sizeof(arr)); // Likely 8 on 64-bit
}

int main(void) {
    int arr[100];
    printf("In main: %zu bytes\n", sizeof(arr)); // 400 bytes
    print_size(arr);
    return 0;
}
```

We will expand on the mechanism of array decay to pointers in the article on pointers. For now, just remember the conclusion: "arrays become pointers when passed to functions."

## C++ Transition

C++ fully inherits all of C's integer types while doing a few important things to make the type system safer.

First, C++11 introduced the `<cstdint>` header file (note the lack of `.h` suffix), which functions like C's `<stdint.h>` but places the types in the `std::` namespace. Second, C++'s `{}` initialization prohibits "narrowing conversions"—you cannot initialize a variable with a value that exceeds the target type's range:

```cpp
int x = 100000; // OK, fits in int
// char c = x;   // Dangerous! Narrowing conversion, data loss
char c {x};     // Error: narrowing conversion not allowed with {}
```

This feature is very effective in eliminating a whole class of implicit conversion bugs. If you write C++ code in the future, it is highly recommended to develop the habit of using `{}` for initialization.

## Summary

At this point, we have a clear understanding of the basic mechanisms of integer storage in C. The core points can be summarized in a few sentences: the C standard only specifies a minimum guaranteed bit count for each integer type; actual widths vary by platform, so cross-platform code should use the fixed-width types from `<stdint.h>`. The difference between signed and unsigned is not just "can it store negative numbers"; their overflow behaviors are completely different—unsigned wrapping is legal, while signed overflow is undefined behavior. `sizeof` is our tool for measuring memory at compile time; combined with arrays, it can calculate element counts, but be aware that arrays decay into pointers when passed to functions.

The next question arises: we've covered integers, but what about decimals? How are characters stored? Can we protect a variable from accidental modification after declaring it? These are the topics we will discuss in the next article.

## Exercises

### Exercise 1: Type Detector

Write a program that prints the `sizeof` values of all the following types, and check against the standard to see if they meet the minimum guarantees:

```c
char, short, int, long, long long,
float, double, long double,
int8_t, int16_t, int32_t, int64_t,
void*, char*
```

Hint: You can use a macro to reduce repetitive code.

### Exercise 2: Overflow Observation

Perform overflow experiments on signed `int8_t` and unsigned `uint8_t` respectively:

```c
#include <stdio.h>
#include <stdint.h>
#include <limits.h>

int main(void) {
    int8_t s = INT8_MAX;
    uint8_t u = UINT8_MAX;

    printf("Signed max + 1: %d\n", s + 1);
    printf("Unsigned max + 1: %u\n", u + 1);

    return 0;
}
```

Compile and run, observing the behavioral difference between the two. Then add the `-fsanitize=undefined` flag to recompile and see what changes.

## Reference Resources

- [cppreference: C Integer Types](https://en.cppreference.com/w/c/language/integer_constant)
- [cppreference: Fixed-width Integer Types](https://en.cppreference.com/w/c/types/integer)
- [Summary of C/C++ Integer Rules](https://www.nayuki.io/page/summary-of-c-cpp-integer-rules)
