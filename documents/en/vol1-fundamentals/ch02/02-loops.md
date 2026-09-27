---
chapter: 2
cpp_standard:
- 11
- 14
- 17
- 20
description: Master for, while, and do-while loops plus break and continue control, and learn to make your program repeat tasks
difficulty: beginner
order: 2
platform: host
prerequisites:
- Conditional Statements
reading_time_minutes: 11
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: Loop Statements
translation:
  source: documents/vol1-fundamentals/ch02/02-loops.md
  source_hash: 0e8dc38c9e45a473b872b7d1c978b51f0b3712a93c086d213905aa0ae967afd6
  translated_at: '2026-09-25T10:08:56+00:00'
  engine: anthropic
  token_count: 7600
---
# Loop Statements: Handing the Repetitive Chores to the Machine

What computers are best at is repeating the same task over and over without ever getting tired. Better yet, you could say the computer is nothing but endless data storing and fetching, tireless judging of 0s and 1s, and loops doing it all again and again — and that is what our internet world is built from!

Humans get tired. If I asked you right now to manually print 100 lines of "Hello", you would simply say CharlieChen114514 has clearly lost his mind. But a computer gets it done with a single loop instruction. Loop statements let us tell a program "repeat this action N times" or "keep doing it until some condition is met" — the core structure of almost every meaningful program.

## The while Loop We Know Best: Keep Going When You Don't Know the Count

The `while` loop is the most straightforward loop structure: check the condition first; if it is true, execute the loop body; once it finishes, come back and check again, stopping only when the condition is false.

```cpp
while (condition) {
    // Loop body
}
```

Before each entry into the loop body, `condition` is evaluated once. If the result is `true`, the code inside the braces runs; after it finishes, control returns to the condition for another check. If the condition is `false` from the very start, the loop body never executes even once.

When do we use `while`? The most typical scenario is "we don't know in advance how many times we need to loop, but we definitely know what the exit condition is". For example, keep asking the user to enter numbers and accumulate them, until they enter 0:

```cpp
#include <iostream>

int main()
{
    int sum = 0;
    int value = 0;

    std::cout << "请输入数字（输入 0 结束）: ";
    std::cin >> value;

    // Well, what will happen we cannot know, but we do know:
    // once our value is no longer 0, the loop is headed for retirement.
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

Let's compile the little program above; a run looks like this:

```text
请输入数字（输入 0 结束）: 10
当前累加和: 10
请继续输入（0 结束）: 25
当前累加和: 35
请继续输入（0 结束）: 0
最终结果: 35
```

There must be an operation inside the loop body that changes the condition (here, it is us re-reading `value` every iteration); otherwise it turns into an **infinite loop.**

**An infinite loop that isn't intended is the most common pitfall of `while`.** If nothing inside the loop body can ever make the condition `false`, the program keeps running forever and never exits. For instance, if you forget to write the `std::cin >> value;` line, `value` never changes and the condition stays true forever. When writing a `while` loop, get into the habit of checking "is there code inside the loop body that changes the condition?".

## The do-while Loop: A while Variant That Acts First, Checks Later

`do-while` looks a lot like `while`, with exactly one key difference: the loop body executes at least once. The condition check is placed after the loop body:

```cpp
do {
    // Loop body
} while (condition);  // Note the semicolon here!
```

Because of this "act first, judge later" nature, `do-while` is a particularly good fit for scenarios like menu systems — the menu has to be displayed at least once, and then you decide whether to continue based on the user's choice:

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

Whatever you do, don't forget the semicolon at the end of a `do-while`. Leave it out and the compiler will parse the next line of code as the while loop's body, and the error messages can get downright bizarre. This is one of the few places in C++ that requires a semicolon after the `}` — unlike `if`, `while`, and `for` — so it is very easy to mix up.

> P.S. Some of you may have seen the macro trick `do { /* some code */  } while(0);` — it is very common in C. But let me be clear: in today's compiler environment, this idiom is pure redundancy. A function does the job perfectly well.

## Another Evenly Matched Contender: The for Loop

When the number of iterations is known, the `for` loop is the clearest choice. It gathers initialization, condition checking, and the increment into a single line, so the loop's range is visible at a glance:

```cpp
for (init; condition; increment) {
    // Loop body
}
```

The execution order is: run `init` once, then check `condition`; if true, execute the loop body; once it completes, do `increment`, then go back and check `condition` again, and so on.

```cpp
for (int i = 1; i <= 10; ++i) {
    // Read this output like so: starting from i = 1, keep executing until i no longer satisfies the relation i <= 10, approaching it by incrementing i by 1.
    std::cout << i << " ";
}
// Note: once we are out of this for loop, i is no longer usable!
// Output: 1 2 3 4 5 6 7 8 9 10
```

`for` also supports manipulating several variables at once — here is a classic two-pointer reversal to demonstrate:

```cpp
int data[] = {1, 2, 3, 4, 5};
int n = 5;

