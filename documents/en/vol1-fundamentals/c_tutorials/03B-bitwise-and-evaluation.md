---
chapter: 1
cpp_standard:
- 11
description: Dive deep into the four fundamental bitwise operations, shift precautions,
  operator precedence pitfalls, evaluation order and sequence points, and understand
  the nature of undefined behavior.
difficulty: beginner
order: 5
platform: host
prerequisites:
- 运算符基础：让数据动起来
reading_time_minutes: 10
tags:
- host
- cpp-modern
- beginner
- 入门
title: Bitwise Operations and Evaluation Order
translation:
  source: documents/vol1-fundamentals/c_tutorials/03B-bitwise-and-evaluation.md
  source_hash: b1eb16f10c755774685a3ff1c6af887e935d9981d867224eaaf0883bf55d9647
  translated_at: '2026-06-16T03:33:39.762676+00:00'
  engine: anthropic
  token_count: 1965
---
# Bitwise Operations and Evaluation Order

In the previous chapter, we covered common operators like arithmetic, relational, and logical ones. Now, let's tackle two tougher topics: bitwise operations and evaluation order. Bitwise operations are less common in application-layer programming, but if you plan to work with embedded systems or low-level system programming, they will be your daily tools—configuring hardware registers, parsing bit fields in communication protocols, and implementing flag sets all rely on them. Evaluation order and sequence points are the keys to understanding "why some code produces different results on different compilers."

Admittedly, these topics can feel a bit confusing when you're starting out. But don't worry, we'll take it step by step, starting with the most intuitive part: bitwise operations.

> **Learning Objectives**
> After completing this chapter, you will be able to:
>
> - [ ] Master the four classic bitwise operations: set, clear, toggle, and check.
> - [ ] Understand the details and pitfalls of left and right shifts.
> - [ ] Remember the most counter-intuitive rules regarding operator precedence.
> - [ ] Understand evaluation order and sequence points to avoid writing code with undefined behavior.

## Environment Setup

We will conduct all subsequent experiments in the following environment:

- Platform: Linux x86_64 (WSL2 is also acceptable)
- Compiler: GCC 13+ or Clang 17+
- Compiler flags: `-std=c17 -Wall -Wextra -pedantic`

## Step 1 — Understanding Bitwise Operators

### What is a "Bit"?

In the previous chapter on data types, we mentioned that a variable's value is stored in memory as 0s and 1s. One `byte` consists of 8 binary bits, and one `uint32_t` consists of 32 binary bits. Bitwise operations manipulate these binary bits directly—you stop treating data as "numbers" and start treating it as "a row of switches."

C provides six bitwise operators:

| Operator | Meaning | Simple Explanation |
|----------|---------|-------------------|
| `&` | Bitwise AND | Results in 1 only if both are 1 |
| `\|` | Bitwise OR | Results in 1 if either is 1 |
| `^` | Bitwise XOR | Results in 1 if different, 0 if same |
| `~` | Bitwise NOT | 0 becomes 1, 1 becomes 0 |
| `<<` | Left Shift | All bits shift left, low bits filled with 0 |
| `>>` | Right Shift | All bits shift right, high bits filled with 0 (for unsigned) |

Let's use an 8-bit unsigned number for demonstration, as it's more intuitive:

```cpp
#include <stdio.h>
#include <stdint.h>

int main(void) {
    uint8_t a = 0b00001100; // 12
    uint8_t b = 0b10101010; // 170

    printf("a & b = 0b%08X\n", a & b); // 0b00001000 (8)
    printf("a | b = 0b%08X\n", a | b); // 0b10101110 (174)
    printf("a ^ b = 0b%08X\n", a ^ b); // 0b10100110 (166)
    printf("~a    = 0b%08X\n", (uint8_t)~a); // 0b11110011 (243)
    printf("a << 2 = %d\n", a << 2); // 48
    printf("b >> 2 = %d\n", b >> 2); // 42

    return 0;
}
```

## Step 2 — The Four Classic Operations: Set, Clear, Toggle, Check

There are four most common operation patterns in embedded development that you must memorize.

### Set — Set a specific bit to 1

To set a specific bit to 1, we use the OR operation combined with a left shift. The principle is: `x | 1 = 1`, `x | 0 = x`—as long as you OR with 1, the result is 1; ORing with 0 leaves the bit unchanged.

```cpp
uint8_t flags = 0b00000000;
flags |= (1 << 3); // Set the 3rd bit (0-indexed)
// Result: 0b00001000
```

### Clear — Set a specific bit to 0

To clear a specific bit, we use the AND operation combined with NOT. The principle is: `x & 0 = 0`, `x & 1 = x`—ANDing with 0 forces the result to 0, while ANDing with 1 preserves the original value.

```cpp
uint8_t flags = 0b00011100;
flags &= ~(1 << 3); // Clear the 3rd bit
// ~(1 << 3) is 0b11110111
// Result: 0b00011000
```

