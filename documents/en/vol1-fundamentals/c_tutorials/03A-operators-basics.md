---
chapter: 1
cpp_standard:
- 11
description: Master C arithmetic operators, increment and decrement, relational and
  logical operators, the conditional operator, and the comma operator, and understand
  short-circuit evaluation and the use of assignment operators.
difficulty: beginner
order: 4
platform: host
prerequisites:
- 浮点、字符、const 与类型转换
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
  source_hash: 76114a11ede805c609f8b153ceb4bc7fb9f640499cc513d845ec21c50bf072bd
  translated_at: '2026-06-16T03:33:08.086193+00:00'
  engine: anthropic
  token_count: 1503
---
# Operator Basics: Making Data Move

In the previous post, we dissected C data types from the inside out—how integers are stored, how floating-point numbers work, and how characters are handled. But just having data isn't enough; we need to make it "move": performing addition, subtraction, multiplication, division, comparisons, and boolean logic. These operations in C are handled by **operators**.

You can think of operators as the "verbs" of C—variables and constants are nouns, operators connect them to form expressions, expressions combine into statements, and statements build programs. We only use a handful of operators in daily programming, but each has its own quirks. In this post, we will go through the most common arithmetic, relational, and logical operators, focusing on the pitfalls that are easy to stumble into. We'll leave bitwise operations and deeper evaluation order issues for the next post.

> **Learning Objectives**
> After completing this chapter, you will be able to:
>
> - [ ] Skillfully use the five arithmetic operators and increment/decrement operators.
> - [ ] Understand the "round towards zero" rule for integer division.
> - [ ] Master the short-circuit evaluation characteristics of relational and logical operators.
> - [ ] Correctly use the conditional operator and the comma operator.

## Environment Setup

We will conduct all subsequent experiments in the following environment:

- Platform: Linux x86\_64 (WSL2 is also acceptable)
- Compiler: GCC 13+ or Clang 17+
- Compiler flags: `-std=c17 -Wall -Wextra -pedantic`

## Step 1 — Add, Subtract, Multiply, Divide: Arithmetic Operators

### The Five Basic Operators

C provides five basic arithmetic operators: `+` (addition), `-` (subtraction), `*` (multiplication), `/` (division), and `%` (modulo). The first four apply to all numeric types, while the modulo operator `%` applies only to integers.

```c
#include <stdio.h>

int main(void) {
    int a = 10, b = 3;

    printf("%d + %d = %d\n", a, b, a + b); // 13
    printf("%d - %d = %d\n", a, b, a - b); // 7
    printf("%d * %d = %d\n", a, b, a * b); // 30
    printf("%d / %d = %d\n", a, b, a / b); // 3 (Integer division)
    printf("%d %% %d = %d\n", a, b, a % b); // 1 (Remainder)

    return 0;
}
```

Here is a pitfall that beginners often step into: **Dividing two integers results in an integer**. `7 / 2` is not `3.5`, but `3`. The decimal part is discarded directly; it is not rounded.

> ⚠️ **Pitfall Warning**
> If you want a division result with decimals, at least one operand must be a floating-point number. `7 / 2` yields `3`, but `7.0 / 2` or `7 / 2.0` yields `3.5`.

### Negative Number Division: Round Towards Zero

The C99 standard explicitly states: integer division rounds towards zero. This means that after the fractional part is discarded, the result approaches zero. `-7 / 2` is `-3`, and `7 / -2` is `-3` (not `-4`). The sign of the remainder in a modulo operation matches the dividend: `-7 % 2` is `-1`.

```c
#include <stdio.h>

int main(void) {
    printf("Integer division rounds towards zero:\n");
    printf("  7 / 2  = %d\n", 7 / 2);   // 3
    printf(" -7 / 2  = %d\n", -7 / 2);  // -3 (not -4)
    printf("  7 / -2 = %d\n", 7 / -2);  // -3

    printf("\nModulo sign matches dividend:\n");
    printf(" -7 %% 2 = %d\n", -7 % 2);  // -1
    printf("  7 %% -2 = %d\n", 7 % -2); // 1

    return 0;
}
```

Let's verify this:

```bash
gcc -std=c17 -Wall -Wextra -pedantic main.c -o main
./main
```

Execution result:

```text
Integer division rounds towards zero:
  7 / 2  = 3
 -7 / 2  = -3
  7 / -2 = -3

Modulo sign matches dividend:
 -7 % 2 = -1
  7 % -2 = 1
```

## Step 2 — Increment and Decrement: Two Special Operators

### Difference Between Prefix and Postfix

