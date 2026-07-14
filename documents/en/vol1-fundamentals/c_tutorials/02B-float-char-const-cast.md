---
chapter: 1
cpp_standard:
- 11
description: Master C floating-point types and precision issues, character storage
  and encoding, the `const` qualifier, and implicit type conversion rules, and understand
  the motivation behind C++ type safety design.
difficulty: beginner
order: 3
platform: host
prerequisites:
- 数据类型基础：整数与内存
reading_time_minutes: 12
tags:
- host
- cpp-modern
- beginner
- 入门
- 基础
title: Floating Point, Characters, const, and Type Conversion
translation:
  source: documents/vol1-fundamentals/c_tutorials/02B-float-char-const-cast.md
  source_hash: 26a73b54e9d5c4cf45da782044197a53a84328fd774f55eadb5283bd45cedee2
  translated_at: '2026-06-16T05:50:02.822671+00:00'
  engine: anthropic
  token_count: 1951
---
# Floating Point, Characters, const, and Type Conversions

In the previous post, we dissected the integer family from the inside out—integer hierarchy, signedness, fixed-width types, and `sizeof`. But the programming world involves more than just integers: product prices require decimals, text on screens requires characters, and variables sometimes need protection from unauthorized modification. Furthermore, how does the compiler handle data of different types when they are mixed in an operation? These are the topics we will tackle one by one today.

To be honest, some parts of this lesson—especially implicit type conversion—might seem convoluted at first glance. But don't worry; these "pitfalls" are precisely the motivation behind C++'s stronger type system. Once you understand "what goes wrong" in C, learning "how C++ fixes these problems" will feel like a natural next step.

> **Learning Objectives**
> After completing this chapter, you will be able to:
>
> - [ ] Understand the precision characteristics of floating-point types and avoid common errors in floating-point comparisons.
> - [ ] Recognize the true nature of character types—they are just small integers.
> - [ ] Correctly use the `const` qualifier to protect data.
> - [ ] Understand the rules of implicit type conversion and avoid the traps of mixing signed and unsigned integers.

## Environment Setup

We will conduct all subsequent experiments in the following environment:

- Platform: Linux x86\_64 (WSL2 is also acceptable)
- Compiler: GCC 13+ or Clang 17+
- Compiler flags: `-Wall -Wextra -std=c17`

## Step 1—How Are Decimals Stored? The World of Floating-Point Precision

### The Floating-Point Trio

The C language provides three floating-point types, listed in order of increasing precision:

| Type | Typical Size | Significant Digits | Literal Syntax |
|------|---------|---------|-----------|
| `float` | 32-bit (Single Precision) | ~7 digits | `3.14f` |
| `double` | 64-bit (Double Precision) | ~15 digits | `3.14` (Default) |
| `long double` | 80 or 128-bit | Platform-dependent | `3.14L` |

`double` is the default floating-point type. When you write `3.14`, the compiler treats it as a `double`. If you want to use `float`, remember to add the `f` suffix; for `long double`, add the `L` suffix.

```c
float f = 3.14f;            // 后缀 f 表示 float
double d = 3.14159265359;    // 默认就是 double
long double ld = 3.14L;      // 后缀 L 表示 long double
```

### Floating-point numbers are imprecise — this is not a bug

To understand floating-point numbers, accept one fact first: **they are approximations, not exact values**. It feels counterintuitive — how can the `0.1` you store not equal `0.1`? The answer is hiding inside the binary structure of a floating-point number.

Start with integers as a warm-up. An `int` has only 32 binary bits, so the numbers it can represent have a fixed ceiling and floor; go past them and you overflow. Yet there are infinitely many integers, and the computer just picks one finite segment to represent. Floating-point numbers face the exact same problem: there are infinitely many real numbers, many with infinitely many fractional digits, and a `float` still only gets 32 bits. How does it cover such a vast range with so few bits? The same way you would on paper — scientific notation.

Decimal scientific notation writes `0.0123` as `1.23 × 10⁻²`, a mantissa times a power of 10. A computer does the same thing, only with base 2: `number = (±1) × mantissa × 2^exponent`. The 32 bits of a `float` are split into exactly these three parts:

| 1 bit sign | 8 bits exponent | 23 bits mantissa |
|---|---|---|
| Positive or negative | How large or small the number can get (range) | How many significant digits you get (precision) |

