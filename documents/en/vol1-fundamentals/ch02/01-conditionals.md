---
title: Conditional statement
description: Master if/else, switch, and ternary operators, and learn to use conditional
  statements to control program flow.
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
---
# Conditional Statements

Well, you can't write a program without `if`/`else`, right? If a program only ever executed in a straight line from top to bottom, it would be no different from a machine that just repeats things. Real-world programs need to make decisions—"Did the user enter a negative number? Then show an error." "Is the sensor reading above the threshold? Then trigger an alarm." Conditional statements are the mechanism that gives programs this ability to "make decisions."

In this chapter, we will go through C++ conditional statements from start to finish: `if/else`, `switch`, the ternary operator, and the C++17 `if` and `switch` with initializers (`if`). They may look simple on the surface, but they hide quite a few easy-to-fall-into traps. Issues like confusing assignment with comparison and `switch` fall-through are high-frequency bug sources in real-world projects.

## if and if-else—The Most Basic Branching

The syntax of the `if` statement is very straightforward: put a conditional expression inside the parentheses, and if the condition is true (meaning it can be converted to `true`), the code block that follows is executed.

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

Doing nothing when the condition isn't met sometimes isn't enough. We need an "otherwise" branch—that's what `else` is for. Going further, if there are third or fourth scenarios, we can chain multiple conditions together using `else if`:

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

Here is an easily overlooked detail: `else if` is not an independent keyword in C++. It is actually an `else` followed by a new `if` statement. What the compiler sees is a nested binary branching tree. Conditions are checked from top to bottom, and once a condition is true, all subsequent branches are skipped—if you put `score >= 60` before `score >= 90`, a score of 85 would also be classified as a D.

Of course, the condition inside the `if` parentheses must be convertible to `bool`: a non-zero integer is `true`, and a non-null pointer is `true`. This implicit conversion will lead to a classic trap later on.

## Traps We've Fallen Into—Common if Pitfalls

### Assignment vs. Comparison—The Compiler Won't Catch Your Typos

```cpp
int x = 0;
if (x = 5) {
    std::cout << "x is 5" << std::endl;
}
```

You might think this means "if x equals 5," but `=` is the assignment operator, while `==` is the comparison operator. What this code actually does is assign 5 to `x`, and because the result of an assignment expression is the assigned value (5, which is non-zero), the condition is always true. To make matters worse, `x` is accidentally modified to 5.

> **Pitfall Warning**: `if (x = 5)` compiles without errors, but the logic is almost certainly not what you intended. Make sure to enable the `-Wall -Wextra` compiler flag; GCC and Clang will issue a warning when they encounter this pattern. Some programmers prefer putting the constant on the left side, like `if (5 == x)`. That way, if you accidentally write `if (5 = x)`, the compiler will throw an error directly because you cannot assign a value to a constant.

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

If our intention was to pair `else` with the outer `if` (setting `result` to -1 when `a <= 0`), then this code is completely wrong. So I am very grateful to my colleague who, upon seeing me write

```cpp
if(a > 1) return -1;
```

immediately said that if I dared to submit this code, it would never pass code review. Now I hardly dare to write code without wrapping it in braces.

> **Pitfall Warning**: So, even if the branch body is only one line, use braces! Use braces! Use braces! Use braces! Use braces! This isn't about typing a few extra characters; it's about preventing ambiguity and bugs introduced during future maintenance—when you add a line of code and forget to add braces, the logic completely changes.

## switch Statements—The Multi-way Branching Power Tool

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

### Fall-through Behavior—Forgetting break Causes a "Leak"

The `break` at the end of each `case` is used to break out of the `switch`. If you forget to write it, execution won't stop after the current case; instead, it will "fall through" to the next case and continue executing—this is fall-through. For example, when `cmd` is `Command::kStart` but you forgot to write `break`, the output would be:

```text
启动
停止
```

It stops right after starting, and this is the bug caused by fall-through.

> **Pitfall Warning**: Writing a `switch` means you must write a `break`; this is an ironclad rule. Make it a habit: as soon as you write a `case`, write the `break` first before filling in the logic. If you genuinely intend to use the fall-through behavior (like merging multiple cases into the same handling logic), add a `/* fall through */` comment to clarify your intent. Otherwise, people maintaining the code later will assume it's a bug.

