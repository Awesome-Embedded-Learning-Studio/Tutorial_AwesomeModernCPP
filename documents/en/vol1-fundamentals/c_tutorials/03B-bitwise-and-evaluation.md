---
title: Bitwise Operations and Evaluation Order
description: A deep dive into the four fundamental bitwise operations, shift caveats,
  operator precedence pitfalls, evaluation order and sequence points, and understanding
  the essence of undefined behavior (UB).
chapter: 1
order: 5
tags:
- host
- cpp-modern
- beginner
- 入门
difficulty: beginner
platform: host
reading_time_minutes: 15
cpp_standard:
- 11
prerequisites:
- 运算符基础：让数据动起来
translation:
  source: documents/vol1-fundamentals/c_tutorials/03B-bitwise-and-evaluation.md
  source_hash: 6726ef3c1f82b2cbaf86581adaf486e306cd216c72c990d97b1c43ae813ee9a3
  translated_at: '2026-04-20T03:15:09.223989+00:00'
  engine: anthropic
  token_count: 1969
---
# Bitwise Operations and Evaluation Order

In the previous chapter, we covered common operators like arithmetic, relational, and logical ones. Now we tackle two tougher topics: bitwise operations and evaluation order. Bitwise operations are rarely used in general application-layer programming, but if you plan to work with embedded systems or low-level system programming, they become your daily tools—configuring hardware registers, parsing bit fields in communication protocols, and implementing flag sets all rely on them. Evaluation order and sequence points are the key to understanding "why some code produces different results on different compilers."

Admittedly, these two topics can feel a bit confusing at first. But don't worry, we'll take it one step at a time, starting with the more intuitive bitwise operations.

> **Learning Objectives**
> After completing this chapter, you will be able to:
>
> - [ ] Master the four classic bitwise operations: set, clear, toggle, and check
> - [ ] Understand the details and pitfalls of left shift and right shift
> - [ ] Remember the most counterintuitive operator precedence rules that are easy to get wrong
> - [ ] Understand evaluation order and sequence points to avoid writing code with undefined behavior

## Environment Setup

All of our following experiments run in this environment:

- Platform: Linux x86\_64 (WSL2 is also fine)
- Compiler: GCC 13+ or Clang 17+
- Compiler flags: `-Wall -Wextra -std=c17`

## Step 1 — Meet the Bitwise Operators

### What is a "Bit"

When we discussed data types in the previous chapter, we mentioned that a variable's value is stored in memory as 0s and 1s. A `uint8_t` has eight binary digits (bits), and a `uint32_t` has 32 bits. Bitwise operations manipulate these binary digits directly—you no longer treat data as a "number," but as a "row of switches."

C provides six bitwise operators:

| Operator | Meaning | Simple Explanation |
|----------|---------|-------------------|
| `&` | Bitwise AND | 1 only if both are 1 |
| `\|` | Bitwise OR | 1 if either is 1 |
| `^` | Bitwise XOR | 1 if different, 0 if the same |
| `~` | Bitwise NOT | 0 becomes 1, 1 becomes 0 |
| `<<` | Left shift | All bits shift left, low bits filled with 0 |
| `>>` | Right shift | All bits shift right, high bits filled with 0 (for unsigned types) |

We'll use 8-bit unsigned numbers for demonstration, as they are easier to visualize:

```text
  0b11001100  (204)
& 0b10101010  (170)
-----------
  0b10001000  (136)

  0b11001100  (204)
| 0b10101010  (170)
-----------
  0b11101110  (238)

  0b11001100  (204)
^ 0b10101010  (170)
-----------
  0b01100110  (102)

~ 0b11001100  (204)
-----------
  0b00110011  (51)    （8 位取反）
```

## Step 2 — Four Classic Operations: Set, Clear, Toggle, Check

Bitwise operations have four most commonly used patterns in embedded development that you must know by heart.

### Set — Change a bit to 1

