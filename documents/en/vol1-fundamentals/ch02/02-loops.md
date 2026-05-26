---
title: Loop Statements
description: Master `for`, `while`, and `do-while` loops along with `break`/`continue`
  control flow, and learn how to make programs repeat tasks.
chapter: 2
order: 2
difficulty: beginner
reading_time_minutes: 12
platform: host
prerequisites:
- 条件语句
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
  source: documents/vol1-fundamentals/ch02/02-loops.md
  source_hash: 9447fb008659a8ae3f77a7cfdf5a2a58ac7ac9444e94a7d00a831d6317f50e15
  translated_at: '2026-05-26T10:44:38.948124+00:00'
  engine: anthropic
  token_count: 2080
---
# Loop Statements

Computers excel at tirelessly repeating the same task. One could even say that our internet world is built on endlessly storing and retrieving data, relentlessly evaluating zeros and ones, and looping through it all!

Humans get tired. If we asked you to manually print 100 lines of "Hello", you would clearly tell us that CharlieChen114514 has lost their mind. But a computer only needs a single loop instruction to get it done. Loop statements let us tell a program, "repeat this action N times" or "keep doing this until a condition is met"—this is the core structure of almost every meaningful program.

In this chapter, we will break down C++'s three loop structures inside out, focusing on which scenarios suit each loop, when to use `break` and `continue`, and the common pitfalls lurking in nested loops.

> **Learning Objectives**
> After completing this chapter, you will be able to:
>
> - [ ] Master the syntax and use cases of `while`, `do-while`, and `for` loops
> - [ ] Correctly use `break` and `continue` to control loop flow
> - [ ] Understand the execution process and time complexity of nested loops
> - [ ] Independently write programs for pattern printing and simple numerical calculations

## Step One — The `while` Loop: Keep Going When You Don't Know the Count

The `while` loop is the most straightforward loop structure: it checks the condition first, executes the loop body if the condition is true, and then loops back to check again, stopping only when the condition becomes false.

```cpp
while (condition) {
    // 循环体
}
```

Before entering the loop body each time, it evaluates the `condition` once. If the result is `true`, it executes the code inside the curly braces. After execution, it loops back to evaluate the condition again. If the condition is `false` from the very beginning, the loop body will never execute.

When do we use `while`? The most typical scenario is "we don't know in advance how many times we need to loop." For example, continuously prompting the user to input numbers to accumulate a sum until they enter 0:

```cpp
#include <iostream>

int main()
{
    int sum = 0;
    int value = 0;

    std::cout << "请输入数字（输入 0 结束）: ";
    std::cin >> value;

    while (value != 0) {
        sum += value;
        std::cout << "当前累加和: " << sum << std::endl;
        std::cout << "请继续输入（0 结束）: ";
        std::cin >> value;
    }

    std::cout << "最终结果: " << sum << std::endl;
    return 0;
}
```

Running effect:

```text
请输入数字（输入 0 结束）: 10
当前累加和: 10
请继续输入（0 结束）: 25
当前累加和: 35
请继续输入（0 结束）: 0
最终结果: 35
```

The loop body must contain an operation that changes the condition (here, we read a new value into `num` each time); otherwise, it becomes an infinite loop.

> ⚠️ **Pitfall Warning**: Infinite loops are the most common pitfall in `while` loops. If there is no operation inside the loop body that can make the condition `false`, the program will run forever and never exit. For example, if we forget to write the line that reads `num`, `num` will never change, and the condition will always be true. When writing `while` loops, make it a habit to check: "Is there code in the loop body that changes the condition?"

## Step Two — The `do-while` Loop: Do It Once First

`do-while` is very similar to `while`, with one key difference: the loop body executes at least once. The condition check is placed after the loop body:

```cpp
do {
    // 循环体
} while (condition);  // 注意这里有个分号！
```

Because of its "do first, check later" nature, `do-while` is particularly suited for scenarios like menu systems—the menu must be displayed at least once, and then we decide whether to continue based on the user's choice:

```cpp
int choice = 0;
do {
    std::cout << "\n=== 菜单 ===" << std::endl;
    std::cout << "1. 打印问候  0. 退出" << std::endl;
    std::cout << "请选择: ";
    std::cin >> choice;
    if (choice == 1) {
        std::cout << "你好！欢迎学习 C++！" << std::endl;
    }
} while (choice != 0);
```

> ⚠️ **Pitfall Warning**: Do not forget the semicolon at the end of `do-while`. If you omit it, the compiler will parse the next line of code as the `while` loop body, and the error message can be very bizarre. This is one of the few places in C++ where a semicolon is mandatory after `)`, which is different from `if`, `for`, and `while`, making it easy to confuse.

## Step Three — The `for` Loop: The Top Choice When the Count Is Known

When the number of loop iterations is known, the `for` loop is the clearest choice. It concentrates the initialization, condition check, and increment operations into a single line, making the loop's range visible at a glance:

```cpp
for (init; condition; increment) {
    // 循环体
}
```

The execution order is: execute the `init` once, then check the `condition`. If true, execute the loop body. After execution, perform the `increment`, then loop back to check the `condition`, and so on.

