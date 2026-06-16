---
chapter: 2
cpp_standard:
- 11
- 14
- 17
- 20
description: Master `if`/`else`, `switch`, and ternary operators, and learn to control
  program flow with conditional statements.
difficulty: beginner
order: 1
platform: host
prerequisites:
- 值类别简介
reading_time_minutes: 10
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: Conditional Statements
translation:
  source: documents/vol1-fundamentals/ch02/01-conditionals.md
  source_hash: c665d23d83b71176a5d5e5b9f543efff4d59f2af9a8e0fe65ec8f403bb8b2397
  translated_at: '2026-06-16T03:41:27.016990+00:00'
  engine: anthropic
  token_count: 1942
---
# Conditional Statements

Let's be honest, you can't write programs without `if`/`else`, right? If a program just executes a single straight line from start to finish, it's no different from a machine that just repeats itself. Real-world programs need to make decisions—"Did the user enter a negative number? Show an error." "Is the sensor reading above the threshold? Trigger an alarm." Conditional statements are the mechanism that gives programs the ability to "make decisions."

In this chapter, we will go through C++ conditional statements from top to bottom: `if`, `if-else`, the ternary operator, and the `if`/`switch` with initializers introduced in C++17. They may look simple on the surface, but they hide quite a few pitfalls, especially confusing assignment with comparison and `switch` fall-through issues. These are high-frequency sources of bugs in actual projects.

## `if` and `if-else` — The Most Basic Branching

The syntax of the `if` statement is very straightforward: put a conditional expression in parentheses. If the condition is true (i.e., can be converted to `true`), execute the following code block.

```cpp
#include <iostream>

int main() {
    int score = 85;

    if (score >= 60) {
        std::cout << "Passed!" << std::endl;
    }

    return 0;
}
```

Output:

```text
Passed!
```

Sometimes doing nothing when the condition isn't met isn't enough. We need an "otherwise" branch — this is `else`. Furthermore, if there are third or fourth scenarios, we can chain multiple conditions with `else if`:

```cpp
#include <iostream>

int main() {
    int score = 85;

    if (score >= 90) {
        std::cout << "Grade: A" << std::endl;
    } else if (score >= 80) {
        std::cout << "Grade: B" << std::endl;
    } else if (score >= 70) {
        std::cout << "Grade: C" << std::endl;
    } else if (score >= 60) {
        std::cout << "Grade: D" << std::endl;
    } else {
        std::cout << "Grade: F" << std::endl;
    }

    return 0;
}
```

Output:

```text
Grade: B
```

Here is a detail that is easily overlooked: `else if` is not an independent keyword in C++. It is actually an `else` followed by a new `if` statement. The compiler sees a nested binary branch tree. Conditions are checked from top to bottom. Once a condition is true, all subsequent branches are skipped — if you put `score >= 60` before `score >= 80`, a score of 85 would be classified as Grade D.

Of course, the condition inside `if` parentheses must be convertible to `bool`: a non-zero integer is `true`, a non-null pointer is `true`. This implicit conversion leads to a classic pitfall later on.

## The Pits We've Fallen Into — Common `if` Traps

### Assignment vs Comparison — The Compiler Won't Stop Your Typos

```cpp
if (x = 5) {
    // ...
}
```

You might think this means "if x equals 5", but `=` is the assignment operator, `==` is the comparison operator. What this code actually does is: assign 5 to `x`, and because the result of an assignment expression is the assigned value (5, which is non-zero), the condition is always true. Even worse, `x` is accidentally modified to 5.

> **Pitfall Warning**: `if (x = 5)` compiles without error, but the logic is almost certainly not what you want. Always enable the `-Wparentheses` compiler option; GCC and Clang will warn you about this style. Some programmers prefer putting the constant on the left (`if (5 == x)`), so if you accidentally write `if (5 = x)`, the compiler will error directly because you cannot assign to a constant.

### Dangling Else and Brace Habits

In the code below, the indentation makes it look like `else` pairs with the first `if`:

```cpp
if (score > 90)
    if (score > 95)
        std::cout << "Excellent!" << std::endl;
else
    std::cout << "Keep trying" << std::endl;
```

