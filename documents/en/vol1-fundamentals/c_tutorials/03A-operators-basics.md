---
chapter: 1
cpp_standard:
- 11
description: Master C's arithmetic operators, increment and decrement, relational
  and logical operators, the conditional operator, and the comma operator, and understand
  short-circuit evaluation and how assignment operators are used.
difficulty: beginner
order: 4
platform: host
prerequisites:
- Floating Point, Characters, const, and Type Conversion
reading_time_minutes: 9
tags:
- host
- cpp-modern
- beginner
- 入门
- 基础
title: 'Operator Basics: Making Data Move'
translation:
  source: documents/vol1-fundamentals/c_tutorials/03A-operators-basics.md
  source_hash: dbf8b30611f1b0a36ac4fcae88057af4bf2cd28c58bc98bfa713a6a100defca0
  translated_at: '2026-09-25T12:41:22+00:00'
  engine: anthropic
  token_count: 5400
---
# Operator Basics: Making Data Move

In the previous post we took C's data types apart from the inside out—how integers are stored, how decimals are stored, how characters are stored. But data alone isn't enough; we also need to make the data "move": do arithmetic, compare sizes, test truth. In C, these operations are carried out by **operators**.

You can think of operators as the "verbs" of C—variables and constants are the nouns, operators connect them into expressions, expressions combine into statements, and statements make up a program. Day to day, we really only use a handful of operators, but every one of them has its own little temperament. In this post we'll walk through the most common arithmetic, relational, and logical operators, with a sharp eye on the spots where it's easy to step into a pit. Bitwise operations and the deeper question of evaluation order get a post of their own next time.

## Step 1 — Add, Subtract, Multiply, Divide: Arithmetic Operators

### The Five Basic Operators

C provides five basic arithmetic operators: `+` (addition), `-` (subtraction), `*` (multiplication), `/` (division), and `%` (modulo). The first four work on all numeric types; the modulo operator `%` works only on integers.

```c
int a = 10 + 3;    // 13
int b = 10 - 3;    // 7
int c = 10 * 3;    // 30
int d = 10 / 3;    // 3 (integer division; the fractional part is simply discarded)
int e = 10 % 3;    // 1 (the remainder of 10 divided by 3)
```

Here is a trap beginners fall into very easily: **when two integers are divided, the result is still an integer**. `10 / 3` is not `3.333...`—it is `3`. The fractional part is discarded outright, not rounded.

If you want a division result that keeps its decimals, at least one operand must be a floating-point number. `10 / 3` gives `3`, but `10.0 / 3` or `10 / 3.0` gives `3.333...`.

### Dividing Negatives: Truncation Toward Zero

The C99 standard states it explicitly: integer division truncates toward zero. In other words, once the fractional part is discarded, the result moves toward zero. `7 / 2` is `3`, and `-7 / 2` is `-3` (not `-4`). With modulo, the remainder takes the sign of the dividend: `-7 % 2` is `-1`.

```c
int a = 7 / 2;    // 3
int b = -7 / 2;   // -3 (truncation toward zero)
int c = -7 % 2;   // -1 (the remainder takes the dividend's sign)
```

Let's verify:

```bash
gcc -Wall -Wextra -std=c17 div_demo.c -o div_demo && ./div_demo
```

Output:

```text
7 / 2 = 3
-7 / 2 = -3
-7 %% 2 = -1
```

## Step 2 — Increment and Decrement: Two Special Operators

### Prefix vs. Postfix

`++` (increment) and `--` (decrement) are two rather special operators in C—they can go in front of a variable (prefix) or after it (postfix). Used on their own, the two forms have exactly the same effect; their behavior differs when they are mixed inside an expression.

Here's an analogy: prefix `++x` is like "raise the price first, then check out"—add 1 to the value first, then hand back the new value; postfix `x++` is like "check out first, then raise the price"—hand back the current value first, and then add 1.

```c
int x = 5;
int a = ++x;  // x becomes 6 first, and a gets 6
int b = x++;  // b gets 6 first, then x becomes 7
printf("a=%d, b=%d, x=%d\n", a, b, x);
```

Output:

```text
a=6, b=6, x=7
```