// Two pointers walk from both ends toward the middle, swapping elements
for (int i = 0, j = n - 1; i < j; ++i, --j) {
    int temp = data[i];
    data[i] = data[j];
    data[j] = temp;
}
// data is now {5, 4, 3, 2, 1}
```

The initialization section declares two variables, `i` and `j`; the increment section does both `++i` and `--j`, closing in from both ends toward the middle and stopping when they meet.

The off-by-one error is the classic trap of the `for` loop. You mean to loop 10 times, but write `for (int i = 1; i < 10; ++i)` and it only runs 9 times. One practical tip: build a fixed habit. You have two paradigms to choose from.

- Either always start from 0 and use `<` (`for (int i = 0; i < n; ++i)`)
- Or start from 1 and use `<=` (`for (int i = 1; i <= n; ++i)`)

**Don't mix them — mixing is the breeding ground for off-by-one errors.**

## When You Truly Need to Interrupt Mid-Loop: The "Emergency Exits" break and continue

`break` jumps out of the current loop immediately, without checking the condition again — just like the name break says: break our loop! `continue` skips the remaining code of the current pass and goes straight into the next iteration.

```cpp
int data[] = {4, 7, 2, 9, 5, 1};
int target = 9;

for (int i = 0; i < 6; ++i) {
    if (data[i] == target) {
        std::cout << "找到 " << target << "，下标为 " << i << std::endl;
        break;  // Found it, no need to keep searching
    }
}
// Output: 找到 9，下标为 3
```

An example with `continue` — printing the odd numbers between 1 and 20:

```cpp
for (int i = 1; i <= 20; ++i) {
    if (i % 2 == 0) {
        continue;  // Skip even numbers
    }
    std::cout << i << " ";
}
// Output: 1 3 5 7 9 11 13 15 17 19
```

Note that `break` only breaks out of the innermost loop. With two nested levels, a `break` in the inner loop only leaves the inner loop — the outer one keeps turning exactly as before. To break out of multiple levels of loop at once, the usual approaches are a flag variable combined with a condition check in the outer loop, or wrapping the logic in a function and exiting with `return`.

Overusing `break` and `continue` shatters the code logic into fragments, forcing whoever reads the code to bounce around in their head tracking the flow of execution. If a loop body contains more than two or three `break` or `continue` statements, it is time to consider whether the loop condition should be written more clearly, or whether part of the logic should be pulled out into a separate function. A simple, direct loop condition is always easier to maintain than `break`s scattered everywhere.

## Step Five — Nested Loops: A Loop Inside a Loop

A loop body can itself hold another loop, and that solves the two-dimensional kind of problem: "do X for each row, and do Y for each column within that row". Let's look at the classic 9x9 multiplication table:

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

The outer loop controls the row number `i`, and the inner loop controls the column number `j`; `j` iterates from 1 to `i`, so what gets printed is a triangle. `std::setw(2)` makes each output item occupy 2 characters of width, so single-digit and double-digit values line up.

The execution count of a nested loop is the product of the iteration counts of each level. With the outer loop running N times and the inner M times, the inner loop body executes N * M times in total. For a two-level nest with N=1000, the inner body executes one million times — so keep this concept firmly in mind: when the data volume is large, the fewer nesting levels, the better.

## Full Practice — loops.cpp

Let's combine the loops we have learned into one program: the 9x9 multiplication table, a number-guessing mini-game (`while` + `break`), and a pyramid pattern printer (nested `for`).

```cpp
// loops.cpp -- a comprehensive loop exercise
// Compile: g++ -Wall -Wextra -o loops loops.cpp

#include <iostream>
#include <iomanip>

/// @brief Print the 9x9 multiplication table
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

/// @brief A number-guessing game, demonstrating while + break working together
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

