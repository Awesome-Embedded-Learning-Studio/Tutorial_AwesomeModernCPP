---
title: Floating-point, character, const, and type conversion
description: Master C language floating-point types and precision issues, character
  storage and encoding, the const qualifier, and implicit type conversion rules, and
  understand the motivation behind C++ type-safe design.
chapter: 1
order: 3
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
- 数据类型基础：整数与内存
---
# Floating Point, Characters, const, and Type Conversions

In the previous chapter, we took the integer family apart from the inside out—integer ranks, signedness, fixed-width types, and `sizeof`. But the programming world isn't limited to integers: product prices need decimals, on-screen text needs characters, variables sometimes need protection from accidental modification, and when different types are mixed in an expression, we need to know exactly how the compiler handles it. These are the topics we'll tackle one by one today.

To be honest, some of the material here—especially implicit type conversions—can feel pretty convoluted at first glance. But don't worry; these "pitfalls" are exactly what motivated C++ to strengthen its type system. Once we understand "what goes wrong in C," learning "how C++ fixes these problems" will feel completely natural.

> **Learning Objectives**
> After completing this chapter, you will be able to:
>
> - [ ] Understand the precision characteristics of floating-point types and avoid common floating-point comparison errors
> - [ ] Recognize the true nature of character types—they are just small integers
> - [ ] Correctly use the `const` qualifier to protect data
> - [ ] Understand implicit type conversion rules and avoid the traps of mixing signed and unsigned values

## Environment Setup

All of our experiments in this section use the following environment:

- Platform: Linux x86\_64 (WSL2 is also fine)
- Compiler: GCC 13+ or Clang 17+
- Compiler flags: `-Wall -Wextra -std=c17`

## Step 1 — How Are Decimals Stored? The World of Floating-Point Precision

### The Three Floating-Point Siblings

C provides three floating-point types, ordered by precision from lowest to highest:

| Type | Typical Size | Significant Digits | Literal Syntax |
|------|-------------|-------------------|----------------|
| `float` | 32-bit (single precision) | ~7 digits | `3.14f` |
| `double` | 64-bit (double precision) | ~15 digits | `3.14` (default) |
| `long double` | 80 or 128 bits | Platform-dependent | `3.14L` |

`double` is the default floating-point type—when you write `3.14`, the compiler treats it as `double`. If you want to use `float`, remember to add the `f` suffix; for `long double`, add the `L` suffix.

```c
float f = 3.14f;            // 后缀 f 表示 float
double d = 3.14159265359;    // 默认就是 double
long double ld = 3.14L;      // 后缀 L 表示 long double
```

### Floating-Point Numbers Are Imprecise — This Is Not a Bug

This is the most important thing to understand about floating-point numbers: **floating-point numbers are approximations, not exact values**. The reason is that computers use a finite number of binary bits to represent decimal fractions, just like using a finite number of decimal places to represent 1/3—you can only ever get an approximation.

```c
#include <stdio.h>

int main(void)
{
    float a = 0.1f;
    float b = 0.2f;
    if (a + b == 0.3f) {
        printf("equal\n");
    } else {
        printf("not equal: %.9f\n", a + b);
    }
    return 0;
}
```

Let's verify this by compiling and running:

```bash
gcc -Wall -Wextra -std=c17 float_demo.c -o float_demo && ./float_demo
```

Output:

```text
not equal: 0.300000012
```

See? — `0.1 + 0.2` does not equal `0.3` in floating-point arithmetic. This is not a compiler bug; it's an inherent characteristic of the IEEE 754 floating-point standard. Therefore, **never use `==` to compare floating-point numbers**. The correct approach is to use a small epsilon value to check for "approximate equality":

```c
#include <math.h>

/// @brief 判断两个 float 是否近似相等
/// @param a 第一个浮点数
/// @param b 第二个浮点数
/// @return 1 表示近似相等，0 表示不相等
int float_equal(float a, float b)
{
    return fabsf(a - b) < 1e-6f;
}
```

> ⚠️ **Pitfall Warning**
> Never use `==` to compare floating-point numbers. `0.1 + 0.2 != 0.3` is the norm in floating-point arithmetic, not a bug. Using epsilon to check for approximate equality is the correct approach.

There's another detail: when you write `float f = 0.1;`, `0.1` is first treated as `double` and then truncated to `float`—which can introduce additional precision differences. If you definitely want to use `float`, make it a habit to add the `f` suffix.

### Floating Point in Embedded Systems

Using floating-point arithmetic on embedded systems requires extra caution. Many microcontrollers lack a hardware floating-point unit (FPU), so floating-point operations rely on software emulation and can be an order of magnitude slower than integer operations. Even with an FPU, `double` operations are usually significantly slower than `float`. Therefore, in embedded development, if a problem can be solved with integers, don't use floating point.

## Step 2 — Characters Are Just Small Integers

