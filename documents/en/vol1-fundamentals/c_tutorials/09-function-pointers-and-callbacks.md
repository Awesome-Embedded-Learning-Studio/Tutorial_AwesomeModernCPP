---
chapter: 1
cpp_standard:
- 11
description: Master the declaration and use of function pointers, understand how the callback pattern serves event-driven programming, and compare C++ lambdas and std::function
difficulty: beginner
order: 13
platform: host
prerequisites:
- 'Pointer Basics: The World of Addresses'
- Pointers, Arrays, const, and Null Pointers
- Multilevel Pointers and Reading Declarations
reading_time_minutes: 29
tags:
- host
- cpp-modern
- beginner
- 入门
title: Function Pointers and the Callback Pattern
translation:
  source: documents/vol1-fundamentals/c_tutorials/09-function-pointers-and-callbacks.md
  source_hash: eb7d469eb0f589f99637195c81f9514314ce9d0d306c775e183eebda8ac9e04f
  translated_at: '2026-09-25T13:09:41+00:00'
  engine: anthropic
  token_count: 10500
---
# Function Pointers and the Callback Pattern

If pointers are C's most powerful feature, then function pointers are the part of the pointer world most likely to send your blood pressure through the roof. But honestly, once you've figured them out, you'll find they're one of the few mechanisms in C that let you write code so flexible it barely feels like C—callbacks, event-driven programming, the strategy pattern. These things that sound like high-level-language luxuries are carried entirely on the shoulders of function pointers in C.

We systematically walked through the various uses of pointers in earlier tutorials; this one is devoted to cracking the tough nut of function pointers. We'll start with declarations and basic usage, move on to arrays of function pointers and the callback pattern, and finish by looking at the improvements C++ has made in this direction to make life more comfortable.

## Step 1 — Treating Functions as Data

In C, a compiled function is a stretch of machine instructions that resides in the code segment of memory. And since it lives in memory, it has an address—the function name itself (when not accompanied by call parentheses) is a pointer to that address. We can store this address away and call the function through it whenever we need to.

### First, Learn to Declare a Function Pointer

The declaration syntax for function pointers is widely acknowledged to be one of C's most user-hostile designs. Let's grit our teeth and take a look:

```c
// Suppose we have a function: int add(int a, int b)
// Its function pointer type is declared as follows:
int (*op_ptr)(int, int);
```

Let's take the declaration apart: `op_ptr` is a pointer (because `*op_ptr` is wrapped in parentheses), and it points to a function that takes two `int` parameters and returns `int`. Those parentheses cannot be dropped—if you write `int *op_ptr(int, int)` instead, the compiler reads it as "a function named `op_ptr` that returns `int*`", which is not the same thing at all.

When declaring a function pointer, the parentheses around `(*op_ptr)` are **absolutely non-negotiable**. Leave them out and you've declared a function that returns a pointer; the compiler won't report an error, but the behavior is completely different. This is one of the mistakes beginners make most often.

Once you have the pointer in hand, assignment and calls come naturally:

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
    int (*op_ptr)(int, int) = add;     // The function name is the address; no & needed
    printf("%d\n", op_ptr(10, 5));      // 15

    op_ptr = subtract;                  // Point to another function
    printf("%d\n", op_ptr(10, 5));      // 5

    // Calling through the pointer can also dereference explicitly; both forms are equivalent
    printf("%d\n", (*op_ptr)(20, 8));   // 12
    return 0;
}
```

The output:

```text
15
5
12
```

In most contexts a function name implicitly converts to a function pointer, just as an array name decays into a pointer to its first element—which is why `op_ptr = add` needs no address-of operator. When calling, `op_ptr(10, 5)` and `(*op_ptr)(10, 5)` are fully equivalent—the C standard says function pointers are dereferenced automatically.

### Making Declarations Readable with typedef

Function pointer declaration syntax is not exactly friendly; once the type gets complicated or you need it in several places, a screen full of `int (*)(int, int)` is genuine torture. `typedef` is our savior—it doesn't create a new type, it just gives an existing type an alias:

```c
// Give an alias to "a function pointer taking two ints and returning int"
typedef int (*BinaryOp)(int, int);

