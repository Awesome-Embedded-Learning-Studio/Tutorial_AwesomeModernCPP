---
chapter: 1
cpp_standard:
- 11
description: Master C conditional branching, loops, switch fallthrough behavior, and
  the state machine pattern, and understand the correct usage of break, continue,
  and goto.
difficulty: beginner
order: 6
platform: host
prerequisites:
- 位运算与求值顺序
reading_time_minutes: 15
tags:
- host
- cpp-modern
- beginner
- 入门
- 基础
title: 'Control Flow: Teaching Programs to Choose and Repeat'
translation:
  source: documents/vol1-fundamentals/c_tutorials/04-control-flow.md
  source_hash: c432d40382b92c1c4b36dd849d5878805f1a742c101a18c77d32a5a8e659aaa9
  translated_at: '2026-04-20T03:16:01.216310+00:00'
  engine: anthropic
  token_count: 2594
---
# Control Flow: Teaching Programs to Choose and Repeat

So far, every program we've written runs straight from the first line to the last. But real-world logic doesn't work that way—"if the temperature exceeds the threshold, turn on the fan," "keep reading sensor data until a stop command is received." Control flow statements do exactly this: they let a program choose different execution paths based on conditions (branching), or repeat a block of logic (looping).

These statements look simple, but they hide plenty of pitfalls. In this chapter, we'll walk through C's control flow from start to finish, focusing on those "you thought it worked like this, but it actually doesn't" moments.

> **Learning Objectives**
> After completing this chapter, you will be able to:
>
> - [ ] Understand the dangling else problem in if/else and its solution
> - [ ] Master the fall-through behavior of switch and the limitations of case labels
> - [ ] Proficiently use three loop structures and their applicable scenarios
> - [ ] Understand the behavior and limitations of break/continue
> - [ ] Implement a practical state machine using switch

## Environment Setup

We will run all the following experiments in this environment:

- Platform: Linux x86\_64 (WSL2 is also fine)
- Compiler: GCC 13+ or Clang 17+
- Compiler flags: `-Wall -Wextra -std=c17`

## Step 1 — Conditional Branching: if/else

### Basic Syntax

`if/else` is the most fundamental and frequently used conditional branching statement. If the condition is true (non-zero), the `if` branch executes; otherwise, the `else` branch executes:

```c
if (temperature > kTempHighThreshold) {
    activate_cooling();
} else if (temperature < kTempLowThreshold) {
    activate_heating();
} else {
    maintain_temperature();
}
```

Here's a fun fact: `else if` is not an independent keyword in C—it's actually an `else` followed by a new `if` statement. So in the compiler's eyes, the code above is a nested `else { if (...) { } else { } }` structure. While thinking of it as a "multi-way branch" is more intuitive, the compiler sees a nested binary branch tree.

### Dangling Else — A Classic Pitfall

Look at this code:

```c
if (a > 0)
    if (b > 0)
        result = 1;
else
    result = -1;
```

The indentation makes it look like `else` pairs with the first `if`, but it doesn't. C's rule is: **`else` always binds to the nearest unpaired `if`**. So this code is actually equivalent to:

```c
if (a > 0) {
    if (b > 0) {
        result = 1;
    } else {
        result = -1;
    }
}
```

If our intention was to pair `else` with the outer `if`, this code is wrong. The solution is simple—**always use curly braces to explicitly define the scope of each branch**.

> ⚠️ **Pitfall Warning**
> Even if a branch has only one line of code, add curly braces. This isn't about typing a few extra characters—it's about preventing ambiguity and bugs during future maintenance. If you add a line of code and forget to add the braces, the logic changes completely. Many coding standards (including the Linux kernel style) strictly enforce this.

### `=` vs `==` — Another Classic Typo

`if (x = 5)` is always true (because the value of an assignment expression is 5, and non-zero means true), and `x` gets accidentally modified. A good compiler will warn you about this pattern, so make sure to enable `-Wall` to let the compiler watch your back. Some programmers prefer putting the constant on the left side: `if (5 == x)`. That way, if you accidentally write `if (5 = x)`, the compiler will throw a direct error.

