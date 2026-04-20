---
chapter: 1
cpp_standard:
- 11
description: Master C arithmetic operators, increment and decrement operators, relational
  and logical operators, the conditional operator, and the comma operator, and understand
  short-circuit evaluation and the usage of assignment operators.
difficulty: beginner
order: 4
platform: host
prerequisites:
- 浮点、字符、const 与类型转换
reading_time_minutes: 11
tags:
- host
- cpp-modern
- beginner
- 入门
- 基础
title: 'Operator Basics: Making Data Move'
translation:
  source: documents/vol1-fundamentals/c_tutorials/03A-operators-basics.md
  source_hash: 2057953d278a84e09f9679e6d1a760fec6325b73ffda5a5048b6ab3a29fc49b7
  translated_at: '2026-04-20T03:14:13.332063+00:00'
  engine: anthropic
  token_count: 1503
---
# Operator Basics: Making Data Move

In the previous chapter, we took C's data types apart from the inside out—how integers are stored, how floating-point numbers are stored, how characters are stored. But data alone isn't enough; we also need to make data "move": performing addition, subtraction, multiplication, and division, comparing sizes, and evaluating true or false. In C, these operations are handled by **operators**.

You can think of operators as the "verbs" of C—variables and constants are nouns, operators connect them into expressions, expressions combine into statements, and statements form programs. In day-to-day programming, we only use a handful of operators, but each one has its own quirks. In this chapter, we'll walk through the most commonly used arithmetic, relational, and logical operators, focusing on the pitfalls that are easy to stumble into. We'll save bitwise operations and the deeper issues of evaluation order for the next chapter.

> **Learning Objectives**
> After completing this chapter, you will be able to:
>
> - [ ] Proficiently use the five arithmetic operators and the increment/decrement operators
> - [ ] Understand the "round toward zero" rule of integer division
> - [ ] Master the short-circuit evaluation behavior of relational and logical operators
> - [ ] Correctly use the conditional operator and the comma operator

## Environment Setup

All of our upcoming experiments will run in this environment:

- Platform: Linux x86\_64 (WSL2 is also fine)
- Compiler: GCC 13+ or Clang 17+
- Compiler flags: `-Wall -Wextra -std=c17`

## Step 1 — Addition, Subtraction, Multiplication, and Division: Arithmetic Operators

### The Five Basic Operators

C provides five basic arithmetic operators: `+` (addition), `-` (subtraction), `*` (multiplication), `/` (division), and `%` (modulo). The first four apply to all numeric types, while the modulo operator `%` only works with integers.

```c
int a = 10 + 3;    // 13
int b = 10 - 3;    // 7
int c = 10 * 3;    // 30
int d = 10 / 3;    // 3（整数除法，小数部分直接丢弃）
int e = 10 % 3;    // 1（10 除以 3 的余数）
```

Here is a pitfall that beginners often stumble into: **dividing two integers yields an integer result**. `10 / 3` is not `3.333...`, but `3`. The fractional part is discarded directly—it is not rounded.

> ⚠️ **Pitfall Warning**
> If you want a floating-point division result, at least one operand must be a floating-point number. `10 / 3` yields `3`, but `10.0 / 3` or `10 / 3.0` yields `3.333...`.

### Negative Number Division: Rounding Toward Zero

The C99 standard explicitly specifies that integer division rounds toward zero. In other words, after the fractional part is discarded, the result moves toward zero. `7 / 2` is `3`, and `-7 / 2` is `-3` (not `-4`). The sign of the modulo result matches the sign of the dividend: `-7 % 2` is `-1`.

```c
int a = 7 / 2;    // 3
int b = -7 / 2;   // -3（向零取整）
int c = -7 % 2;   // -1（余数符号与被除数相同）
```

Let's verify this:

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

`++` (increment) and `--` (decrement) are rather special operators in C—they can be placed before a variable (prefix) or after it (postfix). When used alone, both have the same effect, but their behavior differs when mixed into larger expressions.

Here's an analogy to help you understand: prefix `++x` is like "raise the price first, then check out"—it adds 1 to the value first, then returns the new value. Postfix `x++` is like "check out first, then raise the price"—it returns the current value first, then adds 1.

```c
int x = 5;
int a = ++x;  // x 先变成 6，a 得到 6
int b = x++;  // b 先得到 6，然后 x 变成 7
printf("a=%d, b=%d, x=%d\n", a, b, x);
```

Output:

```text
a=6, b=6, x=7
```

### Never Write It This Way

There is a very important thing to keep in mind—**never use `++`/`--` on the same variable multiple times within a single expression**:

```c
int i = 3;
int a = i++ + ++i;  // 未定义行为！
```

This pattern is **undefined behavior (UB)** in the C standard. Simply put, the standard says "don't write it this way," and the compiler is free to handle it however it likes—different compilers may produce completely different results. As for why this is UB, we'll explain in detail in the next chapter when we discuss sequence points. For now, just remember: **don't use `++` or `--` on the same variable twice in one expression**.

> ⚠️ **Pitfall Warning**
> Patterns like `i = i++`, `a[i] = i++`, and `printf("%d %d", i++, i++)` are all undefined behavior. If you see these in interview questions, just know they are UB—don't bother guessing "what the answer is," because there is no correct answer.

## Step 3 — Comparing and Evaluating: Relational and Logical Operators

### Relational Operators

Relational operators compare the magnitude relationship between two values, yielding "true" or "false." In C, "true" is represented by the integer `1`, and "false" by the integer `0`.

```c
int a = (5 > 3);    // 1（真）
int b = (5 < 3);    // 0（假）
int c = (5 == 5);   // 1（相等）
int d = (5 != 5);   // 0（不相等）
```

