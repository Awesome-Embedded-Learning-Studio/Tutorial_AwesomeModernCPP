---
chapter: 1
cpp_standard:
- 11
description: Master the declaration and use of function pointers, understand the application
  of the callback function pattern in event-driven programming, and compare C++ lambdas
  and std::function
difficulty: beginner
order: 13
platform: host
prerequisites:
- 07A 指针基础与核心用法
- 07B 指针、数组与 const
- 08A 多级指针与函数参数
reading_time_minutes: 13
tags:
- host
- cpp-modern
- beginner
- 入门
title: Function pointers and callback patterns
---
# Function Pointers and the Callback Pattern

If pointers are the most powerful feature of C, then function pointers are the part of the pointer world most likely to send your blood pressure through the roof. But honestly, once you grasp them, you will find that they are one of the few mechanisms in C that let you write code "so flexible it doesn't feel like C"—callbacks, event-driven design, the strategy pattern; these concepts sound like they belong to high-level languages, but in C, they all rely on function pointers to make them happen.

In previous tutorials, we systematically covered various uses of pointers. Now, we will tackle the tough nut that is function pointers. We will start with declaration syntax and basic usage, move on to arrays of function pointers and the callback pattern, and finally look at the comfortable improvements C++ has made in this direction.

> **Learning Objectives**
>
> - After completing this chapter, you will be able to:
> - [ ] Understand function pointer declaration syntax and use it correctly
> - [ ] Use typedef to simplify complex function pointer types
> - [ ] Implement a callback-based sorting interface similar to qsort
> - [ ] Build a simple event dispatch system
> - [ ] Understand the C++ equivalents using std::function, lambda expressions, and function objects

## Environment Setup

All code in this article has been verified under the following environment:

- **Operating System**: Linux (Ubuntu 22.04+) / WSL2 / macOS
- **Compiler**: GCC 11+ (confirm the version via `gcc --version`)
- **Compiler flags**: `gcc -Wall -Wextra -std=c11` (enables warnings, specifies the C11 standard)
- **Verification**: All code can be compiled and run directly

## Step 1 — Treating Functions as Data

In C, a compiled function is simply a sequence of machine instructions residing in the code segment of memory. Since it lives in memory, it has an address—the function name itself (when not followed by call parentheses) is a pointer to this address. We can store this address and use it to call the function whenever we need to.

### Learning to Declare Function Pointers

The declaration syntax for function pointers is widely considered one of C's most "anti-human" designs. Let's bite the bullet and take a look:

```c
// 假设有一个函数：int add(int a, int b)
// 它的函数指针类型声明如下：
int (*op_ptr)(int, int);
```

Let's break down this declaration: `op_ptr` is a pointer (because `*op_ptr` is enclosed in parentheses), and it points to a function that takes two `int` parameters and returns an `int`. Those parentheses cannot be omitted—if you write `int *op_ptr(int, int)`, the compiler interprets it as "a function named `op_ptr` that returns a `int*`", which is a completely different thing.

> ⚠️ **Pitfall Warning**: When declaring a function pointer, the parentheses around `(*op_ptr)` **must absolutely not be omitted**. Omitting them turns the declaration into a function returning a pointer. The compiler will not raise an error, but the behavior will be completely different. This is one of the most common mistakes beginners make.

Once we have the pointer, assignment and invocation are straightforward:

```c
#include <stdio.h>

int add(int a, int b)
{
    return a + b;
}

int subtract(int a, int b)
{
    return a - b;
}

int main(void)
{
    int (*op_ptr)(int, int) = add;     // 函数名就是地址，不需要 &
    printf("%d\n", op_ptr(10, 5));      // 15

    op_ptr = subtract;                  // 指向另一个函数
    printf("%d\n", op_ptr(10, 5));      // 5

    // 通过指针调用也可以显式解引用，两种写法等价
    printf("%d\n", (*op_ptr)(20, 8));   // 12
    return 0;
}
```

Output:

```text
15
5
12
```

In most contexts, a function name implicitly converts to a function pointer, just as an array name decays into a pointer to its first element, so `op_ptr = add` does not need the address-of operator. When calling, `op_ptr(10, 5)` and `(*op_ptr)(10, 5)` are completely equivalent—the C standard states that function pointers are automatically dereferenced.

### Making Declarations Readable with typedef