The exponent bits control magnitude, the mantissa bits control fine detail, and both are finite. The exponent lets you reach as high as `10³⁸` and as low as `10⁻⁴⁵`; but with only 23 mantissa bits, you get roughly 7 decimal digits of precision — anything beyond that cannot be stored.

The real trouble lives in those 23 mantissa bits. The decimal `0.1`, written in binary, is `0.0001100110011...` — an infinitely repeating fraction. The mantissa field has only 23 bits, so it cannot hold the infinite repetition and must truncate. What gets stored is no longer `0.1`. The little program below prints out the raw 32 bits of `0.1f`; click "Try it yourself" to run it directly:

<OnlineCompilerDemo
  title="What 0.1 actually looks like inside a float"
  source-path="code/examples/vol1/c_float_representation_en.c"
  description="Print the 32 bits of 0.1f (sign | exponent | mantissa), then see why 0.1 + 0.2 does not equal 0.3 under double."
  allow-run
  run-compiler="cg132"
  run-options="-O2 -std=c17"
/>

The key lines of output look like this:

```text
32 bits of 0.1f in a float (sign | exponent | mantissa):
  0 01111011 10011001100110011001101

Printed at different precisions — never exactly 0.1:
  9 digits : 0.100000001
  20 digits: 0.10000000149011611938

double: 0.1 + 0.2 == 0.3 ? no
  a + b = 0.30000000000000004441
  c     = 0.29999999999999998890
```

Look at the first line: the sign bit is `0` (positive), the exponent is `01111011`, and the mantissa is `10011001100110011001101` — that run of `1001 1001 1001...` is the infinite repetition truncated to 23 bits, with the final `1` left over from rounding. That is why `0.1f` prints as `0.10000000149011611938`, not `0.1`.

The `double` part is even clearer: `0.1 + 0.2` comes out to `0.30000000000000004441`, while `0.3` stored is `0.29999999999999998890`. Two numbers that were never equal to begin with, compared with `==`, will of course not be equal. There is no mystery here: a finite mantissa cannot hold an infinitely repeating fraction, so this is inevitable.

::: warning
Never compare floating-point numbers with `==`. `0.1 + 0.2 != 0.3` is the norm in floating-point arithmetic, not a bug. Checking for "approximate equality" with an epsilon is the correct fix.
:::

One related trap: under `float`, `0.1f + 0.2f` happens to equal `0.3f`. Don't conclude that `float` is somehow more "accurate" — the 23-bit mantissa is simply too coarse, so the error gets rounded away. Switch to the higher-precision `double` and the error has nowhere to hide. Always use this kind of approximate comparison for equality checks:

```c
#include <math.h>

/// @brief Check whether two floats are approximately equal
/// @param a the first float
/// @param b the second float
/// @return 1 if approximately equal, 0 otherwise
int float_equal(float a, float b)
{
    return fabsf(a - b) < 1e-6f;
}
```

One more detail: when you write `float f = 0.1;`, the `0.1` is first treated as a `double` and then truncated to `float`, which introduces an extra precision difference. If you really mean to use `float`, get into the habit of adding the `f` suffix.

### Floating-Point in Embedded Systems

We must use extra caution when performing floating-point operations on embedded systems. Many microcontrollers lack a hardware Floating Point Unit (FPU), so floating-point operations rely on software emulation. This results in performance that is an order of magnitude worse than integer arithmetic. Even with an FPU, operations on `double` are usually significantly slower than those on `float`. Therefore, in embedded development, if a problem can be solved with integers, we should avoid using floating-point numbers.

## Step 2 — Characters Are Small Integers

### The Dual Identity of `char`

The C language does not have a dedicated "character type". The name `char` is easily misleading. In reality, it is simply the "smallest addressable storage unit," which happens to be one byte in size. We simply habitually use it to store ASCII codes for characters—and ASCII codes are themselves integers ranging from 0 to 127.

```c
char ch = 'A';
printf("%c\n", ch);   // 作为字符打印：A
printf("%d\n", ch);   // 作为整数打印：65
```

The ASCII code for `'A'` is 65. Therefore, the result of `'A' + 1` is 66, which corresponds to the character `'B'`. This "character is integer" feature is particularly useful for case conversion:

```c
char lower = 'a';
char upper = lower - 32;    // 'a' 的 ASCII 是 97，减 32 得 65 = 'A'
char upper2 = lower - ('a' - 'A');  // 更可读的写法
```

Let's verify this:

```bash
gcc -Wall -Wextra -std=c17 char_demo.c -o char_demo && ./char_demo
```

Execution result:

```text
A
65
```

### The Type of Character Literals — C and C++ Differ

Here is a subtle incompatibility between C and C++: in C, the type of a character literal `'A'` is `int` (occupying 4 bytes), whereas in C++, its type is `char` (occupying 1 byte).

```c
printf("%zu\n", sizeof('A'));  // C: 输出 4，C++: 输出 1
```

This distinction rarely affects how you write code, but if you switch from C to C++ later, keep this in mind to avoid being surprised by `sizeof` results.

### The World of Character Encoding—ASCII is Just the Beginning

ASCII uses 7 bits (0–127) to represent English letters, digits, and common symbols. However, the world involves more than just English—Chinese, Japanese, and emoji cannot be represented by ASCII. The C standard later added support for multibyte and wide characters:

```c
#include <wchar.h>

wchar_t wc = L'中';        // 宽字符，大小由实现定义
char* mb = "你好";          // 多字节字符（UTF-8 编码）
```

The problem with `wchar_t` is its inconsistent size—2 bytes on Windows and 4 bytes on Linux. This is why many modern projects simply use `char` arrays encoded in UTF-8 to handle all text. Encoding is a vast topic, so we will just touch on it here; knowing it exists is enough for now.

## Step 3 — Locking Variables: const

### Basic Usage of const

`const` is a type qualifier that tells the compiler, "the value of this variable should not be modified." You can think of it as putting a lock on a variable—once locked, any attempt to modify it will be blocked at compile time.

```c
const int kMaxSize = 256;        // 常量，不能修改
const double kPi = 3.14159265;

// kMaxSize = 100;  // 编译错误！不能修改 const 变量
```

Note that I use the word "should not" rather than "cannot"—technically, you can bypass `const` by using pointers to force a modification, but that is undefined behavior and simply asking for trouble.

### The Value of `const` in Function Parameters

The most common use of `const` is in function parameters to declare that "this function will not modify the passed data":

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

`const char* str` means "the character pointed to by `str` cannot be modified," but `str` itself can point elsewhere. `const size_t len` means "the value of `len` will not be changed inside the function." These `const` qualifiers are not just for the compiler; they are for anyone reading the code—the function signature itself conveys intent.

> ⚠️ **Warning**
> `const int* p` and `int* const p` are different things. The former means "the pointed-to value cannot change," while the latter means "the pointer itself cannot change." We will cover this distinction in detail in the pointers section, but for now, just be aware of it.

### const in Embedded Systems

In embedded development, `const` has a very practical benefit: the compiler can place `const` data in Flash/ROM instead of RAM. For microcontrollers where RAM is scarce, this is a crucial optimization. For example, a sine table used in a lookup table:

```c
const uint8_t sine_table[256] = {128, 131, 134, /* ... */};
```

By adding `const` to this array, we allow the compiler to place it in Flash, avoiding the consumption of valuable RAM.

## Step 4 — When Different Types Collide: Implicit Conversion

This section is likely the most confusing part of this entire article. Don't worry, we will take it one step at a time.

### Integer Promotion — Small Types Automatically "Upgrade"

In any arithmetic operation, `char` and `short` are first automatically promoted to `int` before participating in the calculation. This is a design decision rooted in history—early CPU arithmetic units only supported operations with `int` width, so compilers automatically perform this conversion for you.

```c
uint8_t a = 200;
uint8_t b = 100;
uint8_t c = a + b;  // 200 + 100 = 300，截断为 44
// 但 a + b 本身的类型是 int（300），不是 uint8_t
```

Here, the result of `a + b` is 300 of type `int`, which is then truncated to 44 when assigned to `uint8_t`. Integer promotion ensures that operations on smaller types do not overflow during intermediate steps, but assignment back to a smaller type can still result in truncation.

### Usual Arithmetic Conversions — What Happens with Two Different Types?

