---
chapter: 1
cpp_standard:
- 11
description: Master the declaration and usage of function pointers, understand the
  application of the callback function pattern in event-driven programming, and compare
  C++ lambda expressions and std::function.
difficulty: beginner
order: 13
platform: host
prerequisites:
- 07A 指针基础与核心用法
- 07B 指针、数组与 const
- 08A 多级指针与函数参数
reading_time_minutes: 10
tags:
- host
- cpp-modern
- beginner
- 入门
title: Function Pointers and the Callback Pattern
translation:
  source: documents/vol1-fundamentals/c_tutorials/09-function-pointers-and-callbacks.md
  source_hash: 72179d3582f6fc37c0503f4cc3cce524bab303a276cc4fc8c1852dc5436e6519
  translated_at: '2026-06-16T04:37:47.353631+00:00'
  engine: anthropic
  token_count: 1866
---
# Function Pointers and the Callback Pattern

If pointers are the most powerful feature of C, then function pointers are arguably the most blood-pressure-raising aspect of the pointer world. But honestly, once you master them, you will find they are one of the few mechanisms in C that allow you to write code that is "flexible enough to not feel like C"—callbacks, event-driven programming, the strategy pattern; these concepts that sound like they belong in high-level languages are all supported in C thanks to function pointers.

We have systematically covered various pointer usages in previous tutorials. In this chapter, we will tackle this hard nut: function pointers. We will start with declarations and basic usage, move on to arrays of function pointers and the callback pattern, and finally look at the comfortable improvements C++ has made in this area.

> **Learning Objectives**
>
> - After completing this chapter, you will be able to:
> - [ ] Understand function pointer declaration syntax and use it correctly.
> - [ ] Use `typedef` to simplify complex function pointer types.
> - [ ] Implement a callback-based sorting interface similar to `qsort`.
> - [ ] Build a simple event dispatch system.
> - [ ] Understand the corresponding relationships in C++ regarding `std::function`, lambdas, and function objects.

## Environment Setup

All code in this chapter has been verified in the following environment:

- **Operating System**: Linux (Ubuntu 22.04+) / WSL2 / macOS
- **Compiler**: GCC 11+ (Confirm version via `gcc --version`)
- **Compiler Flags**: `gcc -Wall -Wextra -std=c11` (Enable warnings, specify C11 standard)
- **Verification**: All code can be compiled and run directly.

## Step 1 — Using Functions as Data

In C, a function compiles into a segment of machine instructions residing in the code section of memory. Since it resides in memory, it has an address—the function name itself (when not followed by invocation parentheses) is a pointer to this address. We can store this address and use it to invoke the function when needed.

### Learn to Declare Function Pointers First

The declaration syntax for function pointers is notoriously one of C's "anti-human" designs. Let's grit our teeth and look at it:

```cpp
// Declaration: ptr is a pointer to a function taking two ints and returning an int
int (*ptr)(int, int);
```

Let's break down this declaration: `ptr` is a pointer (because `*ptr` is enclosed in parentheses). It points to a function that accepts two `int` parameters and returns an `int`. Those parentheses cannot be omitted—if you write `int *ptr(int, int)`, the compiler interprets it as "a function named `ptr` that returns an `int pointer`," which is completely different.

> ⚠️ **Warning**: When declaring a function pointer, the parentheses around `(*ptr)` **must never be omitted**. Omitting them turns it into a declaration of a function returning a pointer. The compiler might not error, but the behavior will be completely different. This is one of the most common mistakes for newcomers.

Once we have the pointer, assignment and invocation are natural:

```cpp
#include <stdio.h>

int add(int a, int b) {
    return a + b;
}

int main() {
    // ptr points to the function 'add'
    int (*ptr)(int, int) = add;

    // Call the function via the pointer
    int result = ptr(10, 20);
    printf("Result: %d\n", result);

    return 0;
}
```

Output:

```text
Result: 30
```

In most contexts, a function name implicitly converts to a function pointer, just like an array name "decays" into a pointer to its first element. Therefore, `add` does not need the address-of operator `&`. When calling, `ptr(10, 20)` and `(*ptr)(10, 20)` are completely equivalent—the C standard states that function pointers are automatically dereferenced.

### Use `typedef` to Make Declarations Readable

The syntax for declaring function pointers is unfriendly. Once types get complex or need to be used in multiple places, a screen full of `int (*)(int, int)` is torture. `typedef` is our savior—it doesn't create a new type but gives an alias to an existing one:

```cpp
// Define an alias named 'Operation' for 'int (*)(int, int)'
typedef int (*Operation)(int, int);

int add(int a, int b) { return a + b; }
int sub(int a, int b) { return a - b; }

int main() {
    // Now the declaration is much cleaner
    Operation op = add;
    printf("10 + 20 = %d\n", op(10, 20));

    op = sub;
    printf("10 - 20 = %d\n", op(10, 20));

    return 0;
}
```

It is highly recommended to use `typedef` to manage function pointers whenever they appear in a project. Especially in API design for callback interfaces, `typedef` not only simplifies writing function signatures but also improves the self-documenting nature of header files.

## Step 2 — Batch Dispatching with Arrays of Function Pointers

Function pointers can do more than just store a single function address—by stuffing multiple function pointers into an array, we can use an index to select which function to invoke. This pattern is very useful in scenarios like command dispatching or state machine jump tables:

```cpp
#include <stdio.h>

int add(int a, int b) { return a + b; }
int sub(int a, int b) { return a - b; }
int mul(int a, int b) { return a * b; }
int div(int a, int b) { return a / b; }

// Array of function pointers
int (*operations[])(int, int) = { add, sub, mul, div };

int main() {
    int a = 10, b = 5;

    // Iterate through the operation table
    for (int i = 0; i < 4; i++) {
        int result = operations[i](a, b);
        printf("Operation %d result: %d\n", i, result);
    }

    return 0;
}
```

Output:

```text
Operation 0 result: 15
Operation 1 result: 5
Operation 2 result: 50
Operation 3 result: 2
```

This "operation table" pattern is common in embedded firmware. For example, if you have a set of serial commands, each corresponding to a handler function, you can index these function pointers by command ID. When a command is received, dispatching is done in a single line: `handlers[cmd_id](data)`.

> ⚠️ **Warning**: When using an array of function pointers for dispatching, always check if the index is out of bounds. If `cmd_id` exceeds the array range, you will access either a garbage address or `NULL`—calling it directly will cause a segmentation fault.

## Step 3 — Master the Callback Pattern

Where function pointers truly shine is in **callbacks**. The core idea of a callback is simple: I pass you a function's address, and you call it on my behalf at the appropriate time. In plain English, it means "call me back"—the caller does not execute a piece of logic directly, but instead "registers" this logic with the callee, who triggers it when needed.

### Understanding Callbacks via `qsort`

The C standard library's `qsort` function is a textbook example of the callback pattern:

```cpp
#include <stdio.h>
#include <stdlib.h>

// Comparison function: returns <0, 0, or >0
int compare_ints(const void *a, const void *b) {
    int arg1 = *(const int *)a;
    int arg2 = *(const int *)b;
    return (arg1 > arg2) - (arg1 < arg2);
}

int main() {
    int data[] = { 5, 2, 9, 1, 5, 6 };
    int n = sizeof(data) / sizeof(data[0]);

    // Pass the function pointer to qsort
    qsort(data, n, sizeof(int), compare_ints);

    for (int i = 0; i < n; i++)
        printf("%d ", data[i]);
    printf("\n");

    return 0;
}
```

Output:

```text
1 2 5 5 6 9
```

The first three parameters are the array start address, the number of elements, and the size of each element. The last parameter is a pointer to a comparison function—whenever `qsort` needs to compare two elements during the sorting process, it calls this function.

```cpp
int compare_desc(const void *a, const void *b) {
    return compare_ints(b, a); // Reverse order
}

// ... inside main ...
qsort(data, n, sizeof(int), compare_desc);
```

Output:

```text
9 6 5 5 2 1
```

The sorting logic itself (the implementation of `qsort`) remains completely unchanged. We simply swapped the comparison function, and the sorting result is completely different. This is the power of callbacks—**decoupling algorithms from strategies**.

> ⚠️ **Warning**: `qsort`'s comparison function receives `const void*`. The return value follows the convention: "left less than right returns negative, equal returns 0, left greater than right returns positive." If you write the comparison logic backwards, the result will be unsorted—and there will be no compile-time warnings.

## Step 4 — Build an Event Dispatch System

Let's combine function pointers, `typedef`, and arrays of function pointers to build a simple event dispatch system:

```cpp
#include <stdio.h>
#include <stdbool.h>

// Define callback type: event ID and user data
typedef void (*EventHandler)(int event_id, void *user_data);

// Event handler table
#define MAX_EVENTS 10
typedef struct {
    int id;
    EventHandler callback;
    void *user_data;
} EventEntry;

EventEntry event_table[MAX_EVENTS];
int event_count = 0;

// Register an event
void subscribe(int id, EventHandler handler, void *user_data) {
    if (event_count < MAX_EVENTS) {
        event_table[event_count].id = id;
        event_table[event_count].callback = handler;
        event_table[event_count].user_data = user_data;
        event_count++;
    }
}

// Trigger an event
void publish(int id) {
    for (int i = 0; i < event_count; i++) {
        if (event_table[i].id == id && event_table[i].callback != NULL) {
            event_table[i].callback(id, event_table[i].user_data);
        }
    }
}

// --- User Code ---

void on_led_on(int event_id, void *user_data) {
    printf("LED ON event triggered! User data: %d\n", *(int*)user_data);
}

void on_led_off(int event_id, void *user_data) {
    printf("LED OFF event triggered!\n");
}

int main() {
    int context = 42;

    subscribe(1, on_led_on, &context);
    subscribe(2, on_led_off, NULL);

    printf("Publishing event 1...\n");
    publish(1);

    printf("Publishing event 2...\n");
    publish(2);

    return 0;
}
```

This is a minimal viable event system. `void* user_data` acts as the "universal glue" here—whatever extra state information the callback needs, the caller passes it in via this `void*` pointer. This design is ubiquitous in embedded SDKs. For example, the callback registration interfaces in the STM32 HAL library are essentially this pattern.

## C++ Connection

C++ has made multi-level improvements in this direction, from basic function objects to modern lambdas and `std::function`.

**Function Objects (Functors)**: Overload `operator()` for a class so its instances can be called like functions. Compared to C's function pointers, the biggest advantage of function objects is that they can carry state.

**Lambda Expressions** (C++11): Anonymous function objects defined inline at the call site, supporting capture of external variables (closures). This is impossible to achieve in the world of C function pointers.

**std::function** (C++11): A generic, type-safe function wrapper that can hold any callable target: function pointers, function objects, lambdas, etc. It unifies the interface of all callable objects.

**Template Strategy Pattern**: Strategies are determined at compile time, resulting in zero runtime overhead, but increasing compilation time.

From C's function pointers to C++'s lambdas and `std::function`, the core idea is consistent—parameterizing "behavior". C achieved the most basic version with function pointers, while C++ added type safety, closures, and a unified callable object interface on top of that.

## Summary

Function pointers are the core mechanism for implementing callbacks and the strategy pattern in C. The declaration syntax is indeed unfriendly, but once managed with `typedef`, they are very practical. Arrays of function pointers enable table-driven dispatch logic. The callback pattern is clearly illustrated through the classic case of `qsort`—the algorithm framework and specific strategy are decoupled via function pointers. The event dispatch system is a direct application of callbacks in event-driven programming.

### Key Takeaways

- [ ] Function names implicitly convert to function pointers in most contexts.
- [ ] Parentheses in declaration syntax cannot be omitted: `int (*ptr)(int)` vs `int *ptr(int)`.
- [ ] `typedef` is the best practice for managing complex function pointer types.
- [ ] Arrays of function pointers can implement table-driven command/state dispatch.
- [ ] The core of callbacks is "algorithm invariant, strategy replaceable."
- [ ] `void*` provides generic capabilities at the cost of type safety; C++ templates and `std::function` solve this issue.

## Exercises

### Exercise 1: Generic Sorting Interface

Following the interface design of `qsort`, implement your own generic insertion sort function. Use it to sort an `int` array (ascending and descending) and a string array (lexicographical order):

```cpp
// TODO: Implement this function
void my_isort(void *base, size_t n, size_t size,
              int (*compar)(const void *, const void *));
```

### Exercise 2: Event Dispatch System Extension

Based on the event dispatch system in this chapter, support registering multiple callbacks for the same event (a callback chain) and support unregistering callbacks. Think about this: what happens if a handler in the chain modifies the linked list structure during execution?

### Exercise 3: Simple Command-Line Calculator

Use an array of function pointers to implement a command-line calculator supporting addition, subtraction, multiplication, division, and modulo operations. Select the corresponding function based on the user-inputted operator.

```cpp
// Hint: Define a function pointer array and index it by operator type
// double (*operations[])(double, double) = { ... };
```

## References

- [Function Pointer Declaration - cppreference](https://en.cppreference.com/w/c/language/pointer)
- [qsort - cppreference](https://en.cppreference.com/w/c/algorithm/qsort)
- [std::function - cppreference](https://en.cppreference.com/w/cpp/utility/functional/function)
- [Lambda Expressions - cppreference](https://en.cppreference.com/w/cpp/language/lambda)