/// @brief Print a pyramid made of asterisks
void print_pyramid()
{
    const int kHeight = 5;

    std::cout << "\n=== 金字塔图案 ===" << std::endl;
    for (int row = 1; row <= kHeight; ++row) {
        // Print the leading spaces
        for (int space = 0; space < kHeight - row; ++space) {
            std::cout << " ";
        }
        // Print the stars (the row-th row has 2*row - 1 stars)
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
...(middle omitted)
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

Let's take the pyramid logic apart. Row `row` needs `kHeight - row` leading spaces to center the stars, then prints `2 * row - 1` stars. This `2n-1` pattern shows up all the time in pattern printing. The `while (true)` + `break` in the guessing game is another classic idiom — when the exit condition isn't easily condensed into a single boolean expression, checking inside the loop body and then breaking is a clean approach.

## Run Online

Run the comprehensive loop example online and observe the output of the multiplication table, the pyramid pattern, and the prime sieve:

<OnlineCompilerDemo
  title="Loop Statements in Action: Multiplication Table, Pyramid, Primes"
  source-path="code/examples/vol1/06_loops.cpp"
  description="Run online and observe the combined use of for loops, nested loops, and break. Try modifying kHeight or the prime range."
  allow-run
/>

## Try It Yourself

Just reading along isn't enough; you have to write it yourself to truly know it. Here are four exercises — I recommend doing every one of them by hand.

### Exercise 1: Print a Hollow Square

Input a positive integer N and print an N x N hollow square. For example, with N=5:

```text
 *  *  *  *  *
 *           *
 *           *
 *           *
 *  *  *  *  *
```

Only the first row, the last row, the first column, and the last column print asterisks; everything in between is spaces. Hint: use nested `for` loops, with the inner loop checking whether the current position is on the boundary.

::: details Reference answer

```cpp
#include <iostream>

int main()
{
    int n = 0;
    std::cout << "输入一个正整数 N: ";
    if (!(std::cin >> n) || n < 1)
    {
        std::cout << "输入无效，请输入一个正整数！" << std::endl;
        return 1;
    }
    for (int i = 1; i <= n; i++)
    {
        for (int j = 1; j <= n; j++)
        {
            if ((i != 1) && (i != n))
            {
                if ((j != 1) && (j != n))
                {
                    std::cout << "   ";
                    continue;
                }
            }
            std::cout << " * ";
        }
        std::cout << std::endl;
    }
    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17  -Wall -Wextra main.cpp -o main &&./main
```

Running result:

```text
输入一个正整数 N: 5
 *  *  *  *  *
 *           *
 *           *
 *           *
 *  *  *  *  *
```

:::

### Exercise 2: Compute a Factorial

Use a `for` loop to compute the factorial of N (N!). For example, 5! = 120. Note that factorials grow extremely fast: with `int`, 13! already overflows. Try seeing how far `long long` can hold up.

::: details Reference answer

```cpp
#include <iostream>

int main()
{
    int n = 0;
    long long factorial = 1;
    std::cout << "输入一个正整数 N: ";
    if (!(std::cin >> n) || n < 1)
    {
        std::cout << "输入无效，请输入一个正确的正整数！" << std::endl;
        return 1;
    }

    for (int i = n; i >= 1; i--)
    {
        factorial *= i;
    }
    std::cout << n << "的阶乘" << "(" << n << "!): " << factorial << std::endl;

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17  -Wall -Wextra main.cpp -o main &&./main
```

Running result:

```text
输入一个正整数 N: 5
5的阶乘(5!): 120
```

:::

### Exercise 3: Find Prime Numbers

Input a positive integer N and print all the primes between 2 and N. How to test for primality: for a number m, check whether any number from 2 to m-1 divides m evenly; if none does, m is prime. Hint: the outer loop walks the candidates, the inner loop does the divisibility check, and once a factor is found, use `break` to leave the inner loop early.

::: details Reference answer

```cpp
#include <iostream>

int main()
{
    int n = 0;
    bool flag=0;
    std::cout << "输入一个正整数 N: ";
    if (!(std::cin >> n) || n < 2)
    {
        std::cout << "输入无效，请输入一个正确的正整数！" << std::endl;
        return 1;
    }
    std::cout << "2" << "到" << n << "之间所有的素数: " << std::endl;
    for (int i = 2; i <= n; i++)
    {
        flag = 0;
        for (int j = 2; j <= i - 1; j++)
        {

            if (i % j == 0)
            {
                flag = 1;
                break;
            }
        }
        if (flag != 1)
        {
            std::cout << i << std::endl;
        }
    }

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17  -Wall -Wextra main.cpp -o main &&./main
```

Running result:

```text
输入一个正整数 N: 5
2到5之间所有的素数:
2
3
5
```

:::

### Exercise 4: Print a Diamond

Input an odd number N and print a diamond pattern with N rows. For example, with N=5:

```text
  *
 ***
*****
 ***
  *
```

Hint: the top half is exactly the pyramid, and the bottom half is the pyramid's mirror image — the row numbers run from large to small.

::: details Reference answer

```cpp
#include <iostream>

int main()
{
    int n = 0;
    std::cout << "输入一个正奇数 N: ";

    if (!(std::cin >> n) || n <= 0 || n % 2 == 0)
    {
        std::cout << "输入无效，请输入一个正确的正奇数！" << std::endl;
        return 1;
    }

    const int middleRow = (n + 1) / 2;
    for (int row = 1; row <= n; ++row)
    {
        const int stars = row <= middleRow
                              ? 2 * row - 1
                              : 2 * (n - row) + 1;
        const int spaces = (n - stars) / 2;

        for (int column = 0; column < spaces; ++column)
        {
            std::cout << ' ';
        }

        for (int column = 0; column < stars; ++column)
        {
            std::cout << '*';
        }

        std::cout << std::endl;
    }

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17  -Wall -Wextra main.cpp -o main &&./main
```

Running result:

```text
输入一个正奇数 N: 5
  *
 ***
*****
 ***
  *
```

:::
