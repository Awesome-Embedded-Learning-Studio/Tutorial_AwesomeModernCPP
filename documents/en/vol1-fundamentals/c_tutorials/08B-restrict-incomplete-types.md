---
chapter: 1
cpp_standard:
- 11
- 17
description: Understanding the optimization principles of the `restrict` qualifier,
  the use cases of incomplete types and forward declarations, the opaque pointer pattern,
  and using the `->` operator with struct pointers
difficulty: beginner
order: 12
platform: host
prerequisites:
- 多级指针与声明读法
reading_time_minutes: 12
tags:
- host
- cpp-modern
- beginner
- 入门
- 基础
title: '`restrict`, Incomplete Types, and Struct Pointers'
translation:
  source: documents/vol1-fundamentals/c_tutorials/08B-restrict-incomplete-types.md
  source_hash: 5ae618e6616269e796e77210bc518a9f89b5cb8a8bb4dba4507b805404e8f1bc
  translated_at: '2026-04-20T03:25:34.827886+00:00'
  engine: anthropic
  token_count: 1782
---
# restrict, Incomplete Types, and Struct Pointers

In the previous chapter, we covered multi-level pointers and how to read declarations. Now we will look at three relatively independent but highly useful mechanisms: the `restrict` qualifier lets the compiler perform more aggressive optimizations, incomplete types and forward declarations allow us to design interfaces without exposing internal details, and the `->` operator is an everyday tool for working with struct pointers.

These three concepts might seem unrelated, but they are all highly practical in C language engineering—and they all have modern counterparts in C++.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Understand what problem the `restrict` qualifier solves and its usage rules
> - [ ] Use incomplete types and forward declarations to reduce header file dependencies
> - [ ] Implement the opaque pointer pattern to hide implementation details
> - [ ] Use the `->` operator to manipulate struct pointers

## Environment Setup

We will run all the following experiments in this environment:

- Platform: Linux x86\_64 (WSL2 is also fine)
- Compiler: GCC 13+ or Clang 17+
- Compiler flags: `-std=c23 -O2 -Wall -Wextra`

## Step 1 — Understanding How restrict Makes Code Faster

### Pointer Aliasing — The Compiler's Nightmare

Consider this function:

```c
void add_arrays(int *a, int *b, int *result, int n) {
    for (int i = 0; i < n; i++) {
        result[i] = a[i] + b[i];
    }
}
```

The compiler faces a problem here: `result` and `a` (or `b`) might point to the same memory. For example, when calling `add_arrays(arr, arr, arr, 10)`, after writing to `result[i]`, the value of `a[i]` also changes. Therefore, the compiler dares not perform aggressive optimizations—it must re-read `a[i]` and `b[i]` from memory after every write to `result[i]`.

This is the "pointer aliasing" problem: the compiler cannot determine whether two pointers point to the same memory, so it must handle them conservatively.

### restrict — A Contract Between Programmer and Compiler

`restrict` is a qualifier introduced in C99 that tells the compiler: "I guarantee that the memory accessed by this pointer will not be accessed through any other pointer."

```c
void add_arrays(int *restrict a, int *restrict b, int *restrict result, int n) {
    for (int i = 0; i < n; i++) {
        result[i] = a[i] + b[i];
    }
}
```

With `restrict` added, the compiler knows that `a`, `b`, and `result` do not overlap, so it can safely perform optimizations like vectorization (SIMD) and loop unrolling.

Let's look at a more intuitive example:

```c
void update(int *restrict p, int *restrict q) {
    *p = 10;
    *q = 20;
    int sum = *p + *q; // Compiler knows *p is 10, no need to re-read from memory
}
```

Inside `update`, the compiler doesn't even need to re-read from memory—it already knows the value of `*p`.

> ⚠️ **Pitfall Warning**
> `restrict` is a one-way promise from the programmer to the compiler; the compiler does not check this at runtime. If you pass overlapping pointers, the behavior is undefined behavior (UB)—the optimized code can produce any result, and this kind of bug only surfaces under specific compiler flags, making it extremely painful to track down.

### memcpy vs memmove — A Classic Comparison

There is a classic example in the standard library that perfectly illustrates the purpose of `restrict`:

```c
void *memcpy(void *restrict dest, const void *restrict src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
```

`memcpy` assumes the memory does not overlap and uses `restrict`, making it faster. `memmove` allows overlapping memory and cannot use `restrict`; it must perform additional checks and buffering internally, making it slightly slower. If you are certain the source and destination do not overlap, prefer `memcpy`.

## Step 2 — Understanding Incomplete Types and Forward Declarations

### What Is an Incomplete Type

If the compiler knows a type exists but does not know its size or internal structure, that type is incomplete. The most common example:

```c
struct Sensor; // Incomplete type - compiler knows it exists, but not what's inside
```

There are very few things you can do with an incomplete type: declare a pointer to it, or use its pointer in a function declaration. To do anything more (define variables, access members, `sizeof`), you must provide a complete definition.

### What Are Forward Declarations Good For

The most direct use of forward declarations is to reduce header file dependencies. Let's look at an example:

```c
// sensor_manager.h
struct Sensor; // Forward declaration - no need to include sensor.h

void sensor_init(struct Sensor *s);
void sensor_read(struct Sensor *s);
```

If `sensor_manager.h` only contains pointers to `Sensor`, we do not need `#include "sensor.h"`. This prevents users of `sensor_manager.h` from being forced to pull in all the dependencies of `sensor.h`, which also speeds up compilation.

