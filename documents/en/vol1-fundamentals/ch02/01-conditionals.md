---
chapter: 2
cpp_standard:
- 11
- 14
- 17
- 20
description: Master if/else, switch, and the ternary operator, and learn to
  steer your program's flow with conditional statements.
difficulty: beginner
order: 1
platform: host
prerequisites:
- Introduction to Value Categories
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
  source_hash: 10642e23321d7bb03002b4260a0cb7104b0f32219593162035cde2282daa2aae
  translated_at: '2026-09-25T10:12:37+00:00'
  engine: anthropic
  token_count: 9800
---
# Conditional Statements: Teaching Your Program to Read the Room

Well, you can't write programs without if/else, right? If a program only ever runs one straight line from start to finish, it's no different from a machine that can do nothing but parrot the same thing. Real-world programs need to make judgments—"the user typed a negative number? Then show an error." "the sensor reading crossed the threshold? Then trigger the alarm." Conditional statements are the mechanism that gives a program this ability to "make decisions."

## `if` and `if-else` — The Most Basic Branching

The syntax of an `if` statement is dead simple: put a conditional expression in the parentheses, and if the condition holds (that is, it converts to `true`), the code block after it runs.

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

Sometimes "do nothing when the condition fails" isn't good enough. We need an "otherwise" branch—that's `else`. And if there's a third or fourth case beyond that, `else if` chains several conditions together:

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

Here's a detail that's easy to miss: `else if` is not a standalone C++ keyword. It's really an `else` followed by a brand-new `if` statement. What the compiler sees is a nested tree of binary branches. Conditions are checked from top to bottom, and once one of them is true, every branch below it gets skipped—if you put `score >= 60` in front of `score >= 90`, a score of 85 would land in grade D.

Of course, the condition inside the `if` parentheses must be convertible to `bool`: a non-zero integer is `true`, and a non-null pointer is `true`. This implicit conversion sets up a classic trap, coming right up.

## The Traps We've Stepped In Over the Years — Common `if` Pitfalls

### Assignment vs Comparison — The Compiler Won't Stop Your Typos

```cpp
int x = 0;
if (x = 5) {
    std::cout << "x is 5" << std::endl;
}
```

You might think this means "if x equals 5", but `=` is the assignment operator; `==` is the comparison operator. What this code actually does is assign 5 to `x`, and since the result of an assignment expression is the value that was assigned (5, non-zero), the condition is always true. Worse yet, `x` gets quietly changed to 5 along the way.

`if (x = 5)` compiles without a peep, but the logic is almost certainly not what you wanted. Always enable the `-Wall -Wextra` compiler options—GCC and Clang will warn when they see this pattern. Some programmers make a habit of putting the constant on the left, `if (5 == x)`, so that a slip of the finger producing `if (5 = x)` fails to compile outright, because you can't assign to a constant.

### Dangling `else` and the Braces Habit

In the code below, the indentation makes it look like the `else` pairs with the first `if`:

```cpp
if (a > 0)
    if (b > 0)
        result = 1;
else
    result = -1;
```

But C++'s rule is that **`else` always binds to the nearest `if` that doesn't already have one**. So this code is actually equivalent to:

```cpp
if (a > 0) {
    if (b > 0) {
        result = 1;
    } else {
        result = -1;
    }
}
```

If our intention was for the `else` to pair with the outer `if` (setting `result` to -1 when `a <= 0`), then this code is flat-out wrong. Which is why I'm deeply grateful to my colleague: the moment he saw me write

```cpp
if(a > 1) return -1;
```

he said without blinking: if you dare hand in code like this, don't expect it to get through code review. To this day I barely dare write code that isn't wrapped in braces.

So, even if the branch body is only one line, add the braces! Add the braces! Add the braces! Add the braces! Add the braces! This isn't about typing a few extra characters—it's about preventing ambiguity and future-maintenance bugs: the day you add one more line and forget to add the braces, the logic changes completely underneath you.

## The `switch` Statement — A Sharp Tool for Multi-way Branching