But C++ rules state that **`else` always binds to the nearest, unpaired `if`**. So this code is actually equivalent to:

```cpp
if (score > 90) {
    if (score > 95) {
        std::cout << "Excellent!" << std::endl;
    } else {
        std::cout << "Keep trying" << std::endl;
    }
}
```

If our intention was to pair `else` with the outer `if` (setting `y` to -1 when `x < 0`), this code is completely wrong. So I have to thank my colleague; when he saw me write

```cpp
if (x > 0)
    if (y > 0)
        doSomething();
else
    doAnotherThing();
```

he said without hesitation: "If you dare commit this code, you won't pass the Code Review." Now I don't dare write code without wrapping it in braces.

> **Pitfall Warning**: So, even if the branch body has only one line, use braces! Use braces! Use braces! Use braces! This isn't about typing a few extra characters, but preventing ambiguity and bugs during future maintenance — when you add a line of code and forget to add braces, the logic changes completely.

## `switch` Statement — The Tool for Multi-way Branching

When you need to compare the same expression against multiple discrete values, `switch` is clearer than an `if-else` chain. Compilers can often optimize it into a jump table, making lookup nearly O(1).

```cpp
#include <iostream>

int main() {
    int option = 2;

    switch (option) {
        case 1:
            std::cout << "Option 1 selected" << std::endl;
            break;
        case 2:
            std::cout << "Option 2 selected" << std::endl;
            break;
        case 3:
            std::cout << "Option 3 selected" << std::endl;
            break;
        default:
            std::cout << "Invalid option" << std::endl;
            break;
    }

    return 0;
}
```

### Fall-Through — Forgetting `break` Causes "Leaks"

The `break` at the end of each `case` is used to jump out of the `switch`. If you forget to write it, execution won't stop after the current `case`; instead, it will "fall through" to the next `case` — this is called fall-through. For example, when `option` is `2` but you forget to write `break`, the output would be:

```text
Option 2 selected
Option 3 selected
Invalid option
```

It stopped as soon as it started, which is the bug caused by fall-through.

> **Pitfall Warning**: When writing `switch`, you must write `break`. This is an iron rule. Make it a habit: write `break` immediately after writing a `case` label, then fill in the logic. If you intentionally want to use fall-through (e.g., merging multiple `case`s to the same logic), add a `[[fallthrough]]` comment to indicate your intent, otherwise maintainers will think it's a bug.

### Restrictions on Case Labels

`switch` case labels must be **integer constant expressions** — integers whose values are known at compile time. You cannot use variables, floating-point numbers, or strings. Also, develop the habit of writing a `default` branch, even if it just logs a line. This is especially true when your enumeration gains new members later but you forget to update the `switch` — `default` is your safety net.

## Ternary Operator — Concise Conditional Expression

The syntax of the ternary operator is `condition ? expr1 : expr2`. It is an expression form of `if-else`, suitable for choosing between two values:

```cpp
int max = (a > b) ? a : b;
```

The ternary operator can be embedded directly into expressions, which is particularly useful when initializing `const` variables — `const` can only be initialized, not assigned, so you can't do this with `if`:

```cpp
const int limit = (is_admin) ? 1000 : 100;
```

However, the ternary operator is not suitable for nesting. Something like `condition1 ? a : condition2 ? b : c` is syntactically legal but has terrible readability. If the logic involves more than two layers of selection, honestly write `if-else`.

## C++17: `if` and `switch` with Initializers

C++17 introduced a very practical feature — you can place an initialization statement in the condition part of `if` and `switch`, separated by a semicolon from the conditional expression:

```cpp
if (auto result = initializeResource(); result.isValid()) {
    // Use result
}
```

Variables declared in the initialization statement are visible throughout the entire `if` statement (including any `else if` and `else` blocks) and go out of scope when the statement ends. Previously, you might have needed to declare a temporary variable before `if`, and it would stay alive until the end of the function — this feature makes scopes tighter, destroying variables immediately after use.

`switch` supports the same syntax:

```cpp
switch (int ch = getchar(); ch) {
    case 'a':
        // ...
}
```

The scope of `ch` is restricted inside the `switch` and won't leak outside.