The declaration syntax for function pointers is not very friendly. Once the types get complex or you need to use them in multiple places, a screen full of `int (*)(int, int)` is pure torture. This is where `typedef` comes to the rescue—it does not create a new type, it simply gives an alias to an existing one:

```c
// 给"接受两个int、返回int的函数指针"起个别名
typedef int (*BinaryOp)(int, int);

// 现在声明变量就像普通类型一样自然
BinaryOp op = add;
printf("%d\n", op(3, 4));  // 7
```

We strongly recommend using typedef to manage function pointers whenever you encounter them in a project. Especially in the API design of callback interfaces, typedef not only simplifies writing function signatures but also significantly improves the self-documenting nature of header files.

## Step 2 — Batch Dispatch with Arrays of Function Pointers

Function pointers can do more than just store a single function address—by putting multiple function pointers into an array, we can use an index to select which function to call. This pattern is extremely useful in scenarios like command dispatch and state machine jump tables:

```c
#include <stdio.h>

typedef int (*BinaryOp)(int, int);

int add(int a, int b)      { return a + b; }
int subtract(int a, int b) { return a - b; }
int multiply(int a, int b) { return a * b; }
int divide(int a, int b)   { return b != 0 ? a / b : 0; }

int main(void)
{
    BinaryOp operations[] = { add, subtract, multiply, divide };
    const char* op_names[] = { "+", "-", "*", "/" };

    int x = 20, y = 4;
    for (int i = 0; i < 4; i++) {
        printf("%d %s %d = %d\n", x, op_names[i], y, operations[i](x, y));
    }
    return 0;
}
```

Output:

```text
20 + 4 = 24
20 - 4 = 16
20 * 4 = 80
20 / 4 = 5
```

This "operation table" pattern is very common in embedded firmware—for example, if you have a set of serial commands where each command corresponds to a handler function, you can organize these function pointers into an array by command ID. When a command is received, a single `handlers[cmd_id](args)` handles the dispatch.

> ⚠️ **Pitfall Warning**: When using an array of function pointers for dispatch, you must always check whether the index is out of bounds. If `cmd_id` exceeds the array range, you will either access a garbage address or NULL—calling it directly will result in a segmentation fault.

## Step 3 — Mastering the Callback Function Pattern

Where function pointers truly shine is in **callbacks**. The core idea of a callback is simple: I give you the address of a function, and you call it on my behalf at the appropriate time. In plain terms, it means "call back later"—the caller does not directly execute a certain piece of logic, but instead "registers" this logic with the callee, who triggers it when needed.

### Understanding Callbacks through qsort

The `qsort` function from the C standard library is the most classic, textbook-level example of the callback pattern:

```c
void qsort(void* base, size_t nmemb, size_t size,
           int (*compar)(const void*, const void*));
```

The first three parameters are the starting address of the array, the number of elements, and the size of each element. The last parameter is a comparison function pointer—whenever `qsort` needs to compare the relative size of two elements during the sorting process, it calls this function.

```c
#include <stdio.h>
#include <stdlib.h>

int compare_asc(const void* a, const void* b)
{
    int ia = *(const int*)a;
    int ib = *(const int*)b;
    return ia - ib;
}

int main(void)
{
    int numbers[] = { 42, 12, 7, 89, 23, 55, 3 };
    size_t count = sizeof(numbers) / sizeof(numbers[0]);

    qsort(numbers, count, sizeof(int), compare_asc);
    for (size_t i = 0; i < count; i++) {
        printf("%d ", numbers[i]);
    }
    printf("\n");
    return 0;
}
```

Output:

```text
3 7 12 23 42 55 89
```

The sorting logic itself (the implementation of `qsort`) did not change at all; we simply swapped in a different comparison function, and the sorting result was completely different. This is the power of callbacks—**decoupling algorithms from strategies**.

> ⚠️ **Pitfall Warning**: The comparison function of `qsort` receives `const void*`, and its return value follows the convention "return negative if left is less than right, zero if equal, and positive if left is greater than right." If you write the comparison logic backwards, the sorted result will be out of order—and there will not be any compile-time warnings.

## Step 4 — Building an Event Dispatch System

Let's combine the function pointers, typedef, and arrays of function pointers we just learned to build a simple event dispatch system:

```c
#include <stdio.h>

typedef enum {
    kEventButtonPress,
    kEventTimerTick,
    kEventDataReceived,
    kEventCount
} EventType;

typedef void (*EventHandler)(EventType event, void* context);

typedef struct {
    EventHandler handlers[kEventCount];
    void* contexts[kEventCount];
} EventDispatcher;

void dispatcher_init(EventDispatcher* dispatcher)
{
    for (int i = 0; i < kEventCount; i++) {
        dispatcher->handlers[i] = NULL;
        dispatcher->contexts[i] = NULL;
    }
}

void dispatcher_register(EventDispatcher* dispatcher,
                          EventType event,
                          EventHandler handler,
                          void* context)
{
    if (event >= 0 && event < kEventCount) {
        dispatcher->handlers[event] = handler;
        dispatcher->contexts[event] = context;
    }
}

void dispatcher_dispatch(EventDispatcher* dispatcher, EventType event)
{
    if (event >= 0 && event < kEventCount) {
        EventHandler handler = dispatcher->handlers[event];
        if (handler != NULL) {
            handler(event, dispatcher->contexts[event]);
        }
    }
}
```

This is a minimal viable event system. `void* context` acts as the "universal glue" here—whatever additional state information the callback function needs, the caller passes it in via the `context` pointer. This design is ubiquitous in embedded SDKs; for example, the callback registration interfaces in the STM32 HAL library are essentially built on this exact pattern.

## Bridging to C++

C++ has made multi-layered improvements in this direction, ranging from basic function objects to modern lambda expressions and `std::function`.

**Function Objects (Functors)**: Overload `operator()` for a class so that its instances can be called like functions. Compared to C function pointers, the biggest advantage of function objects is that they can carry state.

**Lambda Expressions** (C++11): Anonymous function objects defined inline at the call site, supporting the capture of external variables (closures). This is impossible in the world of C function pointers.

**std::function** (C++11): A generic, type-safe function wrapper that can hold any callable target, including function pointers, function objects, and lambdas. It unifies the interface for all callable objects.

**Template Strategy Pattern**: Determines the strategy at compile time with zero runtime overhead, but increases compilation time.

From C function pointers to C++ lambdas and `std::function`, the core idea runs in a straight line—parameterizing "behavior". C achieved the most basic version with function pointers, while C++ added type safety, closures, and a unified callable object interface on top of that foundation.

## Summary

Function pointers are the core mechanism for implementing callbacks and the strategy pattern in C. The declaration syntax is admittedly unfriendly, but once managed with `typedef`, they become highly practical. Arrays of function pointers enable table-driven dispatch logic, and the callback pattern is crystal clear through the classic example of `qsort`—the algorithm framework and the specific strategy are decoupled via function pointers. The event dispatch system is the direct application of callbacks in event-driven programming.

### Key Takeaways

- [ ] A function name implicitly converts to a function pointer in most contexts
- [ ] Parentheses in the declaration syntax cannot be omitted: `int (*p)(int)` rather than `int *p(int)`
- [ ] `typedef` is the best practice for managing complex function pointer types
- [ ] Arrays of function pointers can implement table-driven command/state dispatch
- [ ] The core of callbacks is "algorithm remains unchanged, strategy is replaceable"
- [ ] `void*` provides genericity but sacrifices type safety; C++ templates and `std::function` solve this problem

## Exercises

### Exercise 1: Generic Sorting Interface

Following the interface design of `qsort`, implement your own generic insertion sort function. Use it to sort an `int` array (in ascending and descending order) and a string array (in lexicographical order):

```c
void insertion_sort(void* base, size_t nmemb, size_t size,
                    int (*compar)(const void*, const void*));
```

### Exercise 2: Event Dispatch System Extension

Based on the event dispatch system from this article, add support for registering multiple callbacks for the same event (a callback chain) and support for unregistering callbacks. Think about this: what happens if a handler in the callback chain modifies the linked list structure while it is being executed?

### Exercise 3: Simple Command-Line Calculator

Use an array of function pointers to implement a command-line calculator that supports addition, subtraction, multiplication, division, and modulo operations. The user selects the corresponding function by inputting an operator.

```c
typedef int (*BinaryOp)(int, int);
// 请自行设计映射表和主循环
```

## References

- [Function pointer declarations - cppreference](https://en.cppreference.com/w/c/language/pointer)
- [qsort - cppreference](https://en.cppreference.com/w/c/algorithm/qsort)
- [std::function - cppreference](https://en.cppreference.com/w/cpp/utility/functional/function)
- [Lambda expressions - cppreference](https://en.cppreference.com/w/cpp/language/lambda)
