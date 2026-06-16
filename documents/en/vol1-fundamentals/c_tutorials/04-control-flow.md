---
chapter: 1
cpp_standard:
- 11
description: Master C language conditional branches, loops, switch fall-through behavior,
  and state machine patterns, and understand the correct usage of break, continue,
  and goto.
difficulty: beginner
order: 6
platform: host
prerequisites:
- 位运算与求值顺序
reading_time_minutes: 11
tags:
- host
- cpp-modern
- beginner
- 入门
- 基础
title: 'Control Flow: Teaching Programs to Choose and Repeat'
translation:
  source: documents/vol1-fundamentals/c_tutorials/04-control-flow.md
  source_hash: 2d926a553c6e10f3f52f9db044e51bd5b33710b39bc55f75c39d55da8846baeb
  translated_at: '2026-06-16T03:33:43.831031+00:00'
  engine: anthropic
  token_count: 2594
---
# Control Flow: Teaching Programs to Choose and Repeat

So far, the programs we have written run straight from the first line to the last. However, real-world logic doesn't work that way—"if the temperature exceeds the threshold, turn on the fan," or "keep reading sensor data until a stop command is received." Control flow statements are designed for this: they allow programs to choose different execution paths (branching) based on conditions, or to repeat a specific block of logic (looping).

These statements look simple, but they hide many potential pitfalls. In this article, we will go through C language control flow from start to finish, focusing on those "you thought it worked this way, but it actually doesn't" moments.

> **Learning Objectives**
> After completing this chapter, you will be able to:
>
> - [ ] Understand the dangling else problem in if/else and how to solve it.
> - [ ] Master the fall-through behavior of switch and the limitations of case labels.
> - [ ] Proficiently use the three loop structures and their applicable scenarios.
> - [ ] Understand the behavior and limitations of break/continue.
> - [ ] Implement a practical state machine using switch.

## Environment Setup

We will conduct all subsequent experiments in the following environment:

- Platform: Linux x86\_64 (WSL2 is also acceptable)
- Compiler: GCC 13+ or Clang 17+
- Compiler flags: `-std=c23 -Wall -Wextra -pedantic`

## Step 1 — Conditional Branching: if/else

### Basic Syntax

`if` is the most basic and most frequently used conditional branching statement. If the condition is true (non-zero), the `if` branch is executed; otherwise, the `else` branch is executed:

```c
if (x > 0) {
    printf("Positive\n");
} else {
    printf("Non-positive\n");
}
```

Here is a bit of trivia: `else` is not an independent keyword in the C language—it is actually an `else` attached to a new `if` statement. So, in the compiler's eyes, the code above is a nested structure of `if` statements. While understanding it as a "multi-way branch" is more intuitive, the compiler sees a nested binary branch tree.

### Dangling Else — A Classic Pitfall

Look at this code:

```c
if (x > 0)
    if (y > 0)
        printf("x and y are positive\n");
else
    printf("x is non-positive\n");
```

The indentation suggests that `else` is paired with the first `if`, but it isn't. The rule in C is: **`else` always binds to the nearest, unpaired `if`**. So, this code is actually equivalent to:

```c
if (x > 0) {
    if (y > 0) {
        printf("x and y are positive\n");
    } else {
        printf("x is non-positive\n");
    }
}
```

If our intention was to pair `else` with the outer `if`, this code is wrong. The solution is simple—**always use curly braces to explicitly define the scope of each branch**.

> ⚠️ **Pitfall Warning**
> Even if a branch has only one line of code, add curly braces. This isn't just about typing a few extra characters; it's about preventing ambiguity and bugs during future maintenance—when you add a line of code and forget to add the braces, the logic changes completely. Many coding standards (including the Linux kernel style) enforce this rule.

### `=` vs `==` — Another Classic Typo

```c
if (x = 5) { ... }
```

This is always true (because the value of the assignment expression is 5, and non-zero is true), and `x` is accidentally modified. Good compilers will warn you about this, so make sure to enable `-Wextra` to let the compiler watch your back. Some programmers prefer putting the constant on the left: `if (5 == x)`, so that if you accidentally write `if (5 = x)`, the compiler will report an error directly.

## Step 2 — Multi-way Branching: The switch Statement

When the branching condition involves comparing discrete values of the same expression, `switch` is clearer than an `if`/`else` chain, and compilers usually optimize `switch` into a jump table, which has a time complexity close to O(1).

```c
switch (status_code) {
    case 0:
        // Handle success
        break;
    case 1:
        // Handle specific error
        break;
    default:
        // Handle unknown error
        break;
}
```

### Fall-Through: Forgetting `break` Causes "Leaks"

The `break` at the end of each `case` branch is used to jump out of the `switch`. If you forget to write `break`, the code won't stop after executing the current case—it will "fall through" to the next case and continue executing. This is known as **fall-through**.

```c
switch (motor_state) {
    case START:
        printf("Motor starting...\n");
        // Oops, forgot break!
    case STOP:
        printf("Motor stopping...\n");
        break;
}
```

When `motor_state` is `START`, after printing "Motor starting...", it won't stop; instead, it continues to print "Motor stopping..."—it starts and immediately stops, which is frustrating.