### Never Write It Like This

There is something very important we need to remind you of—**do not apply `++`/`--` to the same variable multiple times within the same expression**:

```c
int i = 3;
int a = i++ + ++i;  // undefined behavior!
```

The C standard classifies this kind of code as **undefined behavior** (UB for short). Simply put, the standard says "you may not write this," and the compiler is free to handle it in any way it likes—different compilers may give completely different results. As for why it is UB, we will explain in detail in the next post when we discuss sequence points. For now, just remember: **do not use `++` or `--` twice on the same variable in one expression**.

`i = i++`, `a[i] = i++`, `printf("%d %d", i++, i++)`—all of these are undefined behavior. When you see something like this in an interview question, knowing it is UB is enough; don't try to guess "what the answer is"—because there is no correct answer.

## Step 3 — Comparing and Judging: Relational and Logical Operators

### Relational Operators

Relational operators compare the magnitude relationship between two values, and the result is "true" or "false". In C, "true" is represented by the integer `1`, and "false" by the integer `0`.

```c
int a = (5 > 3);    // 1 (true)
int b = (5 < 3);    // 0 (false)
int c = (5 == 5);   // 1 (equal)
int d = (5 != 5);   // 0 (not equal)
```

One common typo is writing `==` (equality comparison) as `=` (assignment). `if (x = 5)` is always true (because the value of the assignment expression is 5, and non-zero counts as true), and `x` gets accidentally modified along the way. Good compilers will warn about this pattern, so it is a good idea to enable `-Wall` and let the compiler keep watch for you.

### Logical Operators

There are three logical operators: `&&` (logical AND), `||` (logical OR), and `!` (logical NOT). They operate on truth values—treating their operands as boolean values, where zero is false and non-zero is true.

```c
if (age >= 18 && age <= 65) {
    // age is between 18 and 65
}
if (score < 0 || score > 100) {
    // score is outside the valid range
}
if (!is_valid) {
    // runs when is_valid is false
}
```

### Short-Circuit Evaluation: A Very Practical Feature

`&&` and `||` have a very important property called **short-circuit evaluation**. For `&&`, if the left operand is false, the right operand is not evaluated at all—because the whole expression is already false, and nothing on the right can affect the result. `||` is exactly the opposite: when the left operand is true, the right side is not evaluated.

This property is extremely useful in real programming. The most classic scenario is checking whether a pointer is null before accessing what it points to:

```c
// Dereference the pointer safely
if (ptr != NULL && ptr->value > 0) {
    // If ptr is NULL, ptr->value is never accessed
    // This avoids the crash a null-pointer dereference would cause
}
```

If `ptr` is a null pointer, `ptr != NULL` is false, and thanks to short-circuit evaluation `ptr->value` is never evaluated—the program stays safe. Without short-circuit evaluation, the code would try to access `ptr->value` even when `ptr` is null, and the program would crash on the spot.

Let's verify the effect of short-circuit evaluation:

```c
#include <stdio.h>

int counter = 0;

int increment(void)
{
    counter++;
    printf("increment() 被调用了，counter = %d\n", counter);
    return counter;
}

int main(void)
{
    int result = (0 && increment());  // the left side is 0 (false); the right side never runs
    printf("result = %d, counter = %d\n", result, counter);

    result = (1 || increment());      // the left side is 1 (true); the right side never runs
    printf("result = %d, counter = %d\n", result, counter);

    return 0;
}
```

Output:

```text
result = 0, counter = 0
result = 1, counter = 0
```

Nice—`increment()` was never called even once. Short-circuit evaluation did its job.

## Step 4 — The Conditional Operator and the Comma Operator

### The Conditional Operator `?:`

The conditional operator is the only ternary operator in C, with the syntax `condition ? expr1 : expr2`. If `condition` is true, the value of the whole expression is `expr1`; otherwise it is `expr2`.

You can think of it as a "condensed if-else"—especially handy when you need to pick a value based on a condition but don't want to write a full if-else statement:

```c
int max = (a > b) ? a : b;                  // pick the larger value
const char* label = (count == 1) ? "item" : "items";  // singular vs. plural
```

The conditional operator can be nested, but past two levels readability starts to suffer:

```c
const char* grade = (score >= 90) ? "A" :
                   (score >= 80) ? "B" :
                   (score >= 60) ? "C" : "F";
```

### The Comma Operator

The comma operator `,` has the lowest precedence of any operator in C. It evaluates its two operands from left to right, and the value of the whole expression is the value of the right operand:

```c
int a = (1, 2, 3);  // evaluate 1, then 2, then 3; a = 3
```

You will rarely use this operator on its own; its most common use is maintaining multiple variables at once in a `for` loop:

```c
for (int i = 0, j = n - 1; i < j; i++, j--) {
    int tmp = arr[i];
    arr[i] = arr[j];
    arr[j] = tmp;
}
```

Note that the comma in `int i = 0, j = n - 1` is a declaration separator (not the comma operator), but the comma in `i++, j--` really is the comma operator.

## C++ Transition

C++ does two important things with operators. The first is introducing the C++ version of `<stdbool.h>`—`bool`, `true`, and `false` are built-in language keywords in C++, unlike the macros they are in C. The second is operator overloading—you can define the behavior of operators such as `+` and `==` for your own types, so custom types feel as natural to use as the built-in ones.

But there is one important restriction: although C++ allows overloading `&&` and `||`, **overloading them throws away the short-circuit evaluation property**. Because an overloaded operator is essentially a function call, both arguments get evaluated, and the short-circuit behavior is gone. So in practice, never overload `&&` and `||`.

## Exercises

### Exercise 1: Predicting Integer Division

**Difficulty: Basic** · truncation toward zero and the sign of the remainder

Without actually running it, predict the value of each expression below, then write a program to verify:

```c
printf("%d\n", 7 / 2);
printf("%d\n", -7 / 2);
printf("%d\n", 7 / -2);
printf("%d\n", 7 % 2);
printf("%d\n", -7 % 2);
```

::: details Reference solution

```text
 3    //  7 / 2 = 3.5; integer division truncates toward zero, chopping off the fractional part → 3
-3    // -7 / 2 = -3.5; same truncation toward zero → -3 (not -4)
-3    //  7 / -2 = -3.5; truncation toward zero → -3
 1    //  7 % 2 = 1: 7 = 3×2 + 1
-1    // -7 % 2 = -1: the remainder follows the dividend's sign; -7 = (-3)×2 + (-1)
```

Key point: since C99, both `/` and `%` truncate toward zero (truncation toward zero)—the rule is the same for positive and negative numbers, and the fractional part is simply dropped. That is why `-7 / 2` gives `-3` rather than `-4`, and why the sign of `-7 % 2` turns negative along with the dividend.

:::

### Exercise 2: Short-Circuit Evaluation in Practice

**Difficulty: Intermediate** · guard against out-of-bounds array access with short-circuit evaluation

Write a function that safely finds the first element in an array greater than a given value. Use short-circuit evaluation to make sure it never goes out of bounds:

```c
/// @brief Find the first element in the array greater than threshold
/// @param arr the array
/// @param len the array length
/// @param threshold the threshold value
/// @return the index of the element found, or -1 if not found
int find_first_above(const int* arr, size_t len, int threshold);
```

::: details Reference solution

```c
#include <stddef.h>

int find_first_above(const int* arr, size_t len, int threshold) {
    // If the array is NULL or the length is 0, return -1 right away
    if (arr == NULL || len == 0) {
        return -1;
    }

    size_t i = 0;

    // Short-circuit evaluation guards the bound: check i < len first, and only then access arr[i]
    while (i < len && arr[i] <= threshold) {
        i++;
    }

    // After the loop, i < len means we found one; otherwise we scanned the whole array without a hit
    return (i < len) ? (int)i : -1;
}
```

The heart of it is the line `while (i < len && arr[i] <= threshold)`: `&&` short-circuits, so when `i < len` is false the code never even reads `arr[i]`—the out-of-bounds access is stopped at the door.

:::

## References

- [cppreference: C operator precedence](https://en.cppreference.com/w/c/language/operator_precedence)
- [cppreference: arithmetic operators](https://en.cppreference.com/w/c/language/operator_arithmetic)
