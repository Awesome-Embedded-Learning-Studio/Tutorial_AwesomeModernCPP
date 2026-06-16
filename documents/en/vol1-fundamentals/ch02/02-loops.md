---
chapter: 2
cpp_standard:
- 11
- 14
- 17
- 20
description: Master `for`, `while`, and `do-while` loops, along with `break` and `continue`
  control statements, to learn how to make programs repeat tasks.
difficulty: beginner
order: 2
platform: host
prerequisites:
- 条件语句
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
  source_hash: 37832941202d59658fad49d959a69f7f182d83192fc39011ce35986f8800bfb2
  translated_at: '2026-06-16T03:41:45.297109+00:00'
  engine: anthropic
  token_count: 2076
---
# Loop Statements

Computers excel at tirelessly repeating the same task. One might even say that our digital world is built on endless data storage, retrieval, binary judgment, and loops!

Humans get tired. If I asked you to manually print 100 lines of "Hello", you'd tell me I'm crazy. But a computer handles this with a single loop instruction. Loop statements allow us to tell a program, "Repeat this action N times" or "Keep doing this until a condition is met"—this is a core structure in almost all meaningful programs.

In this chapter, we will dissect C++'s three loop structures inside and out. We will focus on which scenarios suit each loop, when to use `break` and `continue`, and the common pitfalls to avoid in nested loops.

> **Learning Objectives**
> After completing this chapter, you will be able to:
>
> - [ ] Master the syntax and use cases for `while`, `do-while`, and `for` loops.
> - [ ] Correctly use `break` and `continue` to control loop flow.
> - [ ] Understand the execution process and time complexity of nested loops.
> - [ ] Independently write programs for pattern printing and simple numerical calculations.

## Step 1 — The `while` Loop: Keep Going Until a Condition is Met

The `while` loop is the most straightforward loop structure: it checks the condition first; if true, it executes the loop body. After execution, it returns to check the condition again, stopping only when the condition becomes false.

```cpp
while (condition) {
    // Loop body
}
```

Before entering the loop body each time, the `condition` is evaluated once. If the result is `true`, the code inside the braces is executed. After completion, it returns to the condition for judgment. If the condition is `false` from the start, the loop body will never execute.

When do we use `while`? The most typical scenario is "we don't know in advance how many times we need to loop." For example, continuously asking the user to input numbers to sum them up, until the input is 0:

```cpp
#include <iostream>

int main() {
    int sum = 0;
    int input;

    std::cout << "Enter numbers to sum (end with 0): " << std::endl;

    // We don't know how many numbers the user will enter
    while (std::cin >> input && input != 0) {
        sum += input;
    }

    std::cout << "Total sum: " << sum << std::endl;
    return 0;
}
```

Running result:

```text
Enter numbers to sum (end with 0):
10
20
30
0
Total sum: 60
```

There must be an operation inside the loop body that changes the condition (here, we re-read `input` every time); otherwise, it becomes an infinite loop.

> ⚠️ **Pitfall Warning**: Infinite loops are the most common trap in `while` loops. If there is no operation inside the loop body that can make the condition `false`, the program will run forever and never exit. For example, if you forget to write the line that reads `input`, `input` never changes, and the condition remains true forever. When writing `while` loops, make it a habit to check: "Is there code inside the loop body that changes the condition?"

## Step 2 — The `do-while` Loop: Do It First, Ask Later

`do-while` is very similar to `while`, with one key difference: the loop body executes at least once. The condition check is placed after the loop body:

```cpp
do {
    // Loop body
} while (condition);
```

Because of its "act first, judge later" nature, `do-while` is particularly suitable for scenarios like menu systems—the menu must be displayed at least once, and then we decide whether to continue based on the user's choice:

```cpp
#include <iostream>

int main() {
    int choice;
    do {
        std::cout << "1. View Data" << std::endl;
        std::cout << "2. Edit Data" << std::endl;
        std::cout << "3. Exit" << std::endl;
        std::cout << "Please select: ";
        std::cin >> choice;
    } while (choice != 3); // Exit only when user chooses 3

    std::cout << "Goodbye!" << std::endl;
    return 0;
}
```

> ⚠️ **Pitfall Warning**: Don't forget the semicolon at the end of `do-while`. If you miss it, the compiler will parse the next line of code as the `while` loop's body, leading to potentially cryptic error messages. This is one of the few places in C++ where a semicolon must follow `}`, which is different from `if`, `while`, and `for`, making it easy to confuse.

## Step 3 — The `for` Loop: The Top Choice for Known Counts

When the number of loops is known, the `for` loop is the clearest choice. It concentrates initialization, condition checking, and increment operations into one line, making the scope of the loop immediately visible:

```cpp
for (initialization; condition; increment) {
    // Loop body
}
```

Execution order: execute `initialization` once, then check `condition`. If true, execute the loop body. After execution, perform `increment`, then go back to check `condition`, and so on.

