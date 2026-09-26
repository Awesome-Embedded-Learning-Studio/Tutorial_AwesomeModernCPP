---
chapter: 1
cpp_standard:
- 11
description: A deep dive into the four classic bitwise operations, the caveats of
  shifting, operator precedence traps, evaluation order and sequence points, and
  the nature of undefined behavior.
difficulty: beginner
order: 5
platform: host
prerequisites:
- 'Operator Basics: Making Data Move'
reading_time_minutes: 10
tags:
- host
- cpp-modern
- beginner
- 入门
title: Bitwise Operations and Evaluation Order
translation:
  source: documents/vol1-fundamentals/c_tutorials/03B-bitwise-and-evaluation.md
  source_hash: 252a3c9888c9a92c618e21f41242e799b9f22ad0311a28d5bfa47c26cacc102e
  translated_at: '2026-09-25T12:58:25+00:00'
  engine: anthropic
  token_count: 7500
---
# Bitwise Operations and Evaluation Order

In the previous post we walked through the everyday operators—arithmetic, relational, logical. Now we come to two tougher bones to chew on: bitwise operations and evaluation order. Bitwise operations don't come up much in ordinary application-level programming, but if you ever move into embedded development or low-level systems programming, they become your daily tools—configuring hardware registers, parsing bit fields out of communication protocols, implementing sets of flag bits, all of it rests on them. Evaluation order and sequence points, in turn, are the key to understanding why some code produces different results on different compilers.

Honestly, both topics feel a bit twisty the first time you meet them. But don't worry—we'll take it one step at a time, starting with the more intuitive of the two: bitwise operations.

## Step 1 — Meet the Bitwise Operators

### What Is a Bit

When we covered data types, we mentioned that a variable's value is stored in memory as 0s and 1s. A `uint8_t` has 8 binary digits; a `uint32_t` has 32. Bitwise operations manipulate those binary digits directly—you stop treating data as "numbers" and start treating it as "a row of switches".

C provides six bitwise operators:

| Operator | Meaning | Intuition |
|----------|---------|-----------|
| `&` | bitwise AND | 1 only if both are 1 |
| `\|` | bitwise OR | 1 if either is 1 |
| `^` | bitwise XOR | 1 if different, 0 if same |
| `~` | bitwise NOT | 0 becomes 1, 1 becomes 0 |
| `<<` | left shift | all bits move left, 0s fill the low end |
| `>>` | right shift | all bits move right, 0s fill the high end (unsigned) |

Let's demonstrate with 8-bit unsigned values—that's the most intuitive way to see it:

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
  0b00110011  (51)    (8-bit NOT)
```

## Step 2 — The Four Classic Moves: Set, Clear, Toggle, Check

Four operation patterns dominate bitwise work in embedded development—you need to know them by heart.

### Set — Forcing a Bit to 1

To set a bit to 1, combine OR with a left shift. The reasoning: `0 | 1 = 1` and `1 | 1 = 1`—OR anything with 1 and the result is guaranteed to be 1; OR the other bits with 0 and they stay unchanged.

```c
uint8_t reg = 0x00;       // 00000000
reg |= (1 << 3);          // set bit 3 to 1 → 00001000 = 0x08
reg |= (1 << 0);          // set bit 0 to 1 → 00001001 = 0x09

// set several bits at once
reg |= 0x07;              // set bits 0, 1, and 2 → 00001111 = 0x0F
```

### Clear — Forcing a Bit to 0

To clear a bit to 0, combine AND with NOT. The reasoning: `x & 1 = x` and `x & 0 = 0`—AND anything with 0 and it becomes 0; AND with 1 and it stays the same.

```c
uint8_t reg = 0x0F;       // 00001111
reg &= ~(1 << 3);         // clear bit 3 → 00000111 = 0x07
```

`~(1 << 3)` evaluates to `0xF7` (`11110111`); AND it with `0x0F` and bit 3 becomes 0 while every other bit is untouched.

### Toggle — Flipping a Bit

To toggle a bit, use XOR. The reasoning: `x ^ 1 = ~x` (flipped) and `x ^ 0 = x` (unchanged).

```c
uint8_t reg = 0x07;       // 00000111
reg ^= (1 << 0);          // toggle bit 0 → 00000110 = 0x06
```

### Check — Reading Whether a Bit Is 0 or 1

To check a bit's value, AND with a left-shifted 1 and see whether the result is non-zero:

```c
uint8_t reg = 0x06;       // 00000110
if (reg & (1 << 1)) {
    // bit 1 is 1 (and indeed it is: bit 1 of 00000110 is 1)
}
if (reg & (1 << 0)) {
    // bit 0 is 0 (this branch is never taken)
}
```

To verify, let's chain all four operations together and run them:

```c
#include <stdio.h>
#include <stdint.h>

/// @brief Print a uint8_t in binary
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

    reg |= (1 << 3);       // set bit 3
    printf("置位第3位:    "); print_binary(reg);

    reg |= 0x07;           // set bits 0, 1, and 2
    printf("置位0,1,2位:  "); print_binary(reg);

    reg &= ~(1 << 3);      // clear bit 3
    printf("清零第3位:    "); print_binary(reg);

    reg ^= (1 << 0);       // toggle bit 0
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