When two operands of different types are involved in an operation, the compiler converts them to a "common type" according to a set of rules. These rules may seem complex, but we only need to remember the one that causes the most trouble: **when a signed number and an unsigned number are used together, the signed number is converted to an unsigned number**.

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

> ⚠️ **Warning**
> When comparing signed and unsigned numbers, the signed number is implicitly converted to an unsigned number. The result of `-1 < 10u` in C is false. This bug is particularly insidious because the compiler might not warn you at all. It is especially common in mixed comparisons involving `size_t` (unsigned) and `int` (signed).

Our recommendation is simple: **avoid mixing signed and unsigned types whenever possible**. If you must mix them, use an explicit type conversion to make your intent clear:

```c
int count = -1;
size_t len = 5;
if (count < (int)len) {  // 显式转换，意图清楚
    // ...
}
```

### Explicit Type Conversion

In C, explicit conversion uses the C-style cast: `(type)value`. It is blunt and forceful, capable of converting anything without performing any checks:

```c
double pi = 3.14159;
int i = (int)pi;              // 截断为 3
unsigned int u = (unsigned int)-1;  // 变成 UINT_MAX
```

The problem with C-style casts is that they are too "all-powerful"—`const` can be cast away, pointer types can be converted arbitrarily, and assumptions about data layout are completely unchecked. This is why C++ introduced named cast operators (`static_cast`, `const_cast`, `reinterpret_cast`, `dynamic_cast`) to make the intent of each conversion crystal clear.

## C++ Interoperability

C++ significantly hardens the type system, with many improvements directly addressing C's pain points:

- `{}` initialization prohibits narrowing conversions (mentioned in the previous post).
- Named cast operators make type conversion intentions more explicit.
- `constexpr` guarantees compile-time evaluation on top of `const`.
- `char16_t`, `char32_t`, and `char8_t` solve type safety issues for character encodings.
- `std::numeric_limits<T>::epsilon()` provides a more precise tool for floating-point comparison than hand-written epsilon values.

The motivation for all these improvements stems from the "pitfalls" we discussed today. Once we understand "what goes wrong" in C, learning "how C++ solves these problems" becomes very natural.

## Summary

Let's recap the core points of this post. Floating-point numbers are approximations; `0.1 + 0.2 != 0.3` is an inherent characteristic of IEEE 754, so we must use epsilon for comparisons instead of `==`. `char` is essentially a small integer, and its signedness depends on the platform. `const` puts a compile-time protection lock on a variable and helps the compiler place data in Flash in embedded scenarios. Implicit type conversion—especially mixing signed and unsigned integers—is a high-risk area for bugs; when mixing them, we must explicitly write a cast.

At this point, we have laid a solid foundation for C language data types. Next, we will enter the world of operators and see how we perform various operations on this data.

## Exercises

### Exercise 1: Floating-Point Precision Detective

Predict the output of the following code, then compile and run it to verify your prediction:

```c
#include <stdio.h>

int main(void)
{
    double a = 0.1;
    double b = 0.2;
    double c = 0.3;

    printf("a + b == c? %s\n", (a + b == c) ? "yes" : "no");
    printf("a + b     = %.20f\n", a + b);
    printf("c         = %.20f\n", c);
    return 0;
}
```

Modify the code to use epsilon comparison to obtain the correct result.

### Exercise 2: Implicit Conversion Pitfalls

The following code contains a hidden bug. Find it and explain the reason:

```c
int values[] = {1, 2, 3, 4, 5};
int target = -1;

// bug 就在下面这行
if (target < sizeof(values) / sizeof(values[0])) {
    printf("target is in range\n");
}
```

Hint: What type does `sizeof` return?

### Exercise 3: `const` in Practice

Write a function that accepts a string and counts the occurrences of a specific character. Use `const` correctly in the function signature:

```c
/// @brief 统计字符 ch 在字符串 str 中出现的次数
/// @param str 不可修改的字符串
/// @param ch 要查找的字符
/// @return 出现次数
size_t count_char(const char* str, char ch);
```

## References

- [cppreference: Implicit conversions in C](https://en.cppreference.com/w/c/language/conversion)
- [What Every Programmer Should Know About Floating-Point Arithmetic](https://floating-point-gui.de/)
- [IEEE 754 floating-point standard](https://en.wikipedia.org/wiki/IEEE_754)