When you need to compare the same expression against multiple discrete values, `switch` is clearer than an `if/else if` chain. Compilers usually optimize it into a jump table too, so the lookup is close to O(1).

```cpp
// Folks, just get familiar with the look for now: you can treat enum class as plain enum for the time being, but it is clearly better than enum — feel free to ask an AI why
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

### Fall-Through — Forget `break` and It "Leaks"

The `break` at the end of each `case` jumps out of the `switch`. Forget to write it, and once the current case finishes, execution doesn't stop—it "falls through" into the next case and keeps going. That's fall-through. For instance, when `cmd` is `Command::kStart` and you forgot the `break`, the output would be:

```text
启动
停止
```

It stopped the moment it started—that's the kind of bug fall-through brings.

**When you write a `switch`, you must write the `break`. Make it a habit!**

> A veteran might say: ooh, how scary—but in plenty of common cases I *want* fall-through (that is, not breaking is exactly my intent).
> Then remember to tag it with a [[fall-through]] attribute, or add a `/* fall through */` comment if it's not supported. That's my whole point!

### Restrictions on `case` Labels

A `switch`'s case labels must be **integer constant expressions**—integers whose values are known at compile time. Variables, floating-point numbers, and strings don't qualify. Also, build the habit of writing a `default` branch, even if all it does is log one line. Especially when your enum later gains a new member and you forget to update the `switch`, `default` is your safety net.

## The Ternary Operator — A Concise Conditional Expression

The ternary operator's syntax is `condition ? value_if_true : value_if_false`. It is the expression form of `if/else`, well suited to choosing between two values:

```cpp
int a = 10;
int b = 20;
int max_val = (a > b) ? a : b;  // max_val = 20
```

Because it slots directly into an expression, the ternary operator is especially useful when initializing `const` variables—`const` can only be initialized, never assigned, so `if/else` can't do the job:

```cpp
const int kBufferSize = (mode == Mode::kHighSpeed) ? 1024 : 256;
```

But the ternary operator does not nest well. Something like `a ? b ? c : d : e` is syntactically legal yet nearly unreadable. Once your logic involves more than two levels of choice, honestly write `if/else`.

## Hands-On Practice — conditional.cpp

Now let's fold everything from this chapter into one complete program: read an exam score, print the grade, and implement it in several different ways.

```cpp
#include <iostream>

/// @brief Determine the letter grade with an if-else chain
/// @param score Score on a 0-100 scale
/// @return The grade letter
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

/// @brief Determine the letter grade with a switch
/// @param score Score on a 0-100 scale
/// @return The grade letter
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

Test input 85:

```text
请输入成绩 (0-100): 85
if-else 判定结果: B
switch 判定结果:  B
是否及格: 是
超过及格线 25 分
```

Test input 42:

```text
请输入成绩 (0-100): 42
if-else 判定结果: F
switch 判定结果:  F
是否及格: 否
距离及格还差 18 分
```

Nice—all three conditional constructs produced correct, consistent results. Note that `grade_by_switch` uses `score / 10` to map the score into 0-10, then leans on fall-through to merge 10 and 9. You will run into this trick in real projects now and then, but if you find it hard to read, an `if-else` chain is perfectly fine—readability comes first.

## Run It Online

Run the comprehensive example below online and watch how `if-else`, `switch`, and the ternary operator each make their call:

<OnlineCompilerDemo
  title="Conditional Statements Demo: if-else / switch / Ternary"
  source-path="code/examples/vol1/05_conditionals.cpp"
  description="Run it online and compare several implementations of grade determination. Try changing the value of kScore and see how the results change."
  allow-run
/>

## Try It Yourself

Reading without practicing amounts to not learning. Here are three exercises in rising order of difficulty; I suggest writing every one of them yourself.

### Exercise 1: Positive, Negative, or Zero

Write a program that reads an integer and decides whether it is positive, negative, or zero. Implement it two ways: once with an `if-else` chain and once with the ternary operator. Picking one of three outcomes with the ternary forces you to nest—once you've written both, compare them: which one would you be willing to reread half a year from now?

Expected interaction:

```text
请输入一个整数: -7
-7 是负数
```

::: details Reference answer