### The Dual Identity of char

C doesn't have a dedicated "character type." The name `char` is easily misleading; in reality, it's simply "the smallest addressable storage unit," which happens to be one byte in size. We just conventionally use it to store ASCII codes for characters—and ASCII codes are themselves integers in the range 0–127.

```c
char ch = 'A';
printf("%c\n", ch);   // 作为字符打印：A
printf("%d\n", ch);   // 作为整数打印：65
```

The ASCII code for `'A'` is 65. So the result of `'A' + 1` is 66, which corresponds to the character `'B'`. This "characters are integers" property is especially handy for case conversion:

```c
char lower = 'a';
char upper = lower - 32;    // 'a' 的 ASCII 是 97，减 32 得 65 = 'A'
char upper2 = lower - ('a' - 'A');  // 更可读的写法
```

Let's verify this:

```bash
gcc -Wall -Wextra -std=c17 char_demo.c -o char_demo && ./char_demo
```

Output:

```text
A
65
```

### The Type of Character Literals — C vs. C++

Here is a subtle incompatibility between C and C++: in C, the type of a character literal like `'A'` is `int` (4 bytes), but in C++ its type is `char` (1 byte).

```c
printf("%zu\n", sizeof('A'));  // C: 输出 4，C++: 输出 1
```

This difference doesn't affect your code in the vast majority of cases, but if you later switch from C to C++, keep this in mind so you aren't surprised by `sizeof` results.

### The World of Encoding — ASCII Is Just the Starting Point

ASCII uses 7 bits (0–127) to represent English letters, digits, and common symbols. But the world isn't limited to English—Chinese, Japanese, and emoji can't be represented with ASCII. The C standard later added support for multibyte characters and wide characters:

```c
#include <wchar.h>

wchar_t wc = L'中';        // 宽字符，大小由实现定义
char* mb = "你好";          // 多字节字符（UTF-8 编码）
```

The problem with `wchar_t` is that its size is inconsistent—2 bytes on Windows, 4 bytes on Linux. This is why many modern projects simply use UTF-8 encoded `char` arrays to handle all text. Encoding is a huge topic; we'll just touch on it here so you know it exists.

## Step 3 — Putting a Lock on Variables: const

### Basic Usage of const

`const` is a type qualifier that tells the compiler "the value of this variable should not be modified." You can think of it as putting a lock on a variable—once locked, any attempt to modify it will be caught at compile time.

```c
const int kMaxSize = 256;        // 常量，不能修改
const double kPi = 3.14159265;

// kMaxSize = 100;  // 编译错误！不能修改 const 变量
```

Note my wording here is "should not" rather than "cannot"—technically, you can force your way past `const` using pointers to modify data, but that is undefined behavior (UB) and purely asking for trouble.

### The Magic of const in Function Parameters

The most common use for `const` is in function parameters to declare "this function will not modify the passed-in data":

```c
/// @brief 计算字符串长度
/// @param str 不可修改的字符串
/// @return 字符串长度
size_t my_strlen(const char* str);

/// @brief 在缓冲区中写入数据
/// @param buf 可修改的缓冲区
/// @param len 缓冲区长度（函数不会修改 len）
void fill_buffer(char* buf, const size_t len);
```

`const char* str` means "the characters pointed to by str cannot be modified," but str itself can point elsewhere. `const size_t len` means "the value of len will not be changed inside the function." These `const` qualifiers aren't just for the compiler—they're for anyone reading the code. The function signature itself conveys intent.

> ⚠️ **Pitfall Warning**
> `const int* p` and `int* const p` are different things. The former means "the pointed-to value cannot be changed," while the latter means "the pointer itself cannot be changed." We'll dive into this distinction in the chapter on pointers; for now, just be aware that the difference exists.

### const in Embedded Systems

In embedded development, `const` has a very practical benefit—the compiler can place `const` data in Flash/ROM instead of RAM. For microcontrollers where RAM is at a premium, this is an important optimization. For example, a sine lookup table:

```c
const uint8_t sine_table[256] = {128, 131, 134, /* ... */};
```

With `const` added to this array, the compiler can place it in Flash, saving precious RAM.

## Step 4 — When Different Types Collide: Implicit Conversions

This section is the most confusing part of the entire chapter. Don't rush; we'll take it step by step.

### Integer Promotion — Small Types Automatically "Upgrade"

In any arithmetic operation, `char` and `short` are automatically promoted to `int` before participating in the calculation. This is a legacy design—early CPU arithmetic units only supported `int`-width operations, so the compiler automatically performed this conversion for you.

```c
uint8_t a = 200;
uint8_t b = 100;
uint8_t c = a + b;  // 200 + 100 = 300，截断为 44
// 但 a + b 本身的类型是 int（300），不是 uint8_t
```