// Now declaring a variable feels as natural as with an ordinary type
BinaryOp op = add;
printf("%d\n", op(3, 4));  // 7
```

We strongly recommend managing every function pointer you encounter in a project with a typedef. Especially in API design for callback interfaces, a typedef both simplifies writing the function signature and makes the header file considerably more self-documenting.

## Step 2 — Batch Dispatch with Arrays of Function Pointers

Function pointers can do more than store one function's address—pack several of them into an array, and you can use an index to choose which function gets called. This pattern is extremely practical in scenarios like command dispatch and state-machine jump tables:

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

The output:

```text
20 + 4 = 24
20 - 4 = 16
20 * 4 = 80
20 / 4 = 5
```

This "operation table" pattern is very common in embedded firmware—say you have a set of serial-port commands, each with its own handler function. Index those function pointers by command ID, and once a command arrives, `handlers[cmd_id](args)` settles the dispatch in a single line.

When dispatching through an array of function pointers, always check that the index is in bounds. If `cmd_id` exceeds the array's range, what you read is either a garbage address or NULL—calling it outright gets you a segmentation fault.

## Step 3 — Mastering the Callback Pattern

The place function pointers truly shine is the **callback**. The core idea of a callback is simple: I hand you the address of a function, and you call it on my behalf at the right moment. Put plainly, it's "call back later"—the caller doesn't execute some piece of logic directly; instead, it "registers" that logic with the callee, and the callee comes back around to trigger it when needed.

### Learning Callbacks from qsort

The C standard library's `qsort` function is the classic textbook case of the callback pattern:

```c
void qsort(void* base, size_t nmemb, size_t size,
           int (*compar)(const void*, const void*));
```

The first three parameters are the array's base address, the number of elements, and the size of each element. The last parameter is a comparison function pointer—whenever `qsort` internally needs to compare two elements' ordering during the sort, it calls this function.

```c
#include <stdio.h>
#include <stdlib.h>