```cpp
for (int i = 1; i <= 10; ++i) {
    std::cout << i << " ";
}
// 输出: 1 2 3 4 5 6 7 8 9 10
```

Here, the scope of `i` is limited to the inside of the `for` loop—it cannot be accessed once outside the loop body. This is a feature supported since C++11.

`for` also supports manipulating multiple variables simultaneously. Let's demonstrate this with a classic two-pointer reversal:

```cpp
int data[] = {1, 2, 3, 4, 5};
int n = 5;

// 双指针从两端向中间走，交换元素
for (int i = 0, j = n - 1; i < j; ++i, --j) {
    int temp = data[i];
    data[i] = data[j];
    data[j] = temp;
}
// data 现在是 {5, 4, 3, 2, 1}
```

The initialization part declares two variables, `left` and `right`, and the increment part simultaneously performs `left++` and `right--`, approaching from both ends toward the middle and stopping when they meet.

> ⚠️ **Pitfall Warning**: The off-by-one error is the most classic pitfall in `for` loops. The intention might be to loop 10 times, but writing `i < 10` results in only 9 iterations. A practical tip: develop a fixed habit—either always start from 0 and use `<` (`i < n`), or start from 1 and use `<=` (`i <= n`). Do not mix them; mixing is the breeding ground for off-by-one errors.

## Step Four — `break` and `continue`: The "Emergency Exits" in Loops

`break` immediately exits the current loop without re-evaluating the condition—just like its meaning implies, it breaks our loop! `continue` skips the remaining code of the current iteration and proceeds directly to the next iteration.

```cpp
int data[] = {4, 7, 2, 9, 5, 1};
int target = 9;

for (int i = 0; i < 6; ++i) {
    if (data[i] == target) {
        std::cout << "找到 " << target << "，下标为 " << i << std::endl;
        break;  // 找到了，不用继续搜了
    }
}
// 输出: 找到 9，下标为 3
```

An example of `continue`—printing odd numbers between 1 and 20:

```cpp
for (int i = 1; i <= 20; ++i) {
    if (i % 2 == 0) {
        continue;  // 偶数跳过
    }
    std::cout << i << " ";
}
// 输出: 1 3 5 7 9 11 13 15 17 19
```

Note that `break` can only exit the innermost loop. When there are two nested layers, an inner `break` will only exit the inner loop, and the outer loop will continue as normal. To exit multiple layers at once, we usually use a flag variable combined with an outer condition check, or encapsulate the logic into a function and use `return` to exit.

> ⚠️ **Pitfall Warning**: Overusing `break` and `continue` makes code logic fragmented, forcing readers to mentally jump around to track the execution flow. If a loop body contains more than two or three `break` or `continue` statements, consider whether the loop condition should be written more clearly, or whether part of the logic should be extracted into a separate function. Simple, straightforward loop conditions are always easier to maintain than `break` statements scattered everywhere.

## Step Five — Nested Loops: Loops Inside Loops

A loop body can contain another loop. This solves two-dimensional problems like "do X for each row, and do Y for each column within that row." Let's look at the classic multiplication table:

```cpp
#include <iostream>
#include <iomanip>  // std::setw

int main()
{
    for (int i = 1; i <= 9; ++i) {
        for (int j = 1; j <= i; ++j) {
            std::cout << j << "x" << i << "=" << std::setw(2) << i * j << " ";
        }
        std::cout << std::endl;
    }
    return 0;
}
```

Running result:

```text
1x1= 1
1x2= 2 2x2= 4
1x3= 3 2x3= 6 3x3= 9
1x4= 4 2x4= 8 3x4=12 4x4=16
...
1x9= 9 2x9=18 3x9=27 4x9=36 5x9=45 6x9=54 7x9=63 8x9=72 9x9=81
```

The outer loop controls the row number `i`, and the inner loop controls the column number `j`. `j` iterates from 1 to `i`, printing a triangle shape. `std::setw(2)` makes each output item occupy a width of 2 characters, so single-digit and double-digit numbers align neatly.

The number of executions in a nested loop is the product of the iterations of each layer. With N times in the outer loop and M times in the inner loop, the inner loop body executes a total of N * M times. For a double nested loop with N=1000, the inner loop executes one million times—so we must keep this concept in mind: when dealing with large amounts of data, the fewer nested layers, the better.

## Complete Practical Exercise — loops.cpp

Let's combine the several loop types we learned earlier into a single program: a multiplication table, a number-guessing mini-game (`while` + `break`), and a pyramid pattern print (nested `for`).