## Step 2 — Multi-way Branching: The switch Statement

When branch conditions involve comparing a single expression against discrete values, `switch` is cleaner than an `if/else if` chain. Additionally, compilers typically optimize `switch` into a jump table, giving a lookup time complexity close to O(1).

```c
typedef enum {
    kCmdStart  = 0x01,
    kCmdStop   = 0x02,
    kCmdPause  = 0x03,
    kCmdResume = 0x04
} Command;

void handle_command(Command cmd) {
    switch (cmd) {
        case kCmdStart:
            start_operation();
            break;
        case kCmdStop:
            stop_operation();
            break;
        case kCmdPause:
            pause_operation();
            break;
        case kCmdResume:
            resume_operation();
            break;
        default:
            handle_unknown_command();
            break;
    }
}
```

### Fall-through: Forgetting break Causes a "Leak"

The `break` at the end of each `case` branch exits the `switch`. If you forget to write `break`, execution won't stop after the current case's code—it will "fall through" to the next case and keep executing. This is known as **fall-through**.

```c
switch (cmd) {
    case kCmdStart:
        start_operation();
        // 忘了 break！会穿透到 kCmdStop 的逻辑
    case kCmdStop:
        stop_operation();
        break;
}
```

When `cmd` is `kCmdStart`, execution doesn't stop after `start_operation()`—it continues to `stop_operation()`. You start up and immediately shut down. Blood pressure spikes.

> ⚠️ **Pitfall Warning**
> However, consciously leveraging fall-through can lead to very elegant code—merging multiple cases into the same handling logic:

```c
int days_in_month(int month, int is_leap_year) {
    switch (month) {
        case 1: case 3: case 5: case 7:
        case 8: case 10: case 12:
            return 31;
        case 4: case 6: case 9: case 11:
            return 30;
        case 2:
            return is_leap_year ? 29 : 28;
        default:
            return -1;
    }
}
```

If you do intentionally use fall-through, we recommend adding a `/* fall through */` comment to clarify your intent. Otherwise, someone maintaining the code later might assume it's a bug.

### Limitations of case Labels

Case labels in `switch` must be **integer constant expressions**—integers whose values are known at compile time. This means you can't use variables, floating-point numbers, or strings. Literals (`42`), `enum` members, and `#define` macros are all fine.

Make it a habit: **always write a `default` when you write a `switch`, even if it's just to log a message.** This is especially important when a new member is added to your `enum` but you forget to update the `switch`—the `default` acts as your safety net.

## Step 3 — Three Loop Types: for, while, and do-while

### The for Loop — Repeating a Known Number of Times

The three-part design of the `for` loop centralizes initialization, condition checking, and stepping into one line, making it ideal for scenarios with a known number of iterations:

```c
for (int i = 0; i < count; i++) {
    process_item(items[i]);
}
```

All three parts can be omitted. If you omit all of them, you get an infinite loop—extremely common in the main loop of embedded systems:

```c
for (;;) {
    read_sensors();
    process_data();
    update_outputs();
}
```

The comma operator lets you manipulate multiple variables in the `for` section:

```c
for (int i = 0, j = length - 1; i < j; i++, j--) {
    int temp = arr[i];
    arr[i] = arr[j];
    arr[j] = temp;
}
```

### while — Check First, Then Decide

The `while` loop checks the condition first. If it's false from the start, the loop body never executes. It suits scenarios where "processing is only needed when a condition is met":

```c
while (!uart_data_available()) {
    // 空转等待——实际项目中要加超时机制
}
```

### do-while — Act First, Ask Later

`do-while` executes the loop body at least once before checking the condition. It suits "try at least once" logic:

```c
do {
    result = attempt_communication();
    retry_count++;
} while (result != kSuccess && retry_count < kMaxRetries);
```

No matter the condition, the communication is attempted at least once. Implementing the same logic with a regular `while` would require writing `attempt_communication()` twice, which isn't as elegant.