int compare_asc(const void* a, const void* b)
{
    int ia = *(const int*)a;
    int ib = *(const int*)b;
    return (ia > ib) - (ia < ib);
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

The output:

```text
3 7 12 23 42 55 89
```

The sorting logic itself (qsort's implementation) hasn't changed one bit—we merely swapped in a different comparison function, and the sorting result came out completely different. That is the power of callbacks: **decoupling the algorithm from the policy**.

qsort's comparison function receives `const void*`, and its return value follows the convention "negative when the left is smaller than the right, 0 when they are equal, positive when the left is greater". If you write the comparison logic backwards, the sorted result is a scrambled order—and you get no compile-time warning at all.

## Step 4 — Building an Event Dispatch System

Let's combine what we've learned so far—function pointers, typedef, and arrays of function pointers—to put together a simple event dispatch system:

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

That is a minimal viable event system. The `void* context` here is the "universal glue"—whatever extra state a callback function needs, the caller passes in through the `context` pointer. This design is everywhere in embedded SDKs; for example, the callback registration interfaces in the STM32 HAL library are essentially this very pattern.

## Bridging to C++

C++ has made improvements on this front at multiple levels, from the most basic function objects up to modern lambdas and `std::function`.

**Function objects (functors)**: overload `operator()` for a class so its instances can be called like functions. Compared with C's function pointers, a functor's biggest advantage is that it can carry state.

**Lambda expressions** (C++11): anonymous function objects defined in place at the call site, with support for capturing external variables (closures). This is impossible in the world of C function pointers.

**std::function** (C++11): a general-purpose, type-safe function wrapper that can hold a function pointer, a functor, a lambda—any callable target. It unifies the interface of all callable objects.

**Template-based strategy pattern**: pins the policy down at compile time with zero runtime overhead, at the cost of longer compile times.

From C's function pointers to C++'s lambdas and `std::function`, the core idea runs in one unbroken line—parameterizing "behavior". C delivered the most basic version with function pointers; C++ added type safety, closures, and a unified callable-object interface on top of it.

## Exercises

### Exercise 1: A Generic Sorting Interface

**Difficulty: Intermediate** · Comparison policy via function pointers

Following the interface design of `qsort`, implement your own generic insertion sort function, and use it to sort an `int` array (ascending and descending) as well as an array of strings (in dictionary order):

```c
void insertion_sort(void* base, size_t nmemb, size_t size,
                    int (*compar)(const void*, const void*));
```

::: details Reference solution

Here we follow qsort's comparator convention: a negative return means the left element should be placed before the right one, 0 means the two are equivalent, and a positive return means the left element should be placed after the right one. The insertion sort itself only honors this convention—whether the final order is ascending, descending, or string-lexicographic is decided entirely by the callback.

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int compare_int_ascending(const void* a, const void* b)
{
    const int ia = *(const int*)a;
    const int ib = *(const int*)b;
    return (ia > ib) - (ia < ib);
}

int compare_int_descending(const void* a, const void* b)
{
    const int ia = *(const int*)a;
    const int ib = *(const int*)b;
    return (ib > ia) - (ib < ia);
}

int compare_cstrings(const void* a, const void* b)
{
    const char* const lhs = *(const char* const*)a;
    const char* const rhs = *(const char* const*)b;
    return strcmp(lhs, rhs);
}

void insertion_sort(void* base, size_t nmemb, size_t size,
                    int (*compar)(const void*, const void*))
{
    if (base == NULL || compar == NULL || nmemb < 2 || size == 0) {
        return;
    }

    // unsigned char* moves byte by byte, which lets the same algorithm handle any element type.
    unsigned char* data = (unsigned char*)base;
    unsigned char* current = malloc(size);
    if (current == NULL) {
        return;
    }

    for (size_t i = 1; i < nmemb; ++i) {
        size_t j = i;
        memcpy(current, data + i * size, size);

        while (j > 0 && compar(data + (j - 1) * size, current) > 0) {
            --j;
        }

        if (j != i) {
            // The source and destination ranges overlap, so memmove is required here.
            memmove(data + (j + 1) * size, data + j * size, (i - j) * size);
            memcpy(data + j * size, current, size);
        }
    }

    free(current);
}

int main(void)
{
    int ascending_numbers[] = {5, 2, 9, 1, 5, 6};
    int descending_numbers[] = {5, 2, 9, 1, 5, 6};
    const char* words[] = {"pear", "apple", "orange", "banana", "grape"};
    const size_t number_count = sizeof(ascending_numbers) / sizeof(ascending_numbers[0]);
    const size_t word_count = sizeof(words) / sizeof(words[0]);

    insertion_sort(ascending_numbers, number_count, sizeof(ascending_numbers[0]),
                   compare_int_ascending);
    insertion_sort(descending_numbers, number_count, sizeof(descending_numbers[0]),
                   compare_int_descending);
    insertion_sort(words, word_count, sizeof(words[0]), compare_cstrings);

    printf("int 升序：");
    for (size_t i = 0; i < number_count; ++i) {
        printf("%d ", ascending_numbers[i]);
    }

    printf("\nint 降序：");
    for (size_t i = 0; i < number_count; ++i) {
        printf("%d ", descending_numbers[i]);
    }

    printf("\n字符串字典序：");
    for (size_t i = 0; i < word_count; ++i) {
        printf("%s ", words[i]);
    }
    putchar('\n');

    return 0;
}
```

The output:

```text
int 升序：1 2 5 5 6 9
int 降序：9 6 5 5 2 1
字符串字典序：apple banana grape orange pear
```

Just like the `qsort` example earlier, we didn't write `ia - ib` here, because subtracting two `int`s that are far apart can overflow a signed integer. `(ia > ib) - (ia < ib)` only ever produces `-1`, `0`, or `1`; it satisfies the comparator convention just as well without planting that trap. Another engineering trade-off: the interface given by the exercise returns `void`, so when the temporary buffer allocation fails, the best we can do is leave the original array untouched and return; if this were a proper library interface, we would usually return a status code and hand the failure explicitly to the caller.

:::

### Exercise 2: Retry with a Maximum Attempt Count

**Difficulty: Intermediate** · Conditional callback via function pointers

Implement a `retry_until`: call the `check` function pointer repeatedly until it returns non-zero (success) or the maximum number of attempts is reached.

```c
/// @brief Call check repeatedly until it succeeds or the attempt limit is reached
/// @param check The condition function; a non-zero return means success
/// @param max_attempts The maximum number of attempts
/// @return On success, returns which attempt succeeded (counting from 1); returns -1 when all attempts fail
int retry_until(int (*check)(void), int max_attempts);
```

Hint: here is how `check` can simulate "a peripheral that only becomes ready on the third try":

```c
int device_ready(void) {
    static int tried = 0;       // The static local variable from Chapter 06 comes in handy right here
    return ++tried >= 3;
}
```

Think about it: what does this style—passing the "condition" in as a function pointer—have in common with this chapter's `qsort` comparator and event dispatch?

::: details Reference solution

First, let's get `retry_until` running. We prepare two callbacks at once: `device_ready` succeeds on the 3rd check, while `always_fail` never succeeds—conveniently walking both exit paths, "early success" and "hitting the limit", exactly once each.

```c
#include <stddef.h>
#include <stdio.h>

int retry_until(int (*check)(void), int max_attempts)
{
    if (check == NULL || max_attempts <= 0) {
        return -1;
    }

    int attempt = 0;
    while (attempt < max_attempts) {
        ++attempt;
        if (check() != 0) {
            return attempt;
        }
    }

    return -1;
}

int device_ready(void)
{
    static int tried = 0;
    return ++tried >= 3;
}

int always_fail(void)
{
    return 0;
}

int main(void)
{
    const int ready_attempt = retry_until(device_ready, 5);
    const int failed_attempt = retry_until(always_fail, 2);

    printf("device_ready：第 %d 次检查成功\n", ready_attempt);
    printf("always_fail：%d\n", failed_attempt);
    return 0;
}
```

The output:

```text
device_ready：第 3 次检查成功
always_fail：-1
```

What they share is this: **the framework owns the flow, the callback owns the policy**. `qsort` decides when elements are compared but leaves "what counts as bigger" to the comparator; `retry_until` decides how many checks happen at most but leaves "what counts as success" to `check`; the event dispatcher decides when to respond to an event but leaves "what to do when the event arrives" to the handler. None of the three needs to know the concrete implementation inside the callback—all it takes is an agreed function signature and return-value semantics. That is what it means to decouple the algorithmic framework from replaceable behavior.

The `device_ready` in the example uses a `static` local variable to simulate peripheral state, and its value does not reset automatically after `retry_until` returns. If you test with the same callback again, it will succeed outright on the 1st check. Real projects usually pass in independently managed state through a `void* context`, rather than hiding test state away inside a function.

:::

### Exercise 3: A Simple Command-Line Calculator

**Difficulty: Intermediate** · Table-driven dispatch with an array of function pointers

Use an array of function pointers to implement a command-line calculator that supports addition, subtraction, multiplication, division, and modulo, selecting the corresponding function through the operator the user enters.

```c
typedef int (*BinaryOp)(int, int);
// Design the mapping table and the main loop yourself
```

::: details Reference solution

```c
#include <limits.h>
#include <stddef.h>
#include <stdio.h>

typedef int (*BinaryOp)(int, int);

typedef struct
{
    char symbol;
    BinaryOp function;
} Operation;

static int add(int left, int right)
{
    return left + right;
}

static int subtract(int left, int right)
{
    return left - right;
}

static int multiply(int left, int right)
{
    return left * right;
}

static int divide(int left, int right)
{
    return left / right;
}

static int modulo(int left, int right)
{
    return left % right;
}

static const Operation operations[] = {
    {'+', add},
    {'-', subtract},
    {'*', multiply},
    {'/', divide},
    {'%', modulo},
};

// Look up the operation function for a given symbol
static const Operation *find_operation(char symbol)
{
    const size_t operation_count =
        sizeof(operations) / sizeof(operations[0]);
    size_t i;

    for (i = 0; i < operation_count; ++i)
    {
        if (operations[i].symbol == symbol)
        {
            return &operations[i];
        }
    }

    return NULL;
}

int main(void)
{
    char line[128];

    puts("整数计算器：+  -  *  /  %");
    puts("输入示例：12 + 3；输入 q 退出。");

    for (;;)
    {
        const Operation *operation;
        int left;
        int right;
        int result;
        char symbol;
        char trailing;

        printf("> ");
        // "> " has no newline; flushing immediately ensures the user sees the prompt before waiting for input
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL)
        {
            putchar('\n');
            break;
        }

        if (sscanf(line, " %c", &symbol) == 1 &&
            (symbol == 'q' || symbol == 'Q'))
        {
            putchar('\n');
            break;
        }

        if (sscanf(line, " %d %c %d %c", &left, &symbol, &right,
                   &trailing) != 3)
        {
            puts("输入无效。格式：整数 运算符 整数");
            continue;
        }

        operation = find_operation(symbol);
        if (operation == NULL)
        {
            printf("未知运算符：%c\n", symbol);
            continue;
        }

        if ((symbol == '/' || symbol == '%') && right == 0)
        {
            puts("错误：不允许除以零。");
            continue;
        }

        if ((symbol == '/' || symbol == '%') && left == INT_MIN && right == -1)
        {
            puts("错误：结果超出 int 范围。");
            continue;
        }

        result = operation->function(left, right);
        printf("结果：%d\n", result);
    }

    return 0;
}

```

The output:

```text
整数计算器：+  -  *  /  %
输入示例：12 + 3；输入 q 退出。
> 结果：15
> 错误：不允许除以零。
>
```

:::

### Exercise 4: Extending the Event Dispatch System (Challenge, Optional)

**Difficulty: Challenge** · Optional; requires designing a callback container—beginners can skip it

Building on this chapter's array-based event dispatch system, extend it so that the same event type can register multiple callbacks under different event names, and so that callbacks can be unregistered by `type + name`. Hint: no linked list needed—a **two-dimensional array of function pointers** can serve as the callback container; registering the same `type + name` pair again replaces the original callback, and unregistering clears the corresponding slot.

::: details Reference solution

event.h

```c
#pragma once
#include <stdint.h>
typedef enum {
    ERR_OK = 0, // success
    ERR_NULL = -1, // null pointer
    ERR_INVALID_ARGUMENT = -2, // invalid argument
    ERR_FULL = -3, // queue is full
    ERR_NOT_FOUND = -4 // not found
} err_t;
typedef struct {
    void (*fn)(void *arg);
    void *arg;
} Callback_t;

enum EventType
{
    EVENT_TYPE_1 = 0,
    EVENT_TYPE_2,
    EVENT_TYPE_3,
    EVENT_TYPE_Num,
};
enum EventName
{
    EVENT_NAME_1 = 0,
    EVENT_NAME_2,
    EVENT_NAME_3,
    EVENT_NAME_Num,
};
```

event.c

```c
#include <stddef.h>

#include "event.h"

Callback_t callback_list[EVENT_TYPE_Num][EVENT_NAME_Num] = {0};

// Callback registration
void register_callback(enum EventType type, enum EventName name, void (*fn)(void *arg), void *arg)
{
    if ((unsigned)type >= EVENT_TYPE_Num || (unsigned)name >= EVENT_NAME_Num)
    {
        return;
    }

    callback_list[type][name].fn = fn;
    callback_list[type][name].arg = arg;
}
// Run a callback
void run_callback(enum EventType type, enum EventName name)
{
    if ((unsigned)type >= EVENT_TYPE_Num || (unsigned)name >= EVENT_NAME_Num)
    {
        return;
    }

    Callback_t *callback = &callback_list[type][name];
    if (callback->fn != NULL)
    {
        callback->fn(callback->arg);
    }
}
// Unregister a callback
void unregister_callback(enum EventType type, enum EventName name)
{
    if ((unsigned)type >= EVENT_TYPE_Num || (unsigned)name >= EVENT_NAME_Num)
    {
        return;
    }

    callback_list[type][name].fn = NULL;
    callback_list[type][name].arg = NULL;
}
```

main.c

```c
#include <stdio.h>

#include "event.h"

/* The callback interface is implemented in event.c. */
void register_callback(enum EventType type, enum EventName name,
                       void (*fn)(void *arg), void *arg);
void run_callback(enum EventType type, enum EventName name);
void unregister_callback(enum EventType type, enum EventName name);

static void on_event_name_1(void *arg)
{
    const char *message = (const char *)arg;

    printf("EVENT_NAME_1 回调：%s\n", message);
}

static void on_event_name_2(void *arg)
{
    const char *message = (const char *)arg;

    printf("EVENT_NAME_2 回调：%s\n", message);
}

static void on_event_name_1_replaced(void *arg)
{
    const char *message = (const char *)arg;

    printf("EVENT_NAME_1 替换回调：%s\n", message);
}

static void callback_demo(void)
{
    const enum EventType type = EVENT_TYPE_1;

    puts("回调演示（同一类型使用不同回调）：");

    /* One event type can bind different callbacks through different event names. */
    register_callback(type, EVENT_NAME_1, on_event_name_1,
                      "已为第一个事件名称注册");
    register_callback(type, EVENT_NAME_2, on_event_name_2,
                      "已为第二个事件名称注册");

    printf("运行 run_callback(EVENT_TYPE_1, EVENT_NAME_1) -> ");
    run_callback(type, EVENT_NAME_1);
    printf("运行 run_callback(EVENT_TYPE_1, EVENT_NAME_2) -> ");
    run_callback(type, EVENT_NAME_2);

    /* Registering the same type and name again replaces the callback in that slot. */
    register_callback(type, EVENT_NAME_1, on_event_name_1_replaced,
                      "原始回调已替换");
    printf("重新注册 EVENT_NAME_1 后 -> ");
    run_callback(type, EVENT_NAME_1);

    unregister_callback(type, EVENT_NAME_1);
    puts("注销 EVENT_NAME_1 后 ->（未注册回调）");
    printf("EVENT_NAME_2 仍可用 -> ");
    run_callback(type, EVENT_NAME_2);
}

int main(void)
{
    callback_demo();
    return 0;
}

```

The output:

```text
回调演示（同一类型使用不同回调）：
运行 run_callback(EVENT_TYPE_1, EVENT_NAME_1) -> EVENT_NAME_1 回调：已为第一个事件名称注册
运行 run_callback(EVENT_TYPE_1, EVENT_NAME_2) -> EVENT_NAME_2 回调：已为第二个事件名称注册
重新注册 EVENT_NAME_1 后 -> EVENT_NAME_1 替换回调：原始回调已替换
注销 EVENT_NAME_1 后 ->（未注册回调）
EVENT_NAME_2 仍可用 -> EVENT_NAME_2 回调：已为第二个事件名称注册
```

Here the two-dimensional array uses `type` and `name` together as the callback's key. Under the same `type`, `EVENT_NAME_1` and `EVENT_NAME_2` correspond to different slots that don't affect each other; registering the exact same `type + name` again replaces the callback originally stored in that slot.

Also note the bounds-checking style in the answer: the parameters are first converted to `unsigned`, then compared against the upper bound. If the caller passes a negative value (for example `(enum EventType)-1`), the conversion produces a very large unsigned number that is equally shut out of bounds—so there's no need to write checks like `type < 0`, and it also avoids the "comparison with 0" compiler warning that arises when an enum's underlying type is unsigned.

Think about it: after unregistering `EVENT_TYPE_1 + EVENT_NAME_1`, why can `EVENT_TYPE_1 + EVENT_NAME_2` still be dispatched normally?

The answer is that these two callbacks sit in different slots of the two-dimensional array: the former corresponds to `callback_list[EVENT_TYPE_1][EVENT_NAME_1]`, the latter to `callback_list[EVENT_TYPE_1][EVENT_NAME_2]`. `unregister_callback` only clears the `fn` and `arg` in the specified slot; it doesn't modify the slots of other `name`s under the same `type`, so `EVENT_NAME_2`'s callback can still be dispatched normally.

> **Food for thought**: if, while iterating over the callback array, one callback goes off and unregisters another callback, what goes wrong? It's the same trap as deleting elements from an array while iterating it.

:::

## References

- [cppreference: Function pointer declaration](https://en.cppreference.com/w/c/language/pointer)
- [cppreference: qsort](https://en.cppreference.com/w/c/algorithm/qsort)
- [cppreference: std::function](https://en.cppreference.com/w/cpp/utility/functional/function)
- [cppreference: Lambda expressions](https://en.cppreference.com/w/cpp/language/lambda)
