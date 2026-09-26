---
chapter: 1
cpp_standard:
- 11
description: Master C's conditional branches, loops, switch fall-through behavior, and the state machine pattern, and understand the correct use of break, continue, and goto.
difficulty: beginner
order: 6
platform: host
prerequisites:
- Bitwise Operations and Evaluation Order
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
  source_hash: 8fa2fc6d9cf8d8523478289fe456f6a549d46dce3dcb5e9e8b52e08eee0fa5f0
  translated_at: '2026-09-25T12:57:51+00:00'
  engine: anthropic
  token_count: 8300
---
# Control Flow: Teaching Programs to Choose and Repeat

So far, every program we have written runs straight from the first line to the last. Real-world logic doesn't work that way—"if the temperature crosses the threshold, turn on the fan," "keep reading sensor data until a stop command arrives." That is exactly what control flow statements are for: they let a program choose different execution paths based on a condition (branching), or run a piece of logic over and over (looping).

These statements look simple, but they hide plenty of pits that are easy to step into. In this post we'll walk C's control flow from top to bottom, keeping a sharp eye on the places where "you'd assume it works this way, but it actually doesn't."

## Step 1 — Conditional Branching: if/else

### Basic Syntax

`if/else` is the most basic and most frequently used conditional branch statement. If the condition is true (nonzero), the `if` branch runs; otherwise the `else` branch runs:

```c
if (temperature > kTempHighThreshold) {
    activate_cooling();
} else if (temperature < kTempLowThreshold) {
    activate_heating();
} else {
    maintain_temperature();
}
```

A little trivia: `else if` is not a standalone keyword in C—it is really just an `else` followed by a brand-new `if` statement. So in the compiler's eyes, the code above is a nested structure of the form `else { if (...) { } else { } }`. Thinking of it as a "multi-way branch" is more intuitive, but what the compiler sees is a nested binary branch tree.

### Dangling else — A Classic Trap

Look at this piece of code:

```c
if (a > 0)
    if (b > 0)
        result = 1;
else
    result = -1;
```

The indentation makes it look like the `else` pairs with the first `if`, but it doesn't. C's rule is: **`else` always binds to the nearest `if` that hasn't been paired yet**. So this code is actually equivalent to:

```c
if (a > 0) {
    if (b > 0) {
        result = 1;
    } else {
        result = -1;
    }
}
```

If we actually meant for the `else` to pair with the outer `if`, this code is simply wrong. The fix is easy—**always use braces to delimit the extent of every branch explicitly**.

Add braces even when a branch contains only a single line. This isn't about typing a few extra characters—it's about preventing ambiguity and future-maintenance bugs: you add one more line, forget to add the braces, and the logic silently changes meaning. Many coding standards (including the Linux kernel style) make this mandatory.

### `=` vs `==` — Another Classic Typo