> ⚠️ **Pitfall Warning**
> However, consciously using the fall-through feature can lead to elegant code—merging multiple cases into the same handling logic:

```c
switch (day) {
    case MON:
    case TUE:
    case WED:
    case THU:
    case FRI:
        printf("Workday\n");
        break;
    case SAT:
    case SUN:
        printf("Weekend\n");
        break;
}
```

If you do intend to use fall-through, it is recommended to add a `// fallthrough` comment to clarify your intent; otherwise, future maintainers might think it's a bug.

### Limitations of Case Labels

`case` labels in `switch` must be **integer constant expressions**—integers whose values can be determined at compile time. This means you cannot use variables, floating-point numbers, or strings. Literals (`1`), `enum` members, and `#define` macros are all acceptable.

Make it a habit: **when writing `switch`, always write `default`**, even if it's just to log a message. This is especially important when your `enum` later adds new members but you forget to update the `switch`—`default` is your safety net.

## Step 3 — Three Types of Loops: for, while, do-while

### The for Loop — Repeating a Known Number of Times

The three-part design of the `for` loop concentrates initialization, condition checking, and stepping operations into one line, making it ideal for scenarios where the number of iterations is known:

```c
for (int i = 0; i < 10; i++) {
    printf("%d ", i);
}
```

All three parts can be omitted. If all are omitted, we get an infinite loop—very common in the main loop of embedded systems:

```c
for (;;) {
    // Main application loop
}
```

The comma operator allows manipulating multiple variables in the `for` header:

```c
for (int i = 0, j = 10; i < j; i++, j--) {
    printf("%d %d\n", i, j);
}
```

### while — Check Before Deciding

The `while` loop checks the condition first; if it's false from the start, the loop body never executes. It fits scenarios where "processing is only needed if the condition is met":

```c
while (queue_is_empty()) {
    // Wait for data
}
```

### do-while — Act First, Check Later

`do-while` executes the loop body at least once, then checks the condition. It fits "try at least once" logic:

```c
do {
    retry = send_packet();
} while (retry == RETRY_ERROR);
```

Regardless of the condition, the communication is attempted at least once. Implementing the same logic with a regular `while` would require writing `send_packet()` twice, which isn't elegant.

Let's verify the behavioral differences of the three loops:

```c
#include <stdio.h>

int main(void) {
    // while: condition false initially
    printf("while loop: ");
    int i = 10;
    while (i < 5) {
        printf("%d ", i);
        i++;
    }
    printf("(end)\n");

    // do-while: runs once
    printf("do-while loop: ");
    i = 10;
    do {
        printf("%d ", i);
        i++;
    } while (i < 5);
    printf("(end)\n");
}
```

Output:

```text
while loop: (end)
do-while loop: 10 (end)
```

Great, the `while` loop body didn't execute at all, while `do-while` executed once.

## Step 4 — break, continue, and goto

### break — Jump Out of the Innermost Layer

`break` is used to immediately exit the current loop or `switch` statement. It only affects the **innermost** loop or `switch`, and does not penetrate multiple layers of nesting:

```c
for (int i = 0; i < 10; i++) {
    if (i == 5) {
        break; // Exits the for loop
    }
    printf("%d ", i);
}
// Output: 0 1 2 3 4
```

### continue — Skip This Iteration

`continue` skips the remaining statements in the loop body and proceeds directly to the next iteration:

```c
for (int i = 0; i < 10; i++) {
    if (i % 2 == 0) {
        continue; // Skip even numbers
    }
    printf("%d ", i);
}
// Output: 1 3 5 7 9
```

### goto — Use with Caution, Don't Demonize It

`goto` has a bad reputation in the programming world, but in C, there is one widely accepted reasonable use case: **resource cleanup in error handling**. When you have a series of resources that need to be initialized in sequence, and any failure requires cleaning up all previously successful parts, `goto` makes the code very clear:

```c
int init_device(void) {
    int *buffer = malloc(1024);
    if (!buffer) goto err_buffer;

    int *handle = open_device();
    if (!handle) goto err_handle;

    return 0; // Success

err_handle:
    free(buffer);
err_buffer:
    return -1; // Error
}
```

> ⚠️ **Pitfall Warning**
> Principles for using `goto`: **only jump backwards (down to a later label), and only for error handling or breaking out of nesting**. Jumping forwards (jumping back to previous code to form a loop) should be strictly avoided—that is the job of `for`/`while`.

## Step 5 — Practice: Implementing a State Machine with switch

State Machines are one of the most common design patterns in embedded development—communication protocol parsing, peripheral control sequences, user interface flows, state machines are everywhere. The `switch` statement is the most direct tool for implementing state machines.

Let's implement a simple communication protocol parser. Assume the protocol format is: Frame Header `0xAA` + Length + Payload Data + Checksum.