```cpp
#include <iostream>

int main() {
    // Print 0 to 9
    for (int i = 0; i < 10; ++i) {
        std::cout << i << " ";
    }
    // Output: 0 1 2 3 4 5 6 7 8 9
    return 0;
}
```

Here, the scope of `i` is limited to the inside of the `for` loop—once the loop body ends, it is no longer accessible. This is a feature supported since C++11.

`for` also supports manipulating multiple variables simultaneously. Let's demonstrate this with a classic two-pointer reversal:

```cpp
#include <iostream>
#include <string>

int main() {
    std::string str = "Hello";
    int n = str.length();

    // Initialize two variables: left and right
    for (int left = 0, right = n - 1; left < right; ++left, --right) {
        std::swap(str[left], str[right]);
    }

    std::cout << str << std::endl; // Output: olleH
    return 0;
}
```

The initialization section declares two variables, `left` and `right`. The increment section performs both `++left` and `--right`, approaching from both ends towards the middle, stopping when they meet.

> ⚠️ **Pitfall Warning**: The off-by-one error is the classic pitfall of `for` loops. You might intend to loop 10 times but write `i <= 9` (if starting from 1) or `i < 9` (if starting from 0), resulting in only 9 iterations. A practical tip: form a fixed habit—either always start from 0 using `<` (0 to N-1), or start from 1 using `<=` (1 to N). Don't mix them; mixing is a breeding ground for off-by-one errors.

## Step 4 — `break` and `continue`: The "Emergency Exits" in Loops

`break` immediately jumps out of the current loop without checking the condition again—just like its meaning implies: breaking our loop! `continue` skips the remaining code of the current iteration and proceeds directly to the next iteration.

```cpp
// break example
for (int i = 0; i < 10; ++i) {
    if (i == 5) {
        break; // Exit loop immediately when i is 5
    }
    std::cout << i << " "; // Output: 0 1 2 3 4
}
```

`continue` example—printing odd numbers between 1 and 20:

```cpp
for (int i = 1; i <= 20; ++i) {
    if (i % 2 == 0) {
        continue; // Skip even numbers
    }
    std::cout << i << " "; // Print odd numbers
}
```

Note that `break` only breaks out of the innermost loop. When nested two levels deep, a `break` inside the inner loop only exits the inner loop; the outer loop continues as usual. To break out of multiple layers at once, we usually use a flag variable combined with an outer condition check, or encapsulate the logic into a function and use `return` to exit.

> ⚠️ **Pitfall Warning**: Overusing `break` and `continue` can make the code logic fragmented, forcing readers to jump around mentally to track the execution flow. If a loop body contains more than two or three `break` or `continue` statements, consider whether the loop condition should be written more clearly, or if part of the logic should be extracted into a separate function. Simple, direct loop conditions are always easier to maintain than control flow that jumps around everywhere.

## Step 5 — Nested Loops: Loops Inside Loops

We can place a loop inside another loop body. This solves "2D problems" like "do X for each row, and do Y for each column in that row." Let's look at the classic 9x9 multiplication table:

```cpp
#include <iostream>
#include <iomanip> // For std::setw

int main() {
    for (int i = 1; i <= 9; ++i) {      // Outer loop: rows
        for (int j = 1; j <= i; ++j) {  // Inner loop: columns
            std::cout << j << "x" << i << "=" << (i * j) << "\t";
        }
        std::cout << std::endl;
    }
    return 0;
}
```

Running result:

```text
1x1=1
1x2=2    2x2=4
1x3=3    2x3=6    3x3=9
1x4=4    2x4=8    3x4=12    4x4=16
1x5=5    2x5=10    3x5=15    4x5=20    5x5=25
1x6=6    2x6=12    3x6=18    4x6=24    5x6=30    6x6=36
1x7=7    2x7=14    3x7=21    4x7=28    5x7=35    6x7=42    7x7=49
1x8=8    2x8=16    3x8=24    4x8=32    5x8=40    6x8=48    7x8=56    8x8=64
1x9=9    2x9=18    3x9=27    4x9=36    5x9=45    6x9=54    7x9=63    8x9=72    9x9=81
```

The outer loop controls the row number `i`, and the inner loop controls the column number `j`. `j` iterates from 1 to `i`, printing a triangle. `\t` aligns the output items (tab character).

The execution count of a nested loop is the product of the counts of all layers. N times for the outer layer and M times for the inner layer results in N * M executions of the inner loop body. For a double nested loop with N=1000, the inner body executes one million times—so keep this concept in mind: with large data, fewer nesting levels is better.

## Full Practice — loops.cpp

Let's combine the loops we learned into one program: the 9x9 multiplication table, a number guessing game (`while` + `break`), and a pyramid pattern printer (nested `for`).

