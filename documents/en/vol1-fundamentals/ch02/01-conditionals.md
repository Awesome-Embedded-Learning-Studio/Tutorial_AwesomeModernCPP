---
title: Conditional Statements
description: Master if/else, switch, and the ternary operator, and learn to control
  program flow with conditional statements.
chapter: 2
order: 1
difficulty: beginner
reading_time_minutes: 12
platform: host
prerequisites:
- 值类别简介
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
cpp_standard:
- 11
- 14
- 17
- 20
translation:
  source: documents/vol1-fundamentals/ch02/01-conditionals.md
  source_hash: c559a79e36a9c78116c910faf6e5afc5cbcb59eccc61aadd643d768ec8a9a0db
  translated_at: '2026-05-26T10:43:45.518427+00:00'
  engine: anthropic
  token_count: 1946
---
# Conditional Statements

Let's face it, you can't write a program without `if`/`else`. If a program only ever executed in a straight line from top to bottom, it would be no different from a machine that just repeats itself. Real-world programs need to make decisions—"Did the user enter a negative number? Then show an error." "Is the sensor reading above the threshold? Then trigger an alarm." Conditional statements are the mechanism that gives programs this ability to "make decisions."

In this chapter, we will walk through C++ conditional statements from start to finish: `if/else`, `switch`, the ternary operator, and the C++17 `if` with initializer (`if`). They may look simple on the surface, but they hide quite a few easy-to-fall-into traps. Issues like confusing assignment with comparison and `switch` fall-through are high-frequency bug sources in real-world projects.

## if and if-else — The Most Basic Branches

The syntax of the `if` statement is very straightforward: put a conditional expression in parentheses, and if the condition is true (meaning it can be converted to `true`), the following code block is executed.

```cpp
#include <iostream>

int main()
{
    int temperature = 38;

    if (temperature > 37) {
        std::cout << "温度偏高，请注意降温" << std::endl;
    }

    return 0;
}
```

Output:

```text
温度偏高，请注意降温
```

Sometimes, doing nothing when the condition isn't met isn't enough. We need an "otherwise" branch — that's `else`. Going further, if there are third or fourth possibilities, we can chain multiple conditions together using `else if`:

```cpp
int score = 85;

if (score >= 90) {
    std::cout << "等级: A" << std::endl;
} else if (score >= 80) {
    std::cout << "等级: B" << std::endl;
} else if (score >= 70) {
    std::cout << "等级: C" << std::endl;
} else if (score >= 60) {
    std::cout << "等级: D" << std::endl;
} else {
    std::cout << "等级: F" << std::endl;
}
```

Output:

```text
等级: B
```

Here is an easily overlooked detail: `else if` is not an independent keyword in C++. It is actually `else` followed by a new `if` statement. What the compiler sees is a nested binary branch tree. Conditions are checked from top to bottom, and once a condition is true, all subsequent branches are skipped — if you put `score >= 60` before `score >= 90`, a score of 85 would also be classified as a D.

Of course, the condition inside the `if` parentheses must be convertible to `bool`: a non-zero integer is `true`, and a non-null pointer is `true`. This implicit conversion will lead to a classic trap later on.

## Traps We've Fallen Into — Common if Pitfalls

### Assignment vs. Comparison — The Compiler Won't Catch Your Typos

```cpp
int x = 0;
if (x = 5) {
    std::cout << "x is 5" << std::endl;
}
```

You might think this means "if x equals 5," but `=` is the assignment operator, while `==` is the comparison operator. What this code actually does is assign 5 to `x`, and because the result of an assignment expression is the assigned value (5, which is non-zero), the condition is always true. To make matters worse, `x` is accidentally modified to 5.

> **Trap Warning**: `if (x = 5)` compiles without errors, but the logic is almost certainly not what you intended. Make sure to enable the `-Wall -Wextra` compiler flag; GCC and Clang will issue a warning when they encounter this pattern. Some developers prefer putting the constant on the left side `if (5 == x)`, so if you accidentally write `if (5 = x)`, the compiler will throw an error directly because you cannot assign a value to a constant.

### Dangling else and the Brace Habit

In the following code, the indentation makes it look like `else` is paired with the first `if`:

```cpp
if (a > 0)
    if (b > 0)
        result = 1;
else
    result = -1;
```

But the C++ rule is that **`else` always binds to the nearest unpaired `if`**. So this code is actually equivalent to:

```cpp
if (a > 0) {
    if (b > 0) {
        result = 1;
    } else {
        result = -1;
    }
}
```

If our intention was to pair `else` with the outer `if` (setting `result` to -1 when `a <= 0`), then this code is completely wrong. I have to thank my colleague for this — when he saw me write

```cpp
if(a > 1) return -1;
```

he immediately said there's no way this code is passing code review. Now I don't even dare to write code without wrapping it in braces.

> **Trap Warning**: So, even if the branch body is only one line, use braces! Use braces! Use braces! Use braces! Use braces! It's not about typing a few extra characters; it's about preventing ambiguity and bugs introduced during future maintenance — when you add a line of code and forget to add braces, the logic completely changes.

## switch Statements — The Multi-Branch Power Tool

When you need to compare the same expression against multiple discrete values, `switch` is clearer than an `if/else if` chain. Compilers also typically optimize it into a jump table, making the lookup close to O(1).

```cpp
enum class Command {
    kStart,
    kStop,
    kPause,
    kResume
};

void handle_command(Command cmd)
{
    switch (cmd) {
        case Command::kStart:
            std::cout << "启动操作" << std::endl;
            break;
        case Command::kStop:
            std::cout << "停止操作" << std::endl;
            break;
        case Command::kPause:
            std::cout << "暂停操作" << std::endl;
            break;
        case Command::kResume:
            std::cout << "恢复操作" << std::endl;
            break;
        default:
            std::cout << "未知命令" << std::endl;
            break;
    }
}
```

### Fall-Through — Forgetting break Causes a "Leak"

The `break` at the end of each `case` is used to break out of the `switch`. If you forget to write it, execution won't stop after the current case; instead, it "falls through" to the next case and continues executing — this is fall-through. For example, when `cmd` is `Command::kStart` but you forget to write `break`, the output will be:

```text
启动
停止
```

It stops right after starting — that's the bug caused by fall-through.

> **Trap Warning**: Writing a `switch` means you must write a `break`, that's an ironclad rule. Make it a habit: every time you write a `case`, write the `break` first before filling in the logic. If you genuinely want to leverage fall-through (like merging multiple cases into the same handling logic), add a `/* fall through */` comment to clarify your intent; otherwise, people maintaining the code later will assume it's a bug.

### Restrictions on case Labels

The case labels in a `switch` must be **integer constant expressions** — integers whose values can be determined at compile time. You cannot use variables, floating-point numbers, or strings. Additionally, make it a habit to include a `default` branch, even if it just prints a log line. This is especially important when a new member is later added to your enum but you forget to update the `switch` — the `default` is your safety net.

## The Ternary Operator — Concise Conditional Expressions

The syntax of the ternary operator is `condition ? value_if_true : value_if_false`. It is an expression form of `if/else`, suitable for choosing between two values:

```cpp
int a = 10;
int b = 20;
int max_val = (a > b) ? a : b;  // max_val = 20
```

The ternary operator can be embedded directly into expressions, which is particularly useful when initializing `const` variables — `const` can only be initialized, not assigned, so you can't use `if/else` for this:

```cpp
const int kBufferSize = (mode == Mode::kHighSpeed) ? 1024 : 256;
```

However, the ternary operator is not suited for nesting. Something like `a ? b ? c : d : e` may be syntactically valid, but its readability is terrible. If the logic involves more than two levels of choice, just write an `if/else`.

## C++17: if and switch with Initializers

C++17 introduced a very practical feature — you can place an initialization statement in the condition part of `if` and `switch`, separated from the conditional expression by a semicolon:

```cpp
if (int x = compute_value(); x > 0) {
    std::cout << "正数: " << x << std::endl;
} else {
    std::cout << "非正数: " << x << std::endl;
}
// x 在这里已经不可见了
```

Variables declared in the initialization statement are visible throughout the entire `if/else` scope and go out of scope once the statement ends. In the past, you might have had to declare a temporary variable before `if`, and it would stay alive until the function ended — this feature makes scopes more compact, destroying variables as soon as they are no longer needed.

`switch` supports the same syntax:

```cpp
switch (auto cmd = parse_command(input); cmd) {
    case Command::kStart:
        start_operation();
        break;
    case Command::kStop:
        stop_operation();
        break;
    default:
        handle_unknown(cmd);
        break;
}
```

The scope of `cmd` is restricted to the inside of `switch` and doesn't leak outward.

## Hands-On Practice — conditional.cpp

Now let's integrate what we learned in this chapter into a complete program: outputting a grade based on an input score, implemented in different ways.