The value of `~(1 << 3)` is `0b11110111` (`~0b00001000`). When ANDed with `0b00011100`, the 3rd bit becomes 0 while the others remain unchanged.

### Toggle — Flip a specific bit

To flip a specific bit, use the XOR operation. The principle is: `x ^ 1 = ~x` (flip), `x ^ 0 = x` (unchanged).

```cpp
uint8_t flags = 0b00001000;
flags ^= (1 << 3); // Toggle the 3rd bit
// Result: 0b00000000
```

### Check — See if a bit is 0 or 1

To check the value of a specific bit, use the AND operation combined with a left shift, then check if the result is non-zero:

```cpp
bool is_set = (flags & (1 << 3)) != 0;
```

Let's verify this by chaining all four operations together:

```cpp
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

int main(void) {
    uint8_t flags = 0;

    // 1. Set bit 3
    flags |= (1 << 3);
    printf("After set:   %d (expected 8)\n", flags);

    // 2. Check bit 3
    bool check = (flags & (1 << 3)) != 0;
    printf("Bit 3 is %s\n", check ? "set" : "clear");

    // 3. Toggle bit 3
    flags ^= (1 << 3);
    printf("After toggle: %d (expected 0)\n", flags);

    // 4. Clear bit 3 (idempotent)
    flags &= ~(1 << 3);
    printf("After clear:  %d (expected 0)\n", flags);

    return 0;
}
```

Compile and run:

```bash
gcc -std=c17 main.c -o main && ./main
```

Output:

```text
After set:   8 (expected 8)
Bit 3 is set
After toggle: 0 (expected 0)
After clear:  0 (expected 0)
```

The results match our expectations exactly. If you find the `(flags & (1 << 3)) != 0` syntax unintuitive, you can wrap it in a macro:

```c
#define CHECK_BIT(val, bit) (((val) & (1 << (bit))) != 0)
```

> ⚠️ **Pitfall Warning**
> We added parentheses around every parameter and the entire expression in the macro definition. This isn't redundant. Without them, `CHECK_BIT(flags, 3 + 1)` would expand to `flags & 1 << 3 + 1 != 0`. Because `+` has higher precedence than `<<` and `&`, the meaning changes completely. Parentheses in macros are the cheapest insurance.

## Step 3 — Shift Precautions

### Behavior of Left and Right Shifts

Left shift `<<` has well-defined behavior on unsigned numbers—low bits are filled with 0, and high bits are discarded. Right shift `>>` is also well-defined for unsigned numbers (high bits filled with 0).

However, right shift on **signed** integers is **implementation-defined**—the compiler can choose arithmetic right shift (high bits filled with the sign bit to preserve negativity) or logical right shift (high bits filled with 0). Most platforms use arithmetic right shift, but this is not guaranteed by the standard:

```cpp
#include <stdio.h>
#include <stdint.h>

int main(void) {
    int8_t signed_val = -8; // 0b11111000
    uint8_t unsigned_val = 248; // 0b11111000

    printf("Signed >> 1:   %d\n", signed_val >> 1); // Usually -4 (0b11111100)
    printf("Unsigned >> 1: %d\n", unsigned_val >> 1); // 124 (0b01111100)

    return 0;
}
```

> ⚠️ **Pitfall Warning**
> If the shift amount is negative, or equal to/greater than the bit width of the type (e.g., shifting a 32-bit integer by 32 bits), the behavior is **undefined**. Intuitively, you might think `1 << 32` results in 0, but the standard dictates this is UB—in practice, you might get 1 (because the CPU only takes the lower 5 bits of the shift amount, so 32 becomes 0).

### Bitwise Operator Precedence Traps

This is the most common pitfall for beginners—**bitwise operators have lower precedence than relational operators**. This means `&`, `^`, `|` all have lower precedence than `==`, `!=`, `<`, `>`.

```cpp
// Wrong: Checks if (flags & 1) is non-zero, then compares result to 0
if (flags & 1 == 0) { ... }

// Correct: Explicitly groups the bitwise operation
if ((flags & 1) == 0) { ... }
```

The problem with the first version is that `1` is combined with `== 0` first (because `==` has higher precedence than `&`), resulting in 0 (since `1 == 0` is false). Then `flags & 0` is always 0, so the condition is always false.

Core principle: **Whenever bitwise operations and comparisons are mixed, use parentheses**. Parentheses don't slow down your code, but they save you from these precedence traps.

A practical precedence mnemonic, from high to low:

1. Parentheses `()` > Subscript `[]` > Member access `.` `->`
2. Unary operators (`!` `~` `++` `--` `+` `-` `*` `&` `sizeof`)
3. Arithmetic (`*` `/` `%` > `+` `-`)
4. Shift (`<<` `>>`)
5. Relational (`<` `<=` `>` `>=` > `==` `!=`)
6. Bitwise (`&` > `^` > `|`)
7. Logical (`&&` > `||`)
8. Ternary `?` > Assignment `=` > Comma `,`