A common typo is writing `=` (assignment) instead of `==` (equality comparison). `if (x = 5)` is always true (because the value of the assignment expression is 5, and any non-zero value is true), and `x` gets accidentally modified. A good compiler will issue a warning for this pattern, so we recommend enabling `-Wall` to let the compiler watch your back.

### Logical Operators

There are three logical operators: `&&` (logical AND), `||` (logical OR), and `!` (logical NOT). They operate on "truth values"—treating operands as booleans, where zero is false and non-zero is true.

```c
if (age >= 18 && age <= 65) {
    // age 在 18 到 65 之间
}
if (score < 0 || score > 100) {
    // score 不在合法范围
}
if (!is_valid) {
    // is_valid 为假时执行
}
```

### Short-Circuit Evaluation — A Very Practical Feature

`&&` and `||` have a crucial feature called **short-circuit evaluation**. For `&&`, if the left operand is false, the right operand is not evaluated at all—because the entire expression is already false, and nothing on the right can change that. `||` is the exact opposite: if the left operand is true, the right side is not evaluated.

This feature is extremely useful in practice. The most classic scenario is checking whether a pointer is null before accessing the value it points to:

```c
// 安全地解引用指针
if (ptr != NULL && ptr->value > 0) {
    // 如果 ptr 是 NULL，ptr->value 不会被访问
    // 避免了空指针解引用导致的崩溃
}
```

If `ptr` is a null pointer, `ptr != NULL` is false. Thanks to short-circuit evaluation, `ptr->value` is never evaluated, and the program stays safe. Without short-circuit evaluation, even if `ptr` were null, the program would still attempt to access `ptr->value`, causing an immediate crash.

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
    int result = (0 && increment());  // 左边为 0（假），右边不会执行
    printf("result = %d, counter = %d\n", result, counter);

    result = (1 || increment());      // 左边为 1（真），右边不会执行
    printf("result = %d, counter = %d\n", result, counter);

    return 0;
}
```

Output:

```text
result = 0, counter = 0
result = 1, counter = 0
```

Great—`increment()` was never called. Short-circuit evaluation worked as expected.

## Step 4 — The Conditional Operator and the Comma Operator

### The Conditional Operator `?:`

The conditional operator is the only ternary operator in C. Its syntax is `condition ? expr1 : expr2`. If `condition` is true, the value of the entire expression is `expr1`; otherwise, it is `expr2`.

You can think of it as a "condensed if-else"—it's especially handy when you need to select a value based on a condition but don't want to write a full if-else statement:

```c
int max = (a > b) ? a : b;                  // 取较大值
const char* label = (count == 1) ? "item" : "items";  // 单复数
```

Conditional operators can be nested, but going beyond two levels starts to hurt readability:

```c
const char* grade = (score >= 90) ? "A" :
                   (score >= 80) ? "B" :
                   (score >= 60) ? "C" : "F";
```

### The Comma Operator

The comma operator `,` has the lowest precedence of all operators in C. It evaluates its two operands from left to right, and the value of the entire expression is the value of the right operand:

```c
int a = (1, 2, 3);  // 先求值 1，再求值 2，最后求值 3，a = 3
```

This operator is rarely used on its own. Its most common use case is maintaining multiple variables simultaneously in a `for` loop:

```c
for (int i = 0, j = n - 1; i < j; i++, j--) {
    int tmp = arr[i];
    arr[i] = arr[j];
    arr[j] = tmp;
}
```

Note that the comma in `int i = 0, j = n - 1` is a declaration separator (not the comma operator), but the comma in `i++, j--` is indeed the comma operator.

## Bridging to C++

C++ does two important things regarding operators. The first is introducing C++ versions of `<stdbool.h>`—`bool`, `true`, and `false` are built-in language keywords in C++, unlike in C where they are macros. The second is operator overloading—you can define behaviors for operators like `+` and `==` on custom types, making them feel as natural to use as built-in types.

But there is an important limitation: although C++ allows overloading `&&` and `||`, **overloading them causes the loss of short-circuit evaluation**. Because overloaded operators are essentially function calls, both arguments are evaluated, and the short-circuit property is lost. Therefore, in practice, never overload `&&` and `||`.

## Summary

At this point, we've walked through the most commonly used operators in C. The key takeaways: integer division directly discards the fractional part rather than rounding; prefix and postfix increment/decrement behave differently inside expressions, but never use them twice on the same variable in a single expression; and the short-circuit evaluation of `&&` and `||` is extremely practical—checking safety conditions before performing actual operations is a common programming pattern.

This raises the next question—we haven't covered bitwise operations yet. If you're going to work in embedded development, bitwise operations are bread and butter: configuring hardware registers, parsing bit fields in communication protocols—you can't do without them. These topics, along with the deeper issues of operator precedence and evaluation order, are what we'll tackle in the next chapter.

## Exercises

### Exercise 1: Integer Division Prediction

Without actually running the code, predict the values of the following expressions, then write a program to verify:

```c
printf("%d\n", 7 / 2);
printf("%d\n", -7 / 2);
printf("%d\n", 7 / -2);
printf("%d\n", 7 % 2);
printf("%d\n", -7 % 2);
```

### Exercise 2: Short-Circuit Evaluation in Practice

Write a function that safely finds the first element in an array greater than a specified value. Use short-circuit evaluation to ensure no out-of-bounds access:

```c
/// @brief 在数组中查找第一个大于 threshold 的元素
/// @param arr 数组
/// @param len 数组长度
/// @param threshold 阈值
/// @return 找到的元素的索引，未找到返回 -1
int find_first_above(const int* arr, size_t len, int threshold);
```

## References

- [cppreference: C operator precedence](https://en.cppreference.com/w/c/language/operator_precedence)
- [cppreference: Arithmetic operators](https://en.cppreference.com/w/c/language/operator_arithmetic)