To set a specific bit to 1, we use the OR operator combined with a left shift. The principle is: `0 | 1 = 1`, `1 | 1 = 1`—ORing with 1 always yields 1, while ORing other bits with 0 keeps them unchanged.

```c
uint8_t reg = 0x00;       // 00000000
reg |= (1 << 3);          // 把第 3 位置 1 → 00001000 = 0x08
reg |= (1 << 0);          // 把第 0 位置 1 → 00001001 = 0x09

// 一次置多个位
reg |= 0x07;              // 置位第 0、1、2 位 → 00001111 = 0x0F
```

### Clear — Change a bit to 0

To clear a specific bit to 0, we use the AND operator combined with bitwise NOT. The principle is: `x & 1 = x`, `x & 0 = 0`—ANDing with 0 always yields 0, while ANDing with 1 keeps the bit unchanged.

```c
uint8_t reg = 0x0F;       // 00001111
reg &= ~(1 << 3);         // 清除第 3 位 → 00000111 = 0x07
```

The value of `~(1 << 3)` is `0xF7` (`11110111`). After ANDing with `0x0F`, bit 3 becomes 0 while all other bits remain unchanged.

### Toggle — Flip a bit

To toggle a specific bit, we use the XOR operator. The principle is: `x ^ 1 = ~x` (flipped), `x ^ 0 = x` (unchanged).

```c
uint8_t reg = 0x07;       // 00000111
reg ^= (1 << 0);          // 翻转第 0 位 → 00000110 = 0x06
```

### Check — See if a bit is 0 or 1

To check the value of a specific bit, we use the AND operator combined with a left shift, and then see if the result is non-zero:

```c
uint8_t reg = 0x06;       // 00000110
if (reg & (1 << 1)) {
    // 第 1 位是 1（确实如此：00000110 的第 1 位是 1）
}
if (reg & (1 << 0)) {
    // 第 0 位是 0（不会进入这个分支）
}
```

Let's verify this by chaining all four operations together:

```c
#include <stdio.h>
#include <stdint.h>

/// @brief 将一个 uint8_t 按二进制打印出来
void print_binary(uint8_t val)
{
    for (int i = 7; i >= 0; i--) {
        printf("%d", (val >> i) & 1);
    }
    printf(" (0x%02X)\n", val);
}

int main(void)
{
    uint8_t reg = 0x00;
    printf("初始值:       "); print_binary(reg);

    reg |= (1 << 3);       // 置位第 3 位
    printf("置位第3位:    "); print_binary(reg);

    reg |= 0x07;           // 置位第 0、1、2 位
    printf("置位0,1,2位:  "); print_binary(reg);

    reg &= ~(1 << 3);      // 清零第 3 位
    printf("清零第3位:    "); print_binary(reg);

    reg ^= (1 << 0);       // 翻转第 0 位
    printf("翻转第0位:    "); print_binary(reg);

    printf("第1位是: %d\n", (reg >> 1) & 1);

    return 0;
}
```

Compile and run:

```bash
gcc -Wall -Wextra -std=c17 bitwise_demo.c -o bitwise_demo && ./bitwise_demo
```

Output:

```text
初始值:       00000000 (0x00)
置位第3位:    00001000 (0x08)
置位0,1,2位:  00001011 (0x0B)
清零第3位:    00000011 (0x03)
翻转第0位:    00000010 (0x02)
第1位是: 1
```

This matches our expectations perfectly. If you find the `(1 << n)` syntax unintuitive, you can wrap it in macros:

```c
#define BIT(n)              (1U << (n))
#define SET_BIT(x, n)       ((x) |= BIT(n))
#define CLEAR_BIT(x, n)     ((x) &= ~BIT(n))
#define TOGGLE_BIT(x, n)    ((x) ^= BIT(n))
#define CHECK_BIT(x, n)     (((x) & BIT(n)) != 0)
```

> ⚠️ **Pitfall Warning**
> Every parameter and the overall expression in the macro definitions are wrapped in parentheses. This is not redundant. Without parentheses, `CLEAR_BIT(x | y, 3)` would expand to `x | y &= ~(1 << 3)`. Since `&=` has lower precedence than `|`, the meaning changes completely. Parentheses in macros are the cheapest insurance.