```cpp
// loops.cpp -- 综合循环练习
// 编译: g++ -Wall -Wextra -o loops loops.cpp

#include <iostream>
#include <iomanip>

/// @brief 打印九九乘法表
void print_multiplication_table()
{
    std::cout << "=== 九九乘法表 ===" << std::endl;
    for (int i = 1; i <= 9; ++i) {
        for (int j = 1; j <= i; ++j) {
            std::cout << j << "x" << i << "=" << std::setw(2) << i * j << " ";
        }
        std::cout << std::endl;
    }
}

/// @brief 猜数字游戏，演示 while + break 的配合
void guess_number_game()
{
    const int kSecret = 42;
    int guess = 0;
    int attempts = 0;

    std::cout << "\n=== 猜数字游戏 ===" << std::endl;
    std::cout << "我想了一个 1-100 之间的数字，你来猜！" << std::endl;

    while (true) {
        std::cout << "你的猜测: ";
        std::cin >> guess;
        ++attempts;

        if (guess == kSecret) {
            std::cout << "恭喜！你用了 " << attempts << " 次猜中了！" << std::endl;
            break;
        } else if (guess < kSecret) {
            std::cout << "太小了，再试试。" << std::endl;
        } else {
            std::cout << "太大了，再试试。" << std::endl;
        }
    }
}

/// @brief 打印由星号组成的金字塔
void print_pyramid()
{
    const int kHeight = 5;

    std::cout << "\n=== 金字塔图案 ===" << std::endl;
    for (int row = 1; row <= kHeight; ++row) {
        // 打印前导空格
        for (int space = 0; space < kHeight - row; ++space) {
            std::cout << " ";
        }
        // 打印星号（第 row 行有 2*row - 1 个星号）
        for (int star = 0; star < 2 * row - 1; ++star) {
            std::cout << "*";
        }
        std::cout << std::endl;
    }
}

int main()
{
    print_multiplication_table();
    guess_number_game();
    print_pyramid();

    return 0;
}
```

Compile and run:

```bash
g++ -Wall -Wextra -o loops loops.cpp
./loops
```

```text
=== 九九乘法表 ===
1x1= 1
1x2= 2 2x2= 4
...（中间省略）
1x9= 9 2x9=18 ... 9x9=81

=== 猜数字游戏 ===
你的猜测: 50
太大了，再试试。
你的猜测: 25
太小了，再试试。
你的猜测: 42
恭喜！你用了 3 次猜中了！

=== 金字塔图案 ===
    *
   ***
  *****
 *******
*********
```

Let's break down the pyramid logic. For row `i`, we need `kHeight - i - 1` leading spaces to center the asterisks, and then we print `2 * i + 1` asterisks. This pattern of `2 * i + 1` is very common in pattern printing. The `while (true)` + `break` in the number-guessing game is also a classic pattern—when the exit condition isn't easy to condense into a single boolean expression, judging inside the loop body and then using `break` is a clear approach.

## Run Online

Run the comprehensive loop example online to observe the output of the multiplication table, pyramid pattern, and prime number sieve:

<OnlineCompilerDemo
  title="循环语句综合演示：乘法表、金字塔、素数"
  source-path="code/examples/vol1/06_loops.cpp"
  description="在线运行并观察 for 循环、嵌套循环和 break 的综合运用。试着修改 kHeight 或素数范围。"
  allow-run
/>

## Try It Yourself

Just understanding it isn't enough; you have to write it yourself to truly master it. Below are four exercises, and we recommend completing each one hands-on.

### Exercise 1: Print a Hollow Square

Input a positive integer N and print an N x N hollow square. For example, when N=5:

```text
* * * * *
*       *
*       *
*       *
* * * * *
```

Only the first row, last row, first column, and last column print asterisks; the middle is all spaces. Hint: use a nested `for` loop, and have the inner loop check whether the current position is on the boundary.

### Exercise 2: Calculate Factorials

Use a `while` loop to calculate the factorial of N (N!). For example, 5! = 120. Note that factorials grow extremely fast; with `int`, 13! will overflow. Try seeing how large `long long` can handle.

### Exercise 3: Find Prime Numbers

Input a positive integer N and print all prime numbers between 2 and N. The method to determine a prime: for a number m, check if there is any number between 2 and m-1 that can divide m evenly; if not, it is a prime. Hint: use the outer loop to iterate through candidate numbers, use the inner loop to perform the divisibility check, and use `break` to exit the inner loop early once a factor is found.

### Exercise 4: Print a Diamond

Input an odd integer N and print an N-row diamond pattern. For example, when N=5:

```text
  *
 ***
*****
 ***
  *
```

Hint: the upper half is the same as the pyramid, and the lower half is a mirror of the pyramid—the row numbers go from large to small.

## Summary

In this chapter, we went through all three of C++'s loop structures completely. `while` suits scenarios where "we don't know the count, keep going while the condition is met," `do-while` guarantees the loop body executes at least once (most commonly used in menu systems), and `for` is the clearest when the loop count is known because it groups initialization, condition, and increment together. `break` is used to urgently exit a loop, and `continue` is used to skip the current iteration, but do not overuse them—clear loop conditions are always better than control flow jumping around everywhere. Nested loops can solve two-dimensional problems, but we must be mindful of the O(N^2) growth in execution count.

In the next chapter, we will encounter the range-for loop introduced in C++11—a more modern and safer way to traverse containers and arrays. With the foundation from this chapter, you will find that range-for is simply a breath of fresh air.

---

> **Self-Assessment of Difficulty**: If you feel confused about the execution order of nested loops, we suggest grabbing a pen and manually simulating the execution process of the multiplication table on paper—tracking the values of the outer variable `i` and the inner variable `j` at each step. This builds a very intuitive understanding.