Let's verify the behavioral differences between the three loops:

```c
#include <stdio.h>

int main(void)
{
    int count = 0;

    // while：条件一开始就是假，不执行
    while (count > 0) {
        printf("while: 不会打印这行\n");
        count--;
    }

    // do-while：至少执行一次
    count = 0;
    do {
        printf("do-while: count = %d\n", count);
        count++;
    } while (count < 3);

    return 0;
}
```

Output:

```text
do-while: count = 0
do-while: count = 1
do-while: count = 2
```

Great, the `while` loop body didn't execute at all, and `do-while` executed three times.

## Step 4 — break, continue, and goto

### break — Exit the Innermost Level

`break` immediately exits the current loop or `switch` statement. It only affects the **innermost** loop or `switch`, and won't penetrate multiple levels of nesting:

```c
for (int i = 0; i < rows; i++) {
    for (int j = 0; j < cols; j++) {
        if (matrix[i][j] == target) {
            printf("Found at [%d][%d]\n", i, j);
            break;  // 只跳出内层 j 循环，外层 i 循环继续
        }
    }
}
```

### continue — Skip the Current Iteration

`continue` skips the remaining statements in the loop body and jumps directly to the next iteration:

```c
for (int i = 0; i < count; i++) {
    if (data[i] == kInvalidMarker) {
        continue;  // 跳过无效数据
    }
    process_valid_data(data[i]);
}
```

### goto — Use Sparingly, But Don't Demonize It

`goto` has a bad reputation in the programming world, but in C, there is one widely accepted, reasonable use case: **resource cleanup during error handling**. When you have a series of resources that need to be initialized in order, and any step failing requires cleaning up all previously successful steps, `goto` can make the code very clear:

```c
int initialize_system(void) {
    if (!init_hardware()) {
        goto error_hardware;
    }
    if (!init_peripherals()) {
        goto error_peripherals;
    }
    if (!init_communication()) {
        goto error_communication;
    }
    return kSuccess;

error_communication:
    shutdown_peripherals();
error_peripherals:
    shutdown_hardware();
error_hardware:
    return kError;
}
```

> ⚠️ **Pitfall Warning**
> The rule for using `goto`: **only jump forward (down to a later label), and only for error handling or breaking out of nesting.** Jumping backward (back to earlier code to form a loop) should be strictly avoided—that's the job of `for`/`while`.

## Step 5 — Hands-on: Implementing a State Machine with switch

The state machine is one of the most common design patterns in embedded development—communication protocol parsing, peripheral control sequences, and user interface flows are all full of state machines. The `switch` statement is the most direct tool for implementing them.

Let's implement a simple communication protocol parser. Assume the protocol format is: frame header `0xAA` + length + payload data + checksum.

```c
typedef enum {
    kStateIdle,
    kStateHeader,
    kStatePayload,
    kStateChecksum,
    kStateDone,
    kStateError
} ParseState;

typedef struct {
    ParseState state;
    unsigned char payload[64];
    unsigned char payload_len;
    unsigned char index;
} Parser;

void parser_init(Parser* p) {
    p->state = kStateIdle;
    p->payload_len = 0;
    p->index = 0;
}

ParseState parser_feed(Parser* p, unsigned char byte) {
    switch (p->state) {
        case kStateIdle:
            if (byte == 0xAA) {
                p->state = kStateHeader;
            }
            break;

        case kStateHeader:
            p->payload_len = byte;
            if (p->payload_len > 64) {
                p->state = kStateError;
            } else {
                p->index = 0;
                p->state = kStatePayload;
            }
            break;

        case kStatePayload:
            p->payload[p->index++] = byte;
            if (p->index >= p->payload_len) {
                p->state = kStateChecksum;
            }
            break;

        case kStateChecksum: {
            unsigned char calc = 0;
            for (int i = 0; i < p->payload_len; i++) {
                calc ^= p->payload[i];
            }
            p->state = (calc == byte) ? kStateDone : kStateError;
            break;
        }

        case kStateDone:
        case kStateError:
            break;
    }
    return p->state;
}
```