`++` (increment) and `--` (decrement) are special operators in C—they can be placed before a variable (prefix) or after a variable (postfix). When used alone, they have the same effect, but their behavior differs when mixed within expressions.

Here is an analogy to understand this: prefix `++` is like "raise the price, then check out"—add 1 to the value first, then return the new value. Postfix `++` is like "check out, then raise the price"—return the current value first, then add 1.

```c
#include <stdio.h>

int main(void) {
    int i = 10;

    // Prefix: Increment first, then use the value
    printf("Prefix ++i: %d\n", ++i); // i becomes 11, prints 11

    // Postfix: Use the value first, then increment
    printf("Postfix i++: %d\n", i++); // prints 11, then i becomes 12
    printf("After i++: %d\n", i);     // prints 12

    return 0;
}
```

Execution result:

```text
Prefix ++i: 11
Postfix i++: 11
After i++: 12
```

### Never Write It Like This

Here is something very important to keep in mind—**never use `++` or `--` on the same variable multiple times within the same expression**:

```c
int i = 5;
int a = i++ + ++i; // Undefined Behavior!
```

This kind of writing is **Undefined Behavior (UB)** in the C standard. Simply put, the standard says "don't do this," and compilers can handle it in any way—different compilers might give completely different results. As for why this is UB, we will explain it in detail in the next post when we discuss sequence points. For now, just remember: **do not use `++` or `--` on the same variable twice in one expression**.

> ⚠️ **Pitfall Warning**
> `a = i++ + i++`, `a = ++i + ++i`, `a = i++ + ++i`—all of these are undefined behavior. If you see this in an interview question, just know it's UB; don't try to guess "what the answer is"—because there is no correct answer.

## Step 3 — Comparison and Judgment: Relational and Logical Operators

### Relational Operators

Relational operators are used to compare the magnitude relationship between two values, resulting in "true" or "false". In C, "true" is represented by the integer `1`, and "false" by the integer `0`.

```c
#include <stdio.h>

int main(void) {
    int a = 5, b = 10;

    printf("%d < %d is %d\n", a, b, a < b);  // 1 (true)
    printf("%d > %d is %d\n", a, b, a > b);  // 0 (false)
    printf("%d == %d is %d\n", a, a, a == a); // 1 (true)
    printf("%d != %d is %d\n", a, b, a != b); // 1 (true)

    return 0;
}
```

A common typo is writing `==` (equality comparison) as `=` (assignment). `if (a = 5)` is always true (because the value of the assignment expression is 5, and non-zero is true), and `a` is accidentally modified. Good compilers will warn about this, so it is recommended to enable `-Wextra` to let the compiler watch out for you.

### Logical Operators

There are three logical operators: `&&` (logical AND), `||` (logical OR), and `!` (logical NOT). They operate on "truth values"—treating operands as boolean values, where zero is false and non-zero is true.

```c
#include <stdio.h>

int main(void) {
    int a = 5, b = 0;

    printf("%d && %d = %d\n", a, b, a && b); // 0 (false)
    printf("%d || %d = %d\n", a, b, a || b); // 1 (true)
    printf("!%d = %d\n", a, !a);             // 0 (false)

    return 0;
}
```

### Short-Circuit Evaluation — A Very Practical Feature

`&&` and `||` have a very important feature called **short-circuit evaluation**. For `&&`, if the left operand is false, the right operand is not evaluated at all—because the entire expression is already false, and the right side doesn't affect the result. `||` is the opposite: if the left operand is true, the right side is not evaluated.

This feature is incredibly useful in actual programming. The most classic scenario is checking if a pointer is null before accessing the content it points to:

```c
#include <stdio.h>
#include <stdbool.h>

struct Node {
    int value;
    // ... other fields
};

bool is_positive(const struct Node* node) {
    // If node is NULL, the right side (node->value) is not evaluated
    return (node != NULL) && (node->value > 0);
}

int main(void) {
    struct Node n = {5};
    printf("is_positive(&n) = %d\n", is_positive(&n)); // 1

    printf("is_positive(NULL) = %d\n", is_positive(NULL)); // 0

    return 0;
}
```

If `node` is a null pointer, `node != NULL` is false. Due to short-circuit evaluation, `node->value` is not evaluated, and the program is safe. Without short-circuit evaluation, even if `node` is null, it would attempt to access `node->value`, causing an immediate crash.

Let's verify the effect of short-circuit evaluation:

```c
#include <stdio.h>

int dangerous_call(void) {
    printf("Dangerous function called!\n");
    return 1;
}

int main(void) {
    int a = 0;

    // Because a is 0 (false), dangerous_call() is never executed
    if (a && dangerous_call()) {
        // This block won't run
    }

    printf("Program finished safely.\n");
    return 0;
}
```