## Step 4 — Evaluation Order and Sequence Points

This is one of the most confusing concepts in C. Let's understand it by distinguishing two things: **precedence** and **evaluation order**. These are independent—precedence determines how operators bind operands, while evaluation order determines when operands are calculated.

### Evaluation Order is Unspecified

In most expressions, the order in which operands are evaluated is decided by the compiler. For example, in `func_a() + func_b()`, the standard does not specify whether `func_a()` or `func_b()` is called first—the compiler can choose any order. If the functions have no side effects (don't modify global variables or read/write files), the order doesn't matter; but if they do, results may vary by compiler.

### Sequence Points — The Safety Boundary for Side Effects

A **sequence point** is a specific point in program execution where all previous operations are guaranteed to be complete, and subsequent operations haven't started yet. Sequence points in C include:

- After the left operand of `&&` (this is the basis of short-circuit evaluation).
- After the left operand of `||`.
- After the first operand of `? :`.
- After the left operand of the comma operator.
- At the end of a full expression (the semicolon at the end of a statement).
- After all arguments are evaluated but before the function body executes.

### Undefined Behavior: No Sequence Point Between Two Modifications

If a variable is modified twice within one sequence point, or is modified while being read (where the read isn't used to compute the new value), it is **undefined behavior**:

```c
int i = 0;
i = i++; // UB: Modified twice without sequence point
arr[i++] = i; // UB: i is modified and read in different sub-expressions
```

> ⚠️ **Pitfall Warning**
> These bugs are particularly insidious because code might "look fine" on one compiler, but break when switching compilers or enabling optimizations. In an interview, if you see `i = i++`, the correct answer is "This is UB, there is no standard answer," rather than guessing what the compiler will do.

If you want to understand UB deeply, think of it like traffic rules: the standard says "don't run a red light." If you do, the consequences are unpredictable—you might be fine, you might get a ticket, or you might crash. UB is "running a red light" in the programming world.

## C++ Connection

C++ has done several useful things regarding bitwise operations. `std::bitset` allows direct access to individual bits using the `[]` operator, and provides `set`, `reset`, `flip`, `test` operations with clear semantics—safer and more readable than manual bitwise operations. In C++, prefer `std::bitset` unless you need extreme performance or direct hardware manipulation.

Regarding evaluation order, C++17 strengthened the rules—function expressions are guaranteed to be evaluated before arguments, making it more deterministic than C's "unspecified." Additionally, `constexpr` functions trigger a compiler error if they cause UB at compile time—effectively a free UB detector.

## Summary

The four classic bitwise operations—Set (`|` + `<<`), Clear (`&` + `~` + `<<`), Toggle (`^` + `<<`), and Check (`&` + `<<`)—are essential skills for embedded development. The biggest pitfall in operator precedence is that bitwise operators have lower precedence than relational operators, so parentheses are mandatory when mixing them. The core principle of evaluation order and sequence points is: never modify the same variable multiple times within the same expression—that is undefined behavior.

At this point, we have covered all aspects of C operators. Next, we will learn about control flow—how to make programs execute different code based on conditions and how to repeat code blocks.

## Exercises

### Exercise 1: Bit Manipulation Toolkit

Implement the following bit manipulation functions:

```c
#include <stdint.h>
#include <stdbool.h>

// Set the nth bit of val (0-indexed)
void bit_set(uint32_t *val, uint8_t n);

// Clear the nth bit of val
void bit_clear(uint32_t *val, uint8_t n);

// Toggle the nth bit of val
void bit_toggle(uint32_t *val, uint8_t n);

// Return true if the nth bit is set
bool bit_check(uint32_t val, uint8_t n);
```

### Exercise 2: Safe Shift

Write a function to safely perform a left shift, handling all boundary cases:

```c
#include <stdint.h>
#include <stdbool.h>

// Safely left shift val by n bits.
// Returns false if n is too large or negative, true on success.
bool safe_shift_left(uint32_t *result, uint32_t val, int n);
```

### Exercise 3: Expression Analysis

Analyze the evaluation behavior of the following expressions (without running them), marking each as "well-defined", "unspecified behavior", or "undefined behavior":

```c
int a = 1, b = 2, c = 3;
int arr[10] = {0};

1. a + b
2. a++ + b
3. arr[a++] = a
4. (a = b) + (b = a)
5. a = b + c
```

## References

- [cppreference: C Operator Precedence](https://en.cppreference.com/w/c/language/operator_precedence)
- [cppreference: Sequence Points](https://en.cppreference.com/w/c/language/eval_order)
- [CERT: EXP30-C - Do not depend on the order of of evaluation for side effects](https://wiki.sei.cmu.edu/confluence/display/c/EXP30-C.+Do+not+depend+on+the+order+of+evaluation+for+side+effects)