```cpp
#include <iostream>
#include <cstdlib>  // for rand() and srand()
#include <ctime>    // for time()
#include <iomanip>  // for std::setw

int main() {
    // 1. 9x9 Multiplication Table
    std::cout << "=== 9x9 Multiplication Table ===" << std::endl;
    for (int i = 1; i <= 9; ++i) {
        for (int j = 1; j <= i; ++j) {
            std::cout << j << "x" << i << "=" << (i * j) << "\t";
        }
        std::cout << std::endl;
    }

    // 2. Number Guessing Game
    std::cout << "\n=== Number Guessing Game ===" << std::endl;
    std::srand(std::time(0)); // Seed random number generator
    int target = std::rand() % 100 + 1; // Random number 1-100
    int guess = 0;

    while (true) {
        std::cout << "Guess a number (1-100): ";
        std::cin >> guess;

        if (guess < target) {
            std::cout << "Too low! Try again." << std::endl;
        } else if (guess > target) {
            std::cout << "Too high! Try again." << std::endl;
        } else {
            std::cout << "Correct! You guessed it!" << std::endl;
            break; // Exit loop on success
        }
    }

    // 3. Pyramid Pattern
    std::cout << "\n=== Pyramid Pattern ===" << std::endl;
    const int kHeight = 5;
    for (int i = 1; i <= kHeight; ++i) {
        // Print leading spaces
        for (int j = 0; j < kHeight - i; ++j) {
            std::cout << " ";
        }
        // Print stars
        for (int k = 0; k < 2 * i - 1; ++k) {
            std::cout << "*";
        }
        std::cout << std::endl;
    }

    return 0;
}
```

Compile and run:

```bash
g++ loops.cpp -o loops
./loops
```

Output:

```text
=== 9x9 Multiplication Table ===
1x1=1
1x2=2    2x2=4
...
1x9=9    2x9=18    ...    9x9=81

=== Number Guessing Game ===
Guess a number (1-100): 50
Too high! Try again.
Guess a number (1-100): 25
Too low! Try again.
...
Correct! You guessed it!

=== Pyramid Pattern ===
    *
   ***
  *****
 *******
*********
```

Let's break down the pyramid logic. For row `i`, we need `kHeight - i` leading spaces to center the stars, then print `2 * i - 1` stars. This pattern of `2 * i - 1` is very common in pattern printing. The `while (true)` + `break` in the number guessing game is also a classic pattern—when the exit condition isn't easily condensed into a single boolean expression, judging inside the loop body and then breaking is a clear approach.

## Run Online

Run the comprehensive loop example online to observe the output of the multiplication table, pyramid pattern, and prime number sieve:

<OnlineCompilerDemo
  title="Loop Statement Demo: Multiplication Table, Pyramid, Primes"
  source-path="code/examples/vol1/06_loops.cpp"
  description="Run online to see the combined use of for loops, nested loops, and break. Try modifying kHeight or the prime number range."
  allow-run
/>

## Try It Yourself

Just understanding isn't enough; you need to write it yourself to truly master it. Here are four exercises; I suggest completing each one.

### Exercise 1: Print a Hollow Square

Input a positive integer N and print an N x N hollow square. For example, when N=5:

```text
*****
*   *
*   *
*   *
*****
```

Only the first row, last row, first column, and last column print asterisks; the middle is all spaces. Hint: Use nested `for` loops, and have the inner loop check if the current position is a boundary.

### Exercise 2: Calculate Factorial

Use a `for` loop to calculate the factorial of N (N!). For example, 5! = 120. Note that factorials grow very fast. With `int`, 13! will overflow. See how large `long long` can go.

### Exercise 3: Find Prime Numbers

Input a positive integer N and print all prime numbers between 2 and N. Method to check for primes: for a number m, check if there is any number between 2 and m-1 that divides m evenly. If not, it is a prime. Hint: Use an outer loop to iterate through candidates and an inner loop for divisibility checks. Use `break` to exit the inner loop early if a factor is found.

### Exercise 4: Print a Diamond

Input an odd number N and print a diamond pattern with N rows. For example, when N=5:

```text
  *
 ***
*****
 ***
  *
```

Hint: The top half is the same as the pyramid; the bottom half is a mirror image of the pyramid—the row numbers go from large to small.

## Summary

In this chapter, we went through all three of C++'s loop structures. `while` is suitable for "unknown count, continue while condition is met" scenarios. `do-while` guarantees the loop body executes at least once (most common in menu systems). `for` is clearest when the loop count is known because it groups initialization, condition, and increment together. `break` is for emergency exits, and `continue` is for skipping the current round, but don't abuse them—clear loop conditions are always better than control flow that jumps around everywhere. Nested loops can solve 2D problems, but be mindful of the O(N^2) growth in execution count.

In the next chapter, we will encounter the range-based `for` loop introduced in C++11—a more modern and safer way to traverse containers and arrays. With the foundation of this chapter, you will find range-for to be a breath of fresh air.

---

> **Self-Assessment of Difficulty**: If you are confused about the execution order of nested loops, I suggest taking a pen and manually simulating the execution process of the 9x9 multiplication table on paper—track the values of the outer variable `i` and the inner variable `j` at each step. This will build a very intuitive understanding.