## Real-World Practice — conditional.cpp

Now let's integrate what we learned in this chapter into a complete program: output a grade based on an input score, implemented in different ways.

```cpp
#include <iostream>

int main() {
    int score;
    std::cout << "Enter score (0-100): ";
    std::cin >> score;

    // Method 1: if-else chain
    if (score >= 90) {
        std::cout << "[if-else] Grade: A" << std::endl;
    } else if (score >= 80) {
        std::cout << "[if-else] Grade: B" << std::endl;
    } else if (score >= 70) {
        std::cout << "[if-else] Grade: C" << std::endl;
    } else if (score >= 60) {
        std::cout << "[if-else] Grade: D" << std::endl;
    } else {
        std::cout << "[if-else] Grade: F" << std::endl;
    }

    // Method 2: switch (using integer division)
    // Map 0-100 to 0-10
    switch (score / 10) {
        case 10:
        case 9:
            std::cout << "[switch] Grade: A" << std::endl;
            break;
        case 8:
            std::cout << "[switch] Grade: B" << std::endl;
            break;
        case 7:
            std::cout << "[switch] Grade: C" << std::endl;
            break;
        case 6:
            std::cout << "[switch] Grade: D" << std::endl;
            break;
        default:
            std::cout << "[switch] Grade: F" << std::endl;
            break;
    }

    // Method 3: Ternary operator (simplified logic)
    std::cout << "[ternary] Result: "
              << (score >= 60 ? "Passed" : "Failed")
              << std::endl;

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 conditional.cpp -o conditional
./conditional
```

Test input 85:

```text
Enter score (0-100): 85
[if-else] Grade: B
[switch] Grade: B
[ternary] Result: Passed
```

Test input 42:

```text
Enter score (0-100): 42
[if-else] Grade: F
[switch] Grade: F
[ternary] Result: Failed
```

Great, all three conditional statements produced correct and consistent results. Note that `switch` uses integer division (`score / 10`) to map the score to 0-10, then uses fall-through to merge 10 and 9. You might see this trick in actual projects occasionally, but if you find it hard to read, using an `if-else` chain is fine; readability comes first.

## Run Online

Run the comprehensive example below online to observe the results of `if-else`, `switch`, and the ternary operator:

<OnlineCompilerDemo
  title="Conditional Statements Demo: if-else / switch / Ternary"
  source-path="code/examples/vol1/05_conditionals.cpp"
  description="Run online and observe multiple implementations of grade determination. Try modifying kScore to see different results."
  allow-run
/>

## Try It Yourself

Reading without practicing is like not learning at all. Here are three exercises with increasing difficulty. I suggest you write each one by hand.

### Exercise 1: Positive, Negative, or Zero

Write a program that reads an integer and determines if it is positive, negative, or zero. Implement it using both an `if-else` chain and the ternary operator.

Expected interaction:

```text
Enter a number: -5
Negative
```

### Exercise 2: Simple Calculator

Use `switch` to implement a simple calculator: read two integers and an operator (`+`, `-`, `*`, `/`) from standard input, and output the result. Handle division by zero.

Expected interaction:

```text
Enter first number: 10
Enter operator: /
Enter second number: 2
Result: 5
```

### Exercise 3: Date Validity Check

Write a function that takes three integers (year, month, day) and uses conditional statements to determine if the date is valid. You need to consider if the month is within 1-12, the different maximum days for each month, and leap years (February has 29 days). Hint: using `switch` to handle days for different months will be very clear.

## Summary

Conditional statements are the skeleton of program logic. `if` is the most general branching tool, `switch` is suitable for multi-way matching against discrete values, the ternary operator fits simple binary choices within expressions, and C++17's initializer `if` makes scope control more precise. Always wrap branch bodies in braces, never confuse `=` and `==`, write `break` for every `case` in `switch`, and don't nest ternary operators. If you develop good habits from day one regarding these seemingly simple but frequently occurring pitfalls, the road ahead will be much smoother.

In the next chapter, we will learn about loop statements — teaching programs to repeat. Loops combined with conditionals constitute Turing-complete computational power; any computable problem can be expressed with them.