A perfect match with what we expected. If you find the `(1 << n)` spelling less than intuitive, you can wrap it up in macros:

```c
#define BIT(n)              (1U << (n))
#define SET_BIT(x, n)       ((x) |= BIT(n))
#define CLEAR_BIT(x, n)     ((x) &= ~BIT(n))
#define TOGGLE_BIT(x, n)    ((x) ^= BIT(n))
#define CHECK_BIT(x, n)     (((x) & BIT(n)) != 0)
```

Every parameter and the overall expression in those macro definitions is wrapped in parentheses—and that is not busywork. Without the parentheses, `CLEAR_BIT(x | y, 3)` would expand to `x | y &= ~(1 << 3)`, and since `&=` binds looser than `|`, the meaning changes completely. Parentheses inside macros are the cheapest insurance you can buy.

## Step 3 — What to Watch Out for When Shifting

### How Left and Right Shifts Behave

Left shift `<<` on unsigned values is well-defined—0s enter at the low end, the high bits fall off. Right shift `>>` on unsigned values is well-defined too (0s enter at the high end).

Right shift of **signed** values, however, is **implementation-defined**—the compiler may choose arithmetic shift (fill the high end with the sign bit, keeping negatives negative) or logical shift (fill with 0s). Most platforms use arithmetic shift, but the standard does not guarantee it:

```c
int8_t x = -4;         // binary: 11111100
int8_t y = x >> 1;     // could be -2 (arithmetic shift, 1s fill the high end)
                        // could be 126 (logical shift, 0s fill the high end)
                        // most platforms give the former, but it is not guaranteed
```

If the shift count is negative, or equal to or beyond the type's width (say, shifting an `int32_t` by 32), the behavior is **undefined**. Intuitively you might expect `1 << 32` to be 0, but the standard says this is UB—in practice you may well get 1 (because the CPU looks only at the low 5 bits of the shift count, turning 32 into 0).

### The Precedence Trap in Bitwise Operations

This is the pit beginners most reliably fall into—**every bitwise operator has lower precedence than the relational operators**. In other words, `&`, `|`, and `^` all bind looser than `==`, `!=`, `<`, and `>`.

```c
if (flags & 0x0F == 0) { }    // actually parsed as flags & (0x0F == 0)
                                // that is, flags & 0 — always false!
if ((flags & 0x0F) == 0) { }  // this is what you meant
```

The problem with the first version is that `==` grabs `0x0F` and `0` first (because `==` outranks `&`), the comparison yields 0 (because `0x0F != 0`), and then `flags & 0` is always false.

The core rule: **whenever bitwise operations mix with comparisons, add parentheses**. Parentheses don't make code slower, and they save you from this kind of precedence trap.

A practical precedence mnemonic, from highest to lowest:

1. Parentheses `()` > subscript `[]` > member access `.` `->`
2. Unary operators (`!` `~` `++` `--` `*` `&` `sizeof`)
3. Arithmetic (`*` `/` `%` > `+` `-`)
4. Shifts (`<<` `>>`)
5. Relational (`<` `>` `<=` `>=` > `==` `!=`)
6. Bitwise (`&` > `^` > `|`)
7. Logical (`&&` > `||`)
8. Conditional `?:` > assignment `=` > comma `,`

## Step 4 — Evaluation Order and Sequence Points

This is one of the most confusing corners of C. Let's separate two things: **precedence** and **evaluation order**. They are independent—precedence decides how operators bind to their operands; evaluation order decides when those operands get computed.

### Evaluation Order Is Unspecified

In most expressions, the order in which operands are evaluated is the compiler's call. Take `f() + g()`: the standard doesn't say whether `f` or `g` runs first—the compiler may pick either order. If neither function has side effects (no global variables modified, no files read or written), the order doesn't matter; but if there are side effects, the result can differ from compiler to compiler.

### Sequence Points — The Safety Boundary for Side Effects

A **sequence point** is a specific point in a program's execution where everything before it has finished and nothing after it has started. C's sequence points include:

- after the left operand of `&&` is evaluated (that's what makes short-circuit evaluation work)
- after the left operand of `||` is evaluated
- after the first operand of `?:` is evaluated
- after the left operand of the comma operator is evaluated
- at the end of a full expression (the semicolon at the end of a statement)
- in a function call, after all arguments have been evaluated and before execution of the function body begins

### Undefined Behavior: Two Modifications Without a Sequence Point Between Them

If the same variable is modified twice between sequence points, or is modified and read at the same time (where the read isn't part of computing the new value), that's **undefined behavior**:

```c
int i = 3;

i = i++;                  // UB: i is assigned and incremented at the same time
a[i] = i++;               // UB: i is read and modified at the same time
printf("%d %d", i++, i++); // UB: i is modified twice, no sequence point between the arguments

// correct versions
i = i + 1;    // OK: modified only once
i++;          // OK: used on its own
```

These bugs are particularly sneaky because they can "look fine" on one compiler and then break when you switch compilers or turn on optimization. If an interview throws `i = i++` at you, the correct answer is "this is UB, there is no standard answer"—not an attempt to guess how the compiler handles it.

If you want a deeper feel for what UB is, compare it to traffic rules: the standard says "don't run a red light"; if you run one, the consequences are unpredictable—you might get away clean, might get caught on camera and fined, might cause a crash. UB is the programming world's equivalent of running a red light.

## C++ Transition

C++ does a few useful things on the bitwise front. `std::bitset<N>` from `<bitset>` lets you access individual bits directly with the `[]` operator, and it offers operations with unambiguous semantics such as `test()`, `set()`, `reset()`, and `flip()`—safer and more readable than hand-rolled bitwise code. In C++, prefer `std::bitset` unless you genuinely need the last drop of performance or direct hardware access.

On evaluation order, C++17 tightened the rules—the function expression is now evaluated before the arguments, which is more deterministic than C's "unspecified". And if evaluating a `constexpr` function at compile time runs into UB, the compiler rejects it outright—a free UB detector.

## Exercises

### Exercise 1: A Bit-Manipulation Toolkit

**Difficulty: Intermediate** · set, clear, toggle, and extract a bit field

Implement the following bit-manipulation functions:

```c
/// @brief Set bit n of value to 1
uint32_t bit_set(uint32_t value, int n);

/// @brief Clear bit n of value to 0
uint32_t bit_clear(uint32_t value, int n);

/// @brief Toggle bit n of value
uint32_t bit_toggle(uint32_t value, int n);

/// @brief Extract the [high:low] bit field of value (both ends inclusive)
uint32_t bit_extract(uint32_t value, int high, int low);
```

::: details Reference Solution

```c
/// @brief Set bit n of value to 1
uint32_t bit_set(uint32_t value, int n) {
    return value | (1U << n);
}

/// @brief Clear bit n of value to 0
uint32_t bit_clear(uint32_t value, int n) {
    return value & ~(1U << n);
}

/// @brief Toggle bit n of value
uint32_t bit_toggle(uint32_t value, int n) {
    return value ^ (1U << n);
}

/// @brief Extract the [high:low] bit field of value (both ends inclusive)
uint32_t bit_extract(uint32_t value, int high, int low) {
    uint32_t width = high - low + 1;
    value = value >> low;
    uint64_t mask = (1ULL << width) - 1;
    return value & mask;
}
```

In `bit_extract`, the mask uses `1ULL` to dodge the shift overflow of `1U << 32` when `width == 32`—this kind of detail shows up constantly in bit-field work.

:::

### Exercise 2: Safe Shifting

**Difficulty: Basic** · bounds checks that block shift UB

Write a function that performs a left shift safely, handling every edge case:

```c
/// @brief A safe left shift
/// @param val the value to shift
/// @param n the shift count
/// @param bits the width of the type (e.g., 32)
/// @return the shift result, or 0 for an illegal shift count
uint32_t safe_shift_left(uint32_t val, int n, int bits);
```

::: details Reference Solution

```c
#include <stdint.h>

uint32_t safe_shift_left(uint32_t val, int n, int bits) {
    // bits must be a valid width, and n must fall within [0, bits)
    if (bits <= 0 || bits > 32 || n < 0 || n >= bits) {
        return 0;
    }
    return val << n;
}
```

As long as `n < bits` and `bits <= 32` hold, `val << n` never triggers shift-overflow UB of this kind.

:::

### Exercise 3: Analyzing Expressions

**Difficulty: Basic** · identifying sequence points and undefined behavior

Analyze the evaluation behavior of the following expressions (without actually running them), and label each one as "well-defined", "unspecified behavior", or "undefined behavior":

```c
int a = 5, b = 3;
int r1 = a++ + b;            // ?
int r2 = a++ + ++a;          // ?
int r3 = (a > b) ? a-- : b--; // ?
printf("%d %d\n", a++, a++);  // ?
```

::: details Reference Solution

```c
int a = 5, b = 3;
int r1 = a++ + b;             // well-defined: a is modified only once, b is only read
int r2 = a++ + ++a;           // undefined behavior: a is modified twice between sequence points
int r3 = (a > b) ? a-- : b--; // well-defined: there is a sequence point after ?:'s first operand, and only one branch is evaluated
printf("%d %d\n", a++, a++);  // undefined behavior: no sequence point between function arguments, a is modified twice
```

The easy one to trip on is `r3`: it looks like both sides decrement, but the conditional operator evaluates only the branch that holds, and there is a sequence point after `?:`'s first operand, so it is safe. The `printf` line does contain commas—but the comma between function arguments is not the comma operator, there is no sequence point there, and the two `a++` sit with no sequence point between them → UB.

:::

## References

- [cppreference: C operator precedence](https://en.cppreference.com/w/c/language/operator_precedence)
- [cppreference: sequence points](https://en.cppreference.com/w/c/language/eval_order)
- [CERT: EXP30-C - Do not depend on the order of evaluation for side effects](https://wiki.sei.cmu.edu/confluence/display/c/EXP30-C.+Do+not+depend+on+the+order+of+evaluation+for+side+effects)