Execution result:

```text
Program finished safely.
```

Great, `dangerous_call` was never called—short-circuit evaluation took effect.

## Step 4 — Conditional Operator and Comma Operator

### Conditional Operator `? :`

The conditional operator is the only ternary operator in C, with the syntax `condition ? expr1 : expr2`. If `condition` is true, the value of the entire expression is `expr1`; otherwise, it is `expr2`.

You can think of it as a "condensed if-else"—it is particularly convenient when you need to select a value based on a condition but don't want to write a full if-else statement:

```c
#include <stdio.h>

int main(void) {
    int age = 17;

    // Determine if an adult
    const char* status = (age >= 18) ? "Adult" : "Minor";
    printf("Status: %s\n", status);

    // Calculate absolute value
    int x = -10;
    int abs_x = (x < 0) ? -x : x;
    printf("Absolute value of %d is %d\n", x, abs_x);

    return 0;
}
```

Conditional operators can be nested, but readability starts to suffer after more than two levels:

```c
// Not recommended: deeply nested ternary
int score = 85;
const char* grade = (score >= 90) ? "A" :
                    (score >= 80) ? "B" :
                    (score >= 70) ? "C" : "F";
```

### Comma Operator

The comma operator `,` is the lowest precedence operator in C. It evaluates two operands from left to right, and the value of the entire expression is the value of the right operand:

```c
#include <stdio.h>

int main(void) {
    int a = 10, b = 20;

    // The comma operator causes a to be incremented first,
    // then the expression takes the value of b
    int c = (a++, b);

    printf("a: %d, b: %d, c: %d\n", a, b, c); // a=11, b=20, c=20

    return 0;
}
```

This operator is rarely used alone. The most common usage is to maintain multiple variables simultaneously in a `for` loop:

```c
#include <stdio.h>

int main(void) {
    // Using the comma operator in a for loop
    for (int i = 0, j = 10; i < 10; i++, j--) {
        printf("i: %d, j: %d\n", i, j);
    }

    return 0;
}
```

Note that the comma in `int i = 0, j = 10` is a declaration separator (not the comma operator), but the comma in `i++, j--` is indeed the comma operator.

## C++ Transition

C++ does two important things regarding operators. First, it introduces C++ versions of `<stdbool.h>`—`true`, `false`, and `bool` are built-in keywords in C++, unlike macros in C. Second is operator overloading—you can define behaviors for operators like `+`, `==`, etc., for custom types, making custom types feel as natural to use as built-in types.

However, there is an important limitation: although C++ allows overloading `&&` and `||`, **overloading them loses the short-circuit evaluation property**. Because overloaded operators are essentially function calls, both parameters will be evaluated, and the short-circuit characteristic is gone. Therefore, in practice, never overload `&&` and `||`.

## Summary

At this point, we have gone through the most commonly used operators in C. Key takeaways: integer division directly discards the decimal part, it does not round; prefix and postfix increment/decrement behave differently in expressions, but do not use them twice on the same variable in one expression; short-circuit evaluation of `&&` and `||` is very practical, and checking safety conditions before performing actual operations is a common programming pattern.

The next question is—we haven't covered bitwise operations yet. If you plan to touch embedded development later, bitwise operations are part of the daily routine: configuring hardware registers, parsing bit fields in communication protocols—it's all indispensable. These topics, combined with deeper operator precedence and evaluation order, are the bones we will pick in the next post.

## Exercises

### Exercise 1: Integer Division Prediction

Without actually running it, predict the value of the following expressions, then write a program to verify:

```c
int a = 7, b = -4;
// Predict the values of:
// 1. a / b
// 2. a % b
// 3. -a / b
// 4. b / a
```

### Exercise 2: Short-Circuit Evaluation in Action

Write a function that safely finds the first element in an array greater than a specified value. Use short-circuit evaluation to ensure no out-of-bounds access occurs:

```c
#include <stdio.h>
#include <stdbool.h>

// Returns true if found, and stores the index in *out_index
bool find_first_greater_than(const int* arr, size_t size, int threshold, size_t* out_index) {
    // TODO: Use short-circuit evaluation to check bounds first
    // if (arr != NULL && size > 0 && ...) { ... }
    return false;
}
```

## References

- [cppreference: C Operator Precedence](https://en.cppreference.com/w/c/language/operator_precedence)
- [cppreference: Arithmetic Operators](https://en.cppreference.com/w/c/language/operator_arithmetic)