Here, the result of `a + b` is `int` type with a value of 300, which then gets truncated to 44 when assigned back to `uint8_t`. Integer promotion ensures that operations on small types don't overflow during intermediate steps, but assigning back to a small type can still cause truncation.

### Usual Arithmetic Conversions — What Happens with Two Different Types

When two operands of different types are used in an operation, the compiler converts them to a "common type" according to a set of rules. These rules look pretty complex, but we only need to remember the most trap-prone one: **when a signed number and an unsigned number are used together, the signed number is converted to unsigned**.

```c
int i = -1;
unsigned int u = 10;
if (i < u) {
    // 你以为 -1 < 10 是 true？
    // 错！i 被转成 unsigned int，变成 UINT_MAX（一个巨大的正数）
    // 所以 UINT_MAX < 10 是 false
    printf("这行不会打印\n");
}
```

> ⚠️ **Pitfall Warning**
> When comparing signed and unsigned numbers, the signed number is implicitly converted to unsigned. The result of `-1 < 10u` in C is false. This kind of bug is particularly insidious because the compiler might not warn you at all. It's especially common in mixed comparisons involving `size_t` (unsigned) and `int` (signed).

Our advice is simple: **avoid mixing signed and unsigned values whenever possible**. If you must mix them, write an explicit cast to make your intent clear:

```c
int count = -1;
size_t len = 5;
if (count < (int)len) {  // 显式转换，意图清楚
    // ...
}
```

### Explicit Type Conversions

Explicit conversion in C is just the C-style cast: `(type)value`. It's blunt and forceful—it can convert anything and performs no checks whatsoever:

```c
double pi = 3.14159;
int i = (int)pi;              // 截断为 3
unsigned int u = (unsigned int)-1;  // 变成 UINT_MAX
```

The problem with C-style casts is that they're too "omnipotent"—`const` can be cast away, pointer types can be converted arbitrarily, and assumptions about data layout go completely unverified. This is exactly why C++ introduced named cast operators (`static_cast`, `const_cast`, `reinterpret_cast`, `dynamic_cast`), making the intent of each conversion clear at a glance.

## Bridging to C++

C++ made extensive safety hardening to its type system, with many improvements directly targeting C's pain points:

- `{}` initialization prohibits narrowing conversions (mentioned in the previous chapter)
- Named cast operators make the intent of type conversions more explicit
- `constexpr` guarantees compile-time evaluation on top of `const`
- `char16_t`, `char32_t`, and `char8_t` solve the type safety problems of encoding
- `std::numeric_limits<T>::epsilon()` provides more precise floating-point comparison tools than hand-writing epsilon

The motivation for all of these improvements comes directly from the "pitfalls" we discussed today. Once we understand "what goes wrong in C," learning "how C++ fixes these problems" feels completely natural.

## Summary

Let's recap the core points of this chapter. Floating-point numbers are approximations; `0.1 + 0.2 != 0.3` is an inherent characteristic of IEEE 754, and we should use epsilon instead of `==` to compare floating-point numbers. `char` is essentially a small integer, and its signedness depends on the platform. `const` puts a compile-time protection lock on a variable, and in embedded scenarios it also helps the compiler place data in Flash. Implicit type conversions—especially mixing signed and unsigned values—are a high-risk area for bugs; when mixing types, always write an explicit cast.

At this point, we've laid a solid foundation for C data types. Next, we'll enter the world of operators and see how to perform various operations on this data.

## Exercises

### Exercise 1: Floating-Point Precision Detective

Predict the output of the following code, then compile and run it to verify your prediction:

```c
#include <stdio.h>

int main(void)
{
    float a = 0.1f;
    float b = 0.2f;
    float c = 0.3f;

    printf("a + b == c? %s\n", (a + b == c) ? "yes" : "no");
    printf("a + b     = %.20f\n", a + b);
    printf("c         = %.20f\n", c);
    return 0;
}
```

Modify the code to use epsilon comparison to get the correct result.

### Exercise 2: Implicit Conversion Trap

The following code has a hidden bug. Find it and explain the reason:

```c
int values[] = {1, 2, 3, 4, 5};
int target = -1;

// bug 就在下面这行
if (target < sizeof(values) / sizeof(values[0])) {
    printf("target is in range\n");
}
```

Hint: What type does `sizeof` return?

### Exercise 3: const in Practice

Write a function that takes a string and counts the occurrences of a specific character. Use `const` correctly in the function signature:

```c
/// @brief 统计字符 ch 在字符串 str 中出现的次数
/// @param str 不可修改的字符串
/// @param ch 要查找的字符
/// @return 出现次数
size_t count_char(const char* str, char ch);
```

## References

- [cppreference: C implicit conversions](https://en.cppreference.com/w/c/language/conversion)
- [What Every Programmer Should Know About Floating-Point Arithmetic](https://floating-point-gui.de/)
- [IEEE 754 floating-point standard](https://en.wikipedia.org/wiki/IEEE_754)