### Restrictions on case Labels

The case labels in a `switch` must be **integer constant expressions**—integers whose values can be determined at compile time. You cannot use variables, floating-point numbers, or strings. Additionally, make it a habit to include a `default` branch, even if it just prints a log line. This is especially important when a new member is later added to your enum but you forget to update the `switch`; the `default` acts as your safety net.

## The Ternary Operator—Concise Conditional Expressions

The syntax of the ternary operator is `condition ? value_if_true : value_if_false`. It is an expression form of `if/else`, suitable for choosing between two values:

```cpp
int a = 10;
int b = 20;
int max_val = (a > b) ? a : b;  // max_val = 20
```

The ternary operator can be embedded directly into expressions, which is particularly useful when initializing `const` variables—`const` can only be initialized, not assigned, so you can't use `if/else` to do this:

```cpp
const int kBufferSize = (mode == Mode::kHighSpeed) ? 1024 : 256;
```

However, the ternary operator is not suitable for nesting. Something like `a ? b ? c : d : e` may be syntactically valid, but its readability is extremely poor. If the logic involves more than two levels of choice, just honestly write an `if/else`.

## C++17: if and switch with Initializers

C++17 introduced a very practical feature—you can place an initialization statement in the condition part of `if` and `switch`, separated from the conditional expression by a semicolon:

```cpp
if (int x = compute_value(); x > 0) {
    std::cout << "正数: " << x << std::endl;
} else {
    std::cout << "非正数: " << x << std::endl;
}
// x 在这里已经不可见了
```

Variables declared in the initialization statement are visible throughout the entire `if/else` scope and go out of scope once the statement ends. Previously, you might have needed to declare a temporary variable before the `if`, and then it would stay alive until the end of the function—this feature makes the scope more compact, destroying variables as soon as they are no longer needed.

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

The scope of `cmd` is restricted to the inside of the `switch` and does not leak outward.

## Practical Exercise—conditional.cpp

Now let's integrate what we learned in this chapter into a complete program: output a grade based on an input score, implemented in different ways.

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

Great, all three conditional statements produced correct and consistent results. Note that `grade_by_switch` uses `score / 10` to map the score to a range of 0-10, and then uses the fall-through behavior to merge 10 and 9. You might occasionally see this trick in real-world projects, but if you feel it hurts readability, using an `if-else` chain is perfectly fine—readability comes first.

## Try It Yourself

Reading without practicing is like not learning at all. Here are three exercises with increasing difficulty. We recommend writing each one yourself.

### Exercise 1: Positive, Negative, or Zero

Write a program that reads an integer and determines whether it is positive, negative, or zero. You must implement this using both an `if-else` chain and the ternary operator.

Expected interaction:

```text
请输入一个整数: -7
-7 是负数
```

### Exercise 2: Simple Calculator

Use a `switch` to implement a simple calculator: read two integers and an operator (`+`, `-`, `*`, `/`) from standard input, and output the result. The division operation must handle the case where the divisor is zero.

Expected interaction:

```text
请输入表达式（如 3 + 5）: 10 / 0
错误：除数不能为零
```

### Exercise 3: Date Validity Check

Write a function that takes three integers—year, month, and day—and uses conditional statements to determine whether the date is valid. You need to consider whether the month is in the range of 1-12, the different maximum days per month, and that February has 29 days in a leap year. Hint: using a `switch` to handle the number of days in different months will be very clear.

## Summary

Conditional statements are the skeleton of program logic. `if/else` is the most general branching tool, `switch` is suited for multi-way matching against discrete values, the ternary operator is great for simple two-way choices within expressions, and C++17's `if` with initializers provides more precise scope control. Always wrap branch bodies in braces, never confuse `=` with `==`, write a `break` for every `case` in a `switch`, and do not nest ternary operators. These seemingly simple traps appear repeatedly in real-world projects. If you develop good habits from day one, the road ahead will be much smoother.

In the next chapter, we will learn about loop statements—teaching programs how to repeat. Loops combined with conditionals form Turing-complete computational power; any computable problem can be expressed using them.