**main.cpp** (if-else chain version)

```cpp
#include <iostream>

int main()
{
    int value = 0;
    std::cout << "请输入一个整数: ";
    std::cin >> value;

    if (value > 0) {
        std::cout << value << " 是正数" << std::endl;
    } else if (value < 0) {
        std::cout << value << " 是负数" << std::endl;
    } else {
        std::cout << value << " 是零" << std::endl;
    }

    return 0;
}
```

**main.cpp** (ternary operator version)

```cpp
#include <iostream>

int main()
{
    int value = 0;
    std::cout << "请输入一个整数: ";
    std::cin >> value;

    std::cout << value
              << (value > 0 ? " 是正数"
                            : value < 0 ? " 是负数"
                                        : " 是零")
              << std::endl;

    return 0;
}
```

This version is exactly the "does not nest well" style the main text warned about—the exercise demands the ternary operator, so a three-way choice has to nest. Compared with the if-else version above, just counting the question marks and colons strains your eyes; "nearly unreadable" is precisely this feeling. One taste is enough: in real code, once you're past two levels of choice, honestly write `if/else` and save the ternary for two-way picks.

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra main.cpp -o main && ./main
```

Output:

```text
请输入一个整数: 10
10 是正数
```

:::

### Exercise 2: A Simple Calculator

Use `switch` to build a simple calculator: read two integers and an operator (`+`, `-`, `*`, `/`) from standard input, and print the result of the operation. For division, handle the divide-by-zero case.

Expected interaction:

```text
请输入表达式（如 3 + 5）: 10 / 0
错误：除数不能为零
```

::: details Reference answer

**main.cpp**

```cpp
#include <iostream>

int main()
{
    int left = 0;
    int right = 0;
    char operation = 0;

    std::cout << "请输入表达式（如 3 + 5）: ";
    if (!(std::cin >> left >> operation >> right)) {
        std::cout << "错误：输入格式无效" << std::endl;
        return 1;
    }

    switch (operation) {
    case '+':
        std::cout << left + right << std::endl;
        break;
    case '-':
        std::cout << left - right << std::endl;
        break;
    case '*':
        std::cout << left * right << std::endl;
        break;
    case '/':
        if (right == 0) {
            std::cout << "错误：除数不能为零" << std::endl;
            return 1;
        }
        std::cout << left / right << std::endl;
        break;
    default:
        std::cout << "错误：不支持的运算符" << std::endl;
        return 1;
    }

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra main.cpp -o main && ./main
```

Output:

```text
请输入表达式（如 3 + 5）: 10/0
错误：除数不能为零
```

:::

### Exercise 3: Date Validity Check

Write a function that receives three integers—year, month, and day—and uses conditional statements to decide whether the date is valid. You need to consider whether the month falls within 1-12, the differing day limits of each month, and the fact that February has 29 days in leap years. Hint: using `switch` to handle the day counts of different months comes out remarkably clean.

::: details Reference answer

**main.cpp**

```cpp
#include <iostream>

int main()
{
    int year, month, day;
    std::cout << "请输入年、月、日（用空格分隔，例如：2024 2 29）: ";
    if (!(std::cin >> year >> month >> day)) {
        std::cout << "错误：输入格式无效" << std::endl;
        return 1;
    }

    if (year <= 0 || month < 1 || month > 12 || day < 1) {
        std::cout << "这个日期不合法" << std::endl;
        return 1;
    }

    int maxDay = 31;
    switch (month) {
    case 2:
        maxDay = ((year % 400 == 0) ||
                  (year % 4 == 0 && year % 100 != 0))
                     ? 29
                     : 28;
        break;
    case 4:
    case 6:
    case 9:
    case 11:
        maxDay = 30;
        break;
    }

    if (day > maxDay) {
        std::cout << "这个日期不合法" << std::endl;
        return 1;
    }

    std::cout << "这个日期合法" << std::endl;
    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra main.cpp -o main && ./main
```

Output:

```text
请输入年、月、日（用空格分隔，例如：2024 2 29）: 2024 2 29
这个日期合法
```

:::