Let's verify this by simulating the reception of a data frame:

```c
#include <stdio.h>

int main(void)
{
    Parser p;
    parser_init(&p);

    // 帧头 0xAA，长度 3，负载 {0x01, 0x02, 0x03}，校验 0x00
    unsigned char frame[] = {0xAA, 0x03, 0x01, 0x02, 0x03, 0x00};
    for (int i = 0; i < (int)sizeof(frame); i++) {
        ParseState s = parser_feed(&p, frame[i]);
        printf("Byte 0x%02X → State %d\n", frame[i], s);
        if (s == kStateDone) {
            printf("Frame OK, payload: ");
            for (int j = 0; j < p.payload_len; j++) {
                printf("0x%02X ", p.payload[j]);
            }
            printf("\n");
            break;
        } else if (s == kStateError) {
            printf("Parse error at byte %d\n", i);
            break;
        }
    }
    return 0;
}
```

Compile and run:

```bash
gcc -Wall -Wextra -std=c17 parser.c -o parser && ./parser
```

Output:

```text
Byte 0xAA → State 1
Byte 0x03 → State 2
Byte 0x01 → State 2
Byte 0x02 → State 2
Byte 0x03 → State 3
Byte 0x00 → State 4
Frame OK, payload: 0x01 0x02 0x03
```

Great, the state machine correctly transitioned from Idle all the way to Done, and each state transition matched our expectations. This byte-driven state machine pattern is extremely practical in serial communication and network protocol parsing.

## Bridging to C++

C++ makes several important extensions to control flow. C++11 introduced the **range-based for loop**, making container traversal very concise:

```cpp
int arr[] = {1, 2, 3, 4, 5};
for (int x : arr) {
    std::cout << x << " ";
}
// 不需要手动管理索引、判断边界、递增计数器
```

C++17 introduced `if constexpr`, which evaluates conditions at compile time and directly strips out unmet branches from the code. There's also `std::variant` + `std::visit`, which provides a type-safe way to replace traditional `switch`—the compiler checks whether you've handled all types, and will directly throw a compile error if you miss one.

## Summary

Control flow is the skeleton of program logic. `if/else` handles conditional branching; add curly braces to eliminate dangling else ambiguity. `switch` suits multi-way branching; fall-through needs `break` to stop it, and don't forget to add `default`. The three loop types, `for`/`while`/`do-while`, each have their own applicable scenarios. `break` and `continue` only affect the innermost level. `goto` is a reasonable choice for resource cleanup in error handling. Implementing state machines with `switch` is a fundamental skill in embedded development.

Next, we'll learn about functions—how to organize code into reusable modules.

## Exercises

### Exercise 1: Days in a Month

Use `switch` to implement a function that returns the number of days in a given month, accounting for leap years. Use fall-through to merge months with the same number of days.

### Exercise 2: Safe Matrix Search

Search for a target value in a two-dimensional matrix. After finding it, break out of the nested loops in two ways: one using a flag variable, and one using `goto`.

```c
typedef struct {
    int row;
    int col;
    int found;
} SearchResult;

SearchResult matrix_search(int** matrix, int rows, int cols, int target);
```

### Exercise 3: Waiting with a Timeout

Implement a wait function with a timeout mechanism to avoid deadlocks caused by bare `while` waiting:

```c
/// @brief 等待某个条件满足或超时
/// @param check 条件检查函数，返回非零表示条件满足
/// @param timeout_ms 超时时间（毫秒）
/// @return 0 表示条件满足，-1 表示超时
int wait_with_timeout(int (*check)(void), unsigned int timeout_ms);
```

## References

- [cppreference: switch statement](https://en.cppreference.com/w/c/language/switch)
- [cppreference: if statement](https://en.cppreference.com/w/c/language/if)
- [cppreference: for loop](https://en.cppreference.com/w/c/language/for)
- [cppreference: goto statement](https://en.cppreference.com/w/c/language/goto)