## Step 3 — Shift Caveats

### Left Shift and Right Shift Behavior

Left shifting `<<` on unsigned types has well-defined behavior—low bits are filled with 0, and high bits are discarded. Right shifting `>>` on unsigned types is also well-defined (high bits are filled with 0).

However, right shifting signed types is **implementation-defined**—the compiler can choose arithmetic right shift (high bits filled with the sign bit, preserving negative values) or logical right shift (high bits filled with 0). Most platforms use arithmetic right shift, but this is not guaranteed by the standard:

```c
int8_t x = -4;         // 二进制：11111100
int8_t y = x >> 1;     // 可能是 -2（算术右移，高位补 1）
                        // 也可能是 126（逻辑右移，高位补 0）
                        // 大多数平台是前者，但不保证
```

> ⚠️ **Pitfall Warning**
> If the shift amount is negative, or equal to or greater than the bit width of the type (e.g., shifting a `int32_t` by 32 bits), the behavior is **undefined**. Intuitively, you might think the result of `1 << 32` is 0, but the standard dictates this is UB—in practice, you might get 1 (because the CPU only takes the low 5 bits of the shift amount, turning 32 into 0).

### Bitwise Operator Precedence Traps

This is the most common pitfall for bitwise operation beginners—**bitwise operators all have lower precedence than relational operators**. In other words, `&`, `|`, and `^` all have lower precedence than `==`, `!=`, `<`, and `>`.

```c
if (flags & 0x0F == 0) { }    // 实际解析为 flags & (0x0F == 0)
                                // 也就是 flags & 0，永远为假！
if ((flags & 0x0F) == 0) { }  // 这才是你想要的意思
```

The problem with the first approach is that `==` binds with `0x0F` and `0` first (because `==` has higher precedence than `&`), resulting in 0 (since `0x0F != 0`), and then `flags & 0` is always false.

The core principle: **Whenever bitwise and comparison operations are mixed, you must use parentheses**. Parentheses don't slow down your code, but they protect you from these precedence traps.

A practical precedence mnemonic, from highest to lowest:

1. Parentheses `()` > Subscript `[]` > Member access `.` `->`
2. Unary operators (`!` `~` `++` `--` `*` `&` `sizeof`)
3. Arithmetic (`*` `/` `%` > `+` `-`)
4. Shift (`<<` `>>`)
5. Relational (`<` `>` `<=` `>=` > `==` `!=`)
6. Bitwise (`&` > `^` > `|`)
7. Logical (`&&` > `||`)
8. Ternary `?:` > Assignment `=` > Comma `,`

## Step 4 — Evaluation Order and Sequence Points

This is one of the most confusing concepts in C. We need to understand two separate things: **precedence** and **evaluation order**. These two are independent—precedence determines how operators bind to their operands, while evaluation order determines when the operands are actually computed.

### Evaluation Order Is Unspecified

In most expressions, the order in which operands are evaluated is up to the compiler. For example, in `f() + g()`, the standard doesn't specify whether `f` or `g` is called first—the compiler can choose any order. If neither function has side effects (doesn't modify global variables, read or write files), the order doesn't matter; but if there are side effects, the results might vary by compiler.

### Sequence Points — Safe Boundaries for Side Effects

A **sequence point** is a specific point in program execution where all previous operations have been completed, and subsequent operations have not yet begun. Sequence points in C include:

- After evaluating the left operand of `&&` (this is the principle behind short-circuit evaluation)
- After evaluating the left operand of `||`
- After evaluating the first operand of `?:`
- After evaluating the left operand of the comma operator
- At the end of a full expression (the semicolon at the end of a statement)
- After all arguments have been evaluated but before the function body begins executing, during a function call

### Undefined Behavior: No Sequence Point Between Two Modifications

If, between two sequence points, the same variable is modified twice, or is modified and read at the same time (and the read is not used to compute the new value), that is **undefined behavior**:

```c
int i = 3;

i = i++;                  // UB：i 同时被赋值和自增
a[i] = i++;               // UB：i 被读取的同时被修改
printf("%d %d", i++, i++); // UB：i 被修改两次，参数之间没有序列点

// 正确写法
i = i + 1;    // OK：只修改一次
i++;          // OK：单独使用
```

> ⚠️ **Pitfall Warning**
> This type of bug is particularly insidious because it might "look fine" on one compiler, but break when you switch compilers or enable optimizations. If you encounter a question like `i = i++` in an interview, the correct answer is "this is UB, there is no standard answer," rather than guessing how the compiler will handle it.

If you want to deeply understand the concept of UB, think of it as a traffic rule: the standard says "don't run a red light." If you do, the consequences are unpredictable—you might be fine, you might get a ticket, or you might cause an accident. UB is the "running a red light" of the programming world.

## C++ Connection

C++ does a few useful things regarding bitwise operations. In `<bitset>`, `std::bitset<N>` can use the `[]` operator to access individual bits directly, and it provides semantically clear operations like `test()`, `set()`, `reset()`, and `flip()`—safer and more readable than hand-writing bitwise operations. In C++, you should prefer `std::bitset` unless you truly need extreme performance or direct hardware manipulation.

Regarding evaluation order, C++17 strengthened the rules—a function expression is guaranteed to be evaluated before its arguments, which is more deterministic than C's "unspecified" rule. Additionally, if a `constexpr` function triggers UB during compile-time evaluation, the compiler will directly report an error—acting as a free UB detector.

## Summary

The four classic bitwise operations—set (`|=` + `<<`), clear (`&=` + `~` + `<<`), toggle (`^=` + `<<`), and check (`&` + `<<`)—are essential skills for embedded development. The biggest trap in operator precedence is that bitwise operators have lower precedence than relational operators; when mixing bitwise and comparison operations, you must use parentheses. The core principle of evaluation order and sequence points is: never modify the same variable multiple times within a single expression—that is undefined behavior.

At this point, we have covered every aspect of C language operators. Next, we will learn about control flow—how to make a program execute different code based on conditions, and how to repeat a block of code.

## Exercises

### Exercise 1: Bit Manipulation Toolkit

Implement the following bit manipulation functions:

```c
/// @brief 将 value 的第 n 位置为 1
uint32_t bit_set(uint32_t value, int n);

/// @brief 将 value 的第 n 位清零
uint32_t bit_clear(uint32_t value, int n);

/// @brief 翻转 value 的第 n 位
uint32_t bit_toggle(uint32_t value, int n);

/// @brief 提取 value 的 [high:low] 位域（包含两端）
uint32_t bit_extract(uint32_t value, int high, int low);
```

### Exercise 2: Safe Shifting

Write a function that safely performs a left shift operation, handling all edge cases:

```c
/// @brief 安全的左移操作
/// @param val 要移位的值
/// @param n 移位量
/// @param bits 类型的位宽（如 32）
/// @return 移位结果，非法移位量返回 0
uint32_t safe_shift_left(uint32_t val, int n, int bits);
```

### Exercise 3: Expression Analysis

Analyze the evaluation behavior of the following expressions (without actually running them), and label each as "well-defined," "unspecified behavior," or "undefined behavior":

```c
int a = 5, b = 3;
int r1 = a++ + b;            // ?
int r2 = a++ + ++a;          // ?
int r3 = (a > b) ? a-- : b--; // ?
printf("%d %d\n", a++, a++);  // ?
```

## References

- [cppreference: C operator precedence](https://en.cppreference.com/w/c/language/operator_precedence)
- [cppreference: Sequence points](https://en.cppreference.com/w/c/language/eval_order)
- [CERT: EXP30-C - Do not depend on the order of evaluation](https://wiki.sei.cmu.edu/confluence/display/c/EXP30-C.+Do+not+depend+on+the+order+of+evaluation+for+side+effects)