```cpp
#include <iostream>

/// @brief 用 if-else 链判断成绩等级
/// @param score 百分制分数 (0-100)
/// @return 等级字符
char grade_by_if(int score)
{
    if (score >= 90) {
        return 'A';
    } else if (score >= 80) {
        return 'B';
    } else if (score >= 70) {
        return 'C';
    } else if (score >= 60) {
        return 'D';
    } else {
        return 'F';
    }
}

/// @brief 用 switch 判断成绩等级
/// @param score 百分制分数 (0-100)
/// @return 等级字符
char grade_by_switch(int score)
{
    switch (score / 10) {
        case 10:
        case 9:
            return 'A';
        case 8:
            return 'B';
        case 7:
            return 'C';
        case 6:
            return 'D';
        default:
            return 'F';
    }
}

int main()
{
    int score = 0;
    std::cout << "请输入成绩 (0-100): ";
    std::cin >> score;

    if (score < 0 || score > 100) {
        std::cout << "无效的成绩输入" << std::endl;
        return 1;
    }

    char grade = grade_by_if(score);
    std::cout << "if-else 判定结果: " << grade << std::endl;

    grade = grade_by_switch(score);
    std::cout << "switch 判定结果:  " << grade << std::endl;

    std::cout << "是否及格: "
              << (score >= 60 ? "是" : "否") << std::endl;

    if (int diff = score - 60; diff >= 0) {
        std::cout << "超过及格线 " << diff << " 分" << std::endl;
    } else {
        std::cout << "距离及格还差 " << -diff << " 分" << std::endl;
    }

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra -o conditional conditional.cpp
./conditional
```

Test with input 85:

```text
请输入成绩 (0-100): 85
if-else 判定结果: B
switch 判定结果:  B
是否及格: 是
超过及格线 25 分
```

Test with input 42:

```text
请输入成绩 (0-100): 42
if-else 判定结果: F
switch 判定结果:  F
是否及格: 否
距离及格还差 18 分
```

Great, all three conditional statements produce correct and consistent results. Note that `grade_by_switch` uses `score / 10` to map the score to a range of 0-10, and then uses fall-through to merge 10 and 9. You might occasionally see this trick in real-world projects, but if you find it hard to read, using an `if-else` chain is perfectly fine — readability comes first.

## Run Online

Run the following comprehensive example online to observe the evaluation results of if-else, switch, and the ternary operator:

<OnlineCompilerDemo
  title="Conditional Statements Demo: if-else / switch / Ternary Operator"
  source-path="code/examples/vol1/05_conditionals.cpp"
  description="Run online and observe multiple implementations of grade evaluation. Try modifying the value of kScore and check the results."
  allow-run
/>

## Try It Yourself

Reading without practicing means you haven't really learned it. Here are three exercises with increasing difficulty. We recommend writing each one by hand.

### Exercise 1: Positive, Negative, or Zero

Write a program that reads an integer and determines whether it is positive, negative, or zero. Implement it using both an `if-else` chain and the ternary operator.

Expected interaction:

```text
请输入一个整数: -7
-7 是负数
```

### Exercise 2: Simple Calculator

Use `switch` to implement a simple calculator: read two integers and an operator (`+`, `-`, `*`, `/`) from standard input, and output the result. Handle the case where the divisor is zero for division.

Expected interaction:

```text
请输入表达式（如 3 + 5）: 10 / 0
错误：除数不能为零
```

### Exercise 3: Date Validity Check

Write a function that takes three integers (year, month, and day) and uses conditional statements to determine whether the date is valid. You need to consider whether the month is in the range of 1-12, the different maximum days per month, and that February has 29 days in a leap year. Hint: using a `switch` to handle the number of days in different months will be very clear.

## Summary

Conditional statements are the skeleton of program logic. `if/else` is the most general branching tool, `switch` is suited for multi-way matching against discrete values, the ternary operator is great for simple two-way choices within expressions, and C++17's `if` with initializer (`if`) provides more precise scope control. Always wrap branch bodies in braces, never confuse `=` with `==`, write a `break` for every `case` in a `switch`, and don't nest ternary operators. These seemingly simple but repeatedly encountered traps in real-world projects can be avoided by building good habits from day one, making the road ahead much smoother.

In the next chapter, we will learn about loop statements — teaching programs how to repeat. Loops combined with conditionals form Turing-complete computational power, capable of expressing any computable problem.