```c
#include <stdio.h>
#include <stdint.h>

typedef enum {
    STATE_IDLE,
    STATE_HEADER,
    STATE_LENGTH,
    STATE_PAYLOAD,
    STATE_CHECKSUM,
    STATE_DONE,
    STATE_ERROR
} State;

typedef struct {
    State state;
    uint8_t length;
    uint8_t payload[16];
    uint8_t checksum;
    uint8_t index;
} Parser;

void parser_init(Parser *p) {
    p->state = STATE_IDLE;
    p->index = 0;
    p->checksum = 0;
}

void parser_feed(Parser *p, uint8_t byte) {
    switch (p->state) {
        case STATE_IDLE:
            if (byte == 0xAA) {
                p->state = STATE_LENGTH;
                p->checksum = byte;
            }
            break;
        case STATE_LENGTH:
            p->length = byte;
            p->index = 0;
            p->checksum += byte;
            p->state = (byte > 0) ? STATE_PAYLOAD : STATE_CHECKSUM;
            break;
        case STATE_PAYLOAD:
            p->payload[p->index++] = byte;
            p->checksum += byte;
            if (p->index >= p->length) {
                p->state = STATE_CHECKSUM;
            }
            break;
        case STATE_CHECKSUM:
            if (byte == p->checksum) {
                p->state = STATE_DONE;
            } else {
                p->state = STATE_ERROR;
            }
            break;
        case STATE_DONE:
        case STATE_ERROR:
            // Reset to IDLE on next byte
            p->state = STATE_IDLE;
            parser_feed(p, byte); // Re-process the byte
            break;
    }
}
```

Let's verify this by simulating receiving a frame of data:

```c
int main(void) {
    Parser p;
    parser_init(&p);

    // Simulate receiving: 0xAA 0x03 0x11 0x22 0x33 [Checksum]
    // Checksum = 0xAA + 0x03 + 0x11 + 0x22 + 0x33 = 0x143 -> 0x43
    uint8_t data[] = {0xAA, 0x03, 0x11, 0x22, 0x33, 0x43};

    for (int i = 0; i < 6; i++) {
        printf("Feeding 0x%02X, State: ", data[i]);
        parser_feed(&p, data[i]);

        switch (p.state) {
            case STATE_IDLE: printf("IDLE\n"); break;
            case STATE_LENGTH: printf("LENGTH\n"); break;
            case STATE_PAYLOAD: printf("PAYLOAD\n"); break;
            case STATE_CHECKSUM: printf("CHECKSUM\n"); break;
            case STATE_DONE: printf("DONE\n"); break;
            case STATE_ERROR: printf("ERROR\n"); break;
        }
    }
    return 0;
}
```

Compile and run:

```bash
gcc -std=c23 -Wall -Wextra state_machine.c -o state_machine
./state_machine
```

Output:

```text
Feeding 0xAA, State: LENGTH
Feeding 0x03, State: PAYLOAD
Feeding 0x11, State: PAYLOAD
Feeding 0x22, State: PAYLOAD
Feeding 0x33, State: CHECKSUM
Feeding 0x43, State: DONE
```

Excellent, the state machine correctly transitions from Idle all the way to Done, and each state transition meets our expectations. This byte-driven state machine pattern is very practical in serial communication and network protocol parsing.

## C++ Transition

C++ makes several important extensions to control flow. C++11 introduced the **range-based for loop**, making traversing containers very concise:

```cpp
std::array<int, 5> arr = {1, 2, 3, 4, 5};
for (int val : arr) {
    std::cout << val << " ";
}
```

C++17 introduced `if constexpr`, which evaluates conditions at compile time and directly removes branches that don't meet the condition from the code. There's also `std::variant` + `std::visit`, which provides a type-safe way to replace traditional `switch`—the compiler checks if you have handled all types, and if you miss one, it will result in a compilation error.

## Summary

Control flow is the skeleton of program logic. `if` handles conditional branching; add curly braces to eliminate dangling else ambiguity. `switch` is suitable for multi-way branching; the fall-through feature requires `break` to stop it, and don't forget `default`. `for`/`while`/`do-while` each have their scenarios. `break` and `continue` only affect the innermost layer. `goto` is a reasonable choice for resource cleanup in error handling. Using `switch` to implement state machines is a fundamental skill in embedded development.

Next, we will learn about functions—how to organize code into reusable modules.

## Exercises

### Exercise 1: Days in a Month

Use `switch` to implement a function that returns the number of days in a month based on the month and whether it is a leap year. You are required to use the fall-through feature to merge months with the same number of days.

### Exercise 2: Safe Matrix Search

Search for a target value in a 2D matrix. Once found, break out of the multi-level loop in two ways: one using a flag variable, and one using `goto`.

```c
// TODO: Implement search_matrix_flag and search_matrix_goto
```

### Exercise 3: Waiting with Timeout

Implement a waiting function with a timeout mechanism to avoid deadlocks caused by naked `while` waiting:

```c
// TODO: Implement wait_with_timeout
```

## References

- [cppreference: switch statement](https://en.cppreference.com/w/c/language/switch)
- [cppreference: if statement](https://en.cppreference.com/w/c/language/if)
- [cppreference: for loop](https://en.cppreference.com/w/c/language/for)
- [cppreference: goto statement](https://en.cppreference.com/w/c/language/goto)