`if (x = 5)` is always true (the assignment expression's value is 5, and nonzero means true), and on top of that `x` gets modified by accident. Good compilers warn about this pattern, so make sure `-Wall` is on and let the compiler keep watch for you. Some programmers habitually put the constant on the left: `if (5 == x)`, so that a slip of the hand like `if (5 = x)` becomes a hard compile error.

## Step 2 — Multi-Way Branching: the switch Statement

When the branch condition compares one expression against a set of discrete values, `switch` reads cleaner than an `if/else if` chain—and compilers usually optimize a `switch` into a jump table, giving a table lookup with time complexity close to O(1).

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

### Fall-Through: Forget a break and It Leaks

The `break` at the end of each `case` branch exits the `switch`. Forget to write it, and after the current case's code finishes, execution doesn't stop—it "falls through" into the next case and keeps going. This is the notorious **fall-through**.

```c
switch (cmd) {
    case kCmdStart:
        start_operation();
        // Forgot the break! Falls through into the kCmdStop logic
    case kCmdStop:
        stop_operation();
        break;
}
```

When `cmd` is `kCmdStart`, `start_operation()` runs and then, instead of stopping, execution continues into `stop_operation()`—the thing starts up and immediately shuts itself down. Blood pressure through the roof.

But deliberately exploiting fall-through can produce very elegant code—merging several cases into one shared piece of handling:

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

If you do mean to rely on fall-through, it's good practice to add a `/* fall through */` comment stating your intent—otherwise whoever maintains the code later will assume it's a bug.

### Constraints on case Labels

A `switch`'s case labels must be **integer constant expressions**—integers whose values are known at compile time. That means no variables, no floating-point numbers, no strings. Literals (`42`), `enum` members, and `#define` macros all work.

Make it a habit: **whenever you write a `switch`, write a `default`**—even if all it does is log one line. Especially when your `enum` later gains new members and you forget to update the `switch`, `default` is your safety net.

## Step 3 — Three Loops: for, while, and do-while

### The for Loop — Repeating a Known Number of Times

The `for` loop's three-part design packs initialization, condition check, and step operation into a single line, which makes it a perfect fit when the iteration count is known:

```c
for (int i = 0; i < count; i++) {
    process_item(items[i]);
}
```

All three parts can be omitted. Omit them all and you get an infinite loop—extremely common as the main loop of an embedded system:

```c
for (;;) {
    read_sensors();
    process_data();
    update_outputs();
}
```

The comma operator lets a `for` manipulate several variables at once:

```c
for (int i = 0, j = length - 1; i < j; i++, j--) {
    int temp = arr[i];
    arr[i] = arr[j];
    arr[j] = temp;
}
```

### while — Check First, Decide After

A `while` loop checks the condition first; if it is false from the very start, the body never executes at all. It fits the "only process while the condition holds" kind of scenario:

```c
while (!uart_data_available()) {
    // Busy-waiting — a real project needs a timeout mechanism here
}
```

### do-while — Do First, Ask Later

A `do-while` runs the body at least once, and only then checks the condition. It fits the "try at least once" kind of logic:

```c
do {
    result = attempt_communication();
    retry_count++;
} while (result != kSuccess && retry_count < kMaxRetries);
```

Whatever the condition says, the communication gets attempted at least once. Doing the same with a plain `while` would mean writing `attempt_communication()` twice—not elegant.

Let's verify the behavioral differences between the three loops:

```c
#include <stdio.h>

int main(void)
{
    int count = 0;

    // while: the condition is false from the start, so the body never runs
    while (count > 0) {
        printf("while: 不会打印这行\n");
        count--;
    }

    // do-while: runs at least once
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

As expected: the `while` body never ran once, while the `do-while` ran three times.

## Step 4 — break, continue, and goto

### break — Out of the Innermost Level

`break` immediately exits the current loop or `switch` statement. It affects only the **innermost** loop or `switch` and does not punch through multiple levels of nesting:

```c
for (int i = 0; i < rows; i++) {
    for (int j = 0; j < cols; j++) {
        if (matrix[i][j] == target) {
            printf("Found at [%d][%d]\n", i, j);
            break;  // only breaks out of the inner j loop; the outer i loop keeps going
        }
    }
}
```

### continue — Skip the Current Iteration

`continue` skips the remaining statements in the loop body and jumps straight to the next iteration:

```c
for (int i = 0; i < count; i++) {
    if (data[i] == kInvalidMarker) {
        continue;  // skip invalid data
    }
    process_valid_data(data[i]);
}
```

### goto — Use With Care, but Don't Demonize It

`goto` has a poor reputation in the programming world, but in C there is one widely accepted legitimate use for it: **cleaning up resources during error handling**. When you have a series of resources to initialize in order, and any failed step means undoing the ones that already succeeded, `goto` keeps the code remarkably clear:

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

The rule of thumb for `goto`: **only jump forward (down to a label below), and only for error handling or escaping nested loops**. Jumping backward (back up to earlier code, forming a loop) should be firmly avoided—that's the job of `for`/`while`.

## Step 5 — In Practice: A State Machine Built on switch

The state machine is one of the most common design patterns in embedded development—protocol parsing, peripheral control sequences, user-interface flows: state machines are everywhere you look. The `switch` statement is the most direct tool for implementing one.

Let's implement a simple communication protocol parser. Suppose the frame format is: header `0xAA` + length + payload data + checksum.

```c
typedef enum {
    kStateIdle,      // Idle: waiting for the 0xAA frame header
    kStateHeader,    // Header: header received, now waiting for the length byte
    kStatePayload,   // Payload: currently receiving data
    kStateChecksum,  // Checksum: about to verify the data
    kStateDone,      // Done: one frame parsed successfully
    kStateError      // Error: something is wrong (e.g., length over the limit or checksum mismatch)
} ParseState;

typedef struct {
    ParseState state;            // records the current state
    unsigned char payload[64];   // the warehouse: stores the received payload data
    unsigned char payload_len;   // records how many payload bytes this frame expects
    unsigned char index;         // counter: how many bytes have been received so far
} Parser;

void parser_init(Parser* p) {
    p->state = kStateIdle;
    p->payload_len = 0;
    p->index = 0;
}

ParseState parser_feed(Parser* p, unsigned char byte) {
    switch (p->state) {
        case kStateIdle:
            if (byte == 0xAA) {       // Did we see the frame header?
                p->state = kStateHeader; // Yes — move on to the next state (waiting for length)
            }
            break;

        case kStateHeader:
            p->payload_len = byte;    // treat this received byte as the length and store it
            if (p->payload_len > 64) { // Length too big — what if it won't fit in the warehouse?
                p->state = kStateError; // Error!
            } else {
                p->index = 0;         // about to receive data; reset the counter
                p->state = kStatePayload; // enter the payload-receiving state
            }
            break;

        case kStatePayload:
            p->payload[p->index++] = byte; // store the byte in the warehouse and bump the counter
            if (p->index >= p->payload_len) { // Received enough yet?
                p->state = kStateChecksum; // Yes — enter the checksum state
            }
            break;

        case kStateChecksum: {
            unsigned char calc = 0;
            for (int i = 0; i < p->payload_len; i++) {
                calc ^= p->payload[i]; // XOR all the received data together, bit by bit
            }
            p->state = (calc == byte) ? kStateDone : kStateError; // compare the computed value against the received checksum
            break;
        }

        case kStateDone:
        case kStateError:
            break; // do nothing
    }
    return p->state; // tell the caller what state we're in
}
```

To verify it, let's simulate receiving one frame:

```c
#include <stdio.h>

int main(void)
{
    Parser p;
    parser_init(&p);

    // Header 0xAA, length 3, payload {0x01, 0x02, 0x03}, checksum 0x00
    unsigned char frame[] = {0xAA, 0x03, 0x01, 0x02, 0x03, 0x00};
    for (int i = 0; i < (int)sizeof(frame); i++) {
        ParseState s = parser_feed(&p, frame[i]);
        printf("Byte 0x%02X → State %d\n", frame[i], s);
        if (s == kStateDone) {
            // If the parser says "done", print out the received data
            printf("Frame OK, payload: ");
            for (int j = 0; j < p.payload_len; j++) {
                printf("0x%02X ", p.payload[j]);
            }
            printf("\n");
            break;
        } else if (s == kStateError) {
            // If the parser reports an error, stop as well
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

The state machine walked correctly from Idle all the way to Done, and every state transition matched our expectation. This byte-by-byte driven state machine pattern is extremely practical in serial communication and network protocol parsing.

## C++ Transition

C++ made several important extensions to control flow. C++11 introduced the **range-based for loop**, making container traversal extremely concise:

```cpp
int arr[] = {1, 2, 3, 4, 5};
for (int x : arr) {
    std::cout << x << " ";
}
// No manual index management, bounds checking, or counter incrementing needed
```

C++17 introduced `if constexpr`, which evaluates its condition at compile time and simply strips the untaken branch out of the code. And there is `std::variant` + `std::visit`, a type-safe replacement for the traditional `switch`—the compiler checks that you have handled every type; miss one and it's a straight compile error.

## Exercises

### Exercise 1: Days in a Month

**Difficulty: Beginner** · practice the switch fall-through behavior

Use a `switch` to implement a function that returns the number of days in a month, given the month and whether the year is a leap year. Use fall-through to merge months that share the same day count.

::: details Reference solution

```c
bool is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int month_day(int year, int month) {
    switch (month) {
        case 1: case 3: case 5: case 7: case 8: case 10: case 12:
            return 31;
        case 4: case 6: case 9: case 11:
            return 30;
        case 2:
            return is_leap_year(year) ? 29 : 28;
        default:
            return -1;
    }
}
```

:::

### Exercise 2: A Safe Matrix Search

**Difficulty: Intermediate** · two ways to break out of nested loops

Search for a target value in a 2D matrix. Once found, break out of the nested loops in two different ways: one using a flag variable, one using `goto`.

```c
typedef struct {
    int row;
    int col;
    int found;
} SearchResult;

SearchResult matrix_search(int** matrix, int rows, int cols, int target);
```

### Exercise 3: Hand-Write a Protocol Frame Parser State Machine

**Difficulty: Intermediate** · implement a state machine with switch plus a state variable

The end of this post demonstrated a byte-by-byte driven serial protocol state machine (start marker `0xAA` → payload length → payload → end marker `0x55`). Now implement an equivalent parser yourself: feed each received byte into `frame_feed`, which uses `switch (state)` internally to move between states, and print the payload once a complete frame has been received.

```c
#include <stdint.h>

typedef enum { STATE_IDLE, STATE_LEN, STATE_PAYLOAD, STATE_DONE } FrameState;

/// @brief Feed in one byte at a time; returns 1 when a complete frame (including the end marker) is received, otherwise 0
int frame_feed(uint8_t byte);
```

The requirement: implement it with a `switch` plus an explicit state variable—no long if-else chains. And think about one more thing: if the peer's "payload length" field is tampered into a value larger than your buffer, does your state machine get wrecked? How would you defend against it?

::: details Reference solution

```c
#include <stdio.h>
#include <stdint.h>

#define MAX_PAYLOAD 16

typedef enum { STATE_IDLE, STATE_LEN, STATE_PAYLOAD, STATE_DONE } FrameState;

static FrameState state = STATE_IDLE;
static uint8_t payload[MAX_PAYLOAD];
static uint8_t payload_len = 0;
static uint8_t payload_idx = 0;

int frame_feed(uint8_t byte) {
    switch (state) {
        case STATE_IDLE:
            if (byte == 0xAA) {         // only move to the next state once the start marker arrives
                payload_idx = 0;
                payload_len = 0;
                state = STATE_LEN;
            }
            break;
        case STATE_LEN:
            // Defense: the length field may be tampered with; clamp it to the buffer capacity to avoid an out-of-bounds write later
            payload_len = (byte <= MAX_PAYLOAD) ? byte : MAX_PAYLOAD;
            state = (payload_len == 0) ? STATE_DONE : STATE_PAYLOAD;
            break;
        case STATE_PAYLOAD:
            payload[payload_idx++] = byte;
            if (payload_idx >= payload_len) {
                state = STATE_DONE;
            }
            break;
        case STATE_DONE:
            if (byte == 0x55) {         // the normal end marker
                printf("Frame OK (%u bytes):", payload_len);
                for (uint8_t i = 0; i < payload_len; i++) {
                    printf(" %02X", payload[i]);
                }
                printf("\n");
                state = STATE_IDLE;
                return 1;
            }
            state = STATE_IDLE;         // the end marker never arrived — the frame is broken, go idle and wait for 0xAA again
            break;
    }
    return 0;
}
```

The key is that every `case` explicitly spells out "who the next state is"—that's exactly why a state machine reads cleaner than a long chain of if-else. Clamping the length in `STATE_LEN` is the most basic defense in protocol parsing: never blindly trust a length field sent by the other side.

:::

## References

- [cppreference: switch statement](https://en.cppreference.com/w/c/language/switch)
- [cppreference: if statement](https://en.cppreference.com/w/c/language/if)
- [cppreference: for loop](https://en.cppreference.com/w/c/language/for)
- [cppreference: goto statement](https://en.cppreference.com/w/c/language/goto)