> ⚠️ **Pitfall Warning**
> Forward declarations can only be used to declare pointers or references. If you put `struct Sensor` directly in the header file (not as a pointer), the compiler must know the complete definition of `Sensor` to determine its size—at this point, a forward declaration will not work, and you must `#include` the complete header file.

## Step 3 — Hiding Implementation Details with Opaque Pointers

Incomplete types have a very important application pattern in C: the opaque pointer. The idea is that the header file only exposes the forward declaration and manipulation functions, without exposing the internal details of the struct.

```c
// stack.h
#ifndef STACK_H
#define STACK_H

typedef struct Stack Stack; // Opaque type

Stack *stack_create(int capacity);
void stack_push(Stack *s, int value);
int stack_pop(Stack *s);
void stack_destroy(Stack *s);

#endif
```

The caller can only manipulate `Stack` through functions and can never see the internal structure of `Stack`. The implementation provides the complete definition in the `.c` file:

```c
// stack.c
#include "stack.h"
#include <stdlib.h>

struct Stack {
    int *data;
    int top;
    int capacity;
};

Stack *stack_create(int capacity) {
    Stack *s = malloc(sizeof(Stack));
    s->data = malloc(sizeof(int) * capacity);
    s->top = -1;
    s->capacity = capacity;
    return s;
}

void stack_push(Stack *s, int value) {
    s->data[++s->top] = value;
}

int stack_pop(Stack *s) {
    return s->data[s->top--];
}

void stack_destroy(Stack *s) {
    free(s->data);
    free(s);
}
```

The benefit here is that you can modify the internal implementation of `Stack` (for example, adding a growth strategy), and as long as the function signatures remain unchanged, callers do not need to recompile. The standard library's `FILE` is a classic example of this pattern—you never know what `FILE` looks like on the inside, and you only use `fopen`/`fclose`/`fread`/`fwrite` to operate on it.

## Step 4 — Using -> to Manipulate Struct Pointers

When passing structs between functions, we typically use pointers to avoid copy overhead. There are two ways to access members pointed to by a struct pointer:

```c
struct Point {
    int x;
    int y;
};

struct Point p;
struct Point *ptr = &p;

(*ptr).x = 10; // Dereference first, then access member
ptr->x = 10;   // Directly access member through pointer
```

`->` is syntactic sugar invented to save us typing. Just remember the rule: **use `.` for struct variables, use `->` for struct pointers**.

```c
void move_point(struct Point *p, int dx, int dy) {
    p->x += dx;  // Correct: p is a pointer, use ->
    p->y += dy;
}
```

> ⚠️ **Pitfall Warning**
> Confusing `.` and `->` is one of the most common mistakes beginners make. `ptr->x` is correct, but `ptr.x` will not compile (`ptr` is a pointer, not a variable), and while `(*ptr).x` is equivalent, the parentheses are easy to forget. Just build the habit of using `->`.

## C++ Connections

### PIMPL — The Modern Version of Opaque Pointers

PIMPL (Pointer to Implementation) is the direct descendant of the opaque pointer in C++. It hides the private implementation of a class behind a pointer to an incomplete type, and the header file only needs a forward declaration:

```cpp
// widget.hpp
class Widget {
public:
    Widget();
    ~Widget();
    void do_something();
private:
    struct Impl;      // Forward declaration
    Impl *p_impl;     // Opaque pointer
};
```

Modifying the internal structure of `Impl` does not require recompiling all files that include `widget.hpp`, which drastically reduces compilation time and makes the ABI more stable.

### Why C++ Never Formally Adopted restrict

The C++ standard has never introduced `restrict`. C++ class semantics and references make pointer aliasing analysis more complex—the compiler needs to consider `this` pointers, reference bindings, and object lifetimes, which are issues that do not exist in C. However, mainstream compilers do provide extensions: GCC and Clang use `__restrict__`, and MSVC uses `__restrict`. So you can still use it in C++, it is just not standard.

## Common Pitfalls

| Pitfall | Description | Solution |
|---------|-------------|----------|
| Passing overlapping pointers under restrict | Undefined behavior (UB), the compiler will not check it | Ensure the memory pointed to by restrict pointers truly does not overlap |
| Accessing members directly after a forward declaration | All member accesses will fail | Forward declarations can only declare pointers; full usage requires a complete definition |
| Confusing `.` and `->` | Use `->` for pointers, use `.` for variables | `ptr->x` is equivalent to `(*ptr).x` |
| Mixing up memcpy and memmove | Using memcpy when source and destination overlap is UB | Use memmove if there is a risk of overlap |

## Summary

In this chapter, we looked at three independent but practical mechanisms. `restrict` enables the compiler to perform more aggressive optimizations by eliminating pointer aliasing, but it is a "programmer's guarantee to the compiler"—breaking it results in undefined behavior (UB). Incomplete types and forward declarations allow us to design interfaces without exposing internal details, and the opaque pointer pattern is a classic technique for information hiding in C. `->` is the everyday tool for manipulating struct pointers; just remember "use `.` for variables, use `->` for pointers" and you are set.

## Exercises

### Exercise: Implement a Simple Opaque Pointer Module

Use the opaque pointer pattern to implement a simple Stack module. Requirements:

- Provide `stack_create`, `stack_push`, `stack_pop`, `stack_is_empty`, and `stack_destroy` functions
- The header file must not expose the internal structure of the stack
- Implement the stack using a fixed-size array (capacity passed in at creation)

Hint: Define the complete structure of `Stack` in the `.c` file (you can use an array plus a top index), and only put the forward declaration and function declarations in the `.h` file.

## References

- [restrict qualifier - cppreference](https://en.cppreference.com/w/c/language/restrict)
- [Incomplete types - cppreference](https://en.cppreference.com/w/c/language/type)
