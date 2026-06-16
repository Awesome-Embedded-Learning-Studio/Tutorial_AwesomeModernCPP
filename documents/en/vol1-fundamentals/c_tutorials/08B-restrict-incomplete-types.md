---
chapter: 1
cpp_standard:
- 11
- 17
description: Understand the optimization principles of the restrict qualifier, the
  purpose of incomplete types and forward declarations, the opaque pointer pattern,
  and the -> operator for struct pointer operations.
difficulty: beginner
order: 12
platform: host
prerequisites:
- 多级指针与声明读法
reading_time_minutes: 9
tags:
- host
- cpp-modern
- beginner
- 入门
- 基础
title: restrict, Incomplete Types, and Structure Pointers
translation:
  source: documents/vol1-fundamentals/c_tutorials/08B-restrict-incomplete-types.md
  source_hash: cbd7b2254a2f66092086fcbf58a0b95e926475a7b3e2138721a367fc71ab4c54
  translated_at: '2026-06-16T03:34:31.732335+00:00'
  engine: anthropic
  token_count: 1781
---
# restrict, Incomplete Types, and Structure Pointers

In the previous post, we mastered multi-level pointers and declaration reading. In this post, we will look at several relatively independent but very useful mechanisms: the `restrict` qualifier allows the compiler to perform more aggressive optimizations, incomplete types and forward declarations let us design interfaces without exposing internal details, and the `->` operator is a daily tool for manipulating structure pointers.

These three things may seem unrelated, but they are all very practical in C language engineering practices—and they all have corresponding modern versions in C++.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Understand what problems the `restrict` qualifier solves and its usage rules.
> - [ ] Use incomplete types and forward declarations to reduce header file dependencies.
> - [ ] Implement the opaque pointer pattern to hide implementation details.
> - [ ] Use the `->` operator to manipulate structure pointers.

## Environment Setup

We will conduct all subsequent experiments in the following environment:

- Platform: Linux x86_64 (WSL2 is also acceptable)
- Compiler: GCC 13+ or Clang 17+
- Compiler flags: `-std=c23 -Wall -Wextra -O3`

## Step 1 — Understanding Why `restrict` Makes Code Faster

### Pointer Aliasing — The Compiler's Nightmare

Consider this function:

```c
void add_arrays(int* a, int* b, int* c, int n) {
    for (int i = 0; i < n; i++) {
        a[i] = b[i] + c[i];
    }
}
```

The compiler faces a problem here: `b` and `c` might point to the same memory. For example, when calling `add_arrays(x, x, x, 10)`, writing to `a` changes `b` and `c` as well. Therefore, the compiler dares not perform aggressive optimizations—it must re-read from memory every time after writing to `a`.

This is the "pointer aliasing" problem: the compiler cannot determine if two pointers point to the same memory block, so it must handle it conservatively.

### restrict — A Contract Between Programmer and Compiler

`restrict` is a qualifier introduced in C99 that tells the compiler: "I guarantee that the memory accessed by this pointer will not be accessed through any other pointer."

```c
void add_arrays(int* restrict a, int* restrict b, int* restrict c, int n) {
    for (int i = 0; i < n; i++) {
        a[i] = b[i] + c[i];
    }
}
```

After adding `restrict`, the compiler knows that `b` and `c` do not overlap, so it can safely perform optimizations like vectorization (SIMD) and loop unrolling.

Let's look at a more intuitive example:

```c
int add(int* restrict a, int* restrict b) {
    *a = 10;
    *b = 20;
    return *a + *b; // Compiler knows *a is 10, no need to reload from memory
}
```

In `add`, the compiler doesn't even need to re-read memory—it already knows the value of `*a`.

> ⚠️ **Warning**
> `restrict` is a one-way commitment from the programmer to the compiler; the compiler does not check this at runtime. If you pass overlapping pointers, the behavior is undefined—the optimized code might produce any result, and this type of bug only exposes itself under specific compiler options, making debugging very painful.

### memcpy vs memmove — A Classic Comparison

There is a classic example in the standard library that illustrates the purpose of `restrict`:

```c
void* memcpy(void* restrict dest, const void* restrict src, size_t n);
void* memmove(void* dest, const void* src, size_t n);
```

`memcpy` assumes memory does not overlap and uses `restrict`, so it is faster. `memmove` allows overlap and cannot use `restrict`; it must perform additional checks and buffering internally, so it is slightly slower. If you are sure the source and destination do not overlap, prefer `memcpy`.

## Step 2 — Understanding Incomplete Types and Forward Declarations

### What is an Incomplete Type?

If the compiler knows a type exists but does not know its size and internal structure, that type is incomplete. The most common example:

```c
struct Buffer; // Forward declaration, incomplete type

struct Buffer* buf; // OK: Can declare a pointer
// struct Buffer buf; // Error: Cannot define variable, size unknown
```

There are limited things you can do with an incomplete type: declare a pointer to it, or use its pointer in a function declaration. To do more (define variables, access members, `sizeof`), you must provide the full definition.

### What are Forward Declarations Good For?

The most direct use of forward declarations is to reduce header file dependencies. Let's look at an example:

```c
// buffer.h
#ifndef BUFFER_H
#define BUFFER_H

#include "data.h" // Heavy dependency

struct Buffer {
    Data* data;
    size_t size;
};

void buffer_process(struct Buffer* buf);

#endif
```

If `buffer.h` only holds a pointer to `Data`, we don't need to `#include "data.h"`. This prevents users of `buffer.h` from being forced to pull in all dependencies of `data.h`, and compilation speed can improve.

```c
// buffer.h
#ifndef BUFFER_H
#define BUFFER_H

struct Data; // Forward declaration is enough

struct Buffer {
    struct Data* data;
    size_t size;
};

void buffer_process(struct Buffer* buf);

#endif
```

> ⚠️ **Warning**
> Forward declarations can only be used to declare pointers or references. If you put `Data d` directly in the header file (not a pointer), the compiler must know the full definition of `Data` to determine the size of `Buffer`—in this case, a forward declaration won't work, and you must `#include` the full header file.

## Step 3 — Hiding Implementation Details with Opaque Pointers

Incomplete types have a very important application pattern in C: the opaque pointer. The idea is that the header file only exposes forward declarations and manipulation functions, not the internal details of the structure.

```c
// stack.h
#ifndef STACK_H
#define STACK_H

typedef struct Stack Stack; // Incomplete type

Stack* stack_create(void);
void stack_push(Stack* s, int value);
int stack_pop(Stack* s);
void stack_destroy(Stack* s);

#endif
```

The caller can only manipulate `Stack` through functions and never sees the internal structure of `Stack`. The implementation provides the full definition in the `.c` file:

```c
// stack.c
#include "stack.h"
#include <stdlib.h>

struct Stack { // Full definition here
    int* data;
    size_t size;
    size_t capacity;
};

Stack* stack_create(void) {
    Stack* s = malloc(sizeof(Stack));
    // ... initialization ...
    return s;
}

// ... other function implementations ...
```

The benefit of this is: you can modify the internal implementation of `Stack` (e.g., adding a growth strategy), and as long as the function signatures don't change, the caller doesn't need to recompile. The standard library's `FILE*` is a classic example of this pattern—you never know what `FILE` looks like inside, you only use `fopen`/`fread`/`fwrite`/`fclose` to operate on it.

## Step 4 — Using `->` to Operate on Structure Pointers

When passing structures between functions, we usually use pointers to avoid copying overhead. There are two ways to access members pointed to by a structure pointer:

```c
struct Point {
    int x;
    int y;
};

struct Point p = {10, 20};
struct Point* ptr = &p;

int x1 = (*ptr).x; // Dereference then access
int x2 = ptr->x;   // Use -> operator
```

`->` is syntactic sugar invented to save us typing. Just remember the rule: **structure variables use `.`, structure pointers use `->`**.

```c
void move(struct Point* p, int dx, int dy) {
    p->x += dx;
    p->y += dy;
}
```

> ⚠️ **Warning**
> Confusing `.` and `->` is one of the most common mistakes for beginners. `ptr->x` is correct, but `ptr.x` won't compile (`ptr` is a pointer, not a variable), and `(*ptr).x`, while equivalent, makes it easy to forget the parentheses. It's best to develop the habit of using `->`.

## C++ Connection

### PIMPL — The Modern Version of Opaque Pointer

PIMPL (Pointer to Implementation) is the direct successor of the opaque pointer in C++. It hides the private implementation of a class behind a pointer to an incomplete type, and the header file only needs a forward declaration:

```cpp
// MyClass.h
class MyClass {
public:
    MyClass();
    ~MyClass();
    void doSomething();

private:
    class Impl; // Forward declaration
    Impl* pImpl; // Opaque pointer
};
```

Modifying the internal structure of `Impl` does not require recompiling all files that include `MyClass.h`, drastically reducing compilation time and stabilizing the ABI.

### Why C++ Didn't Officially Adopt `restrict`

The C++ standard has not introduced `restrict`. C++ class semantics and references make pointer aliasing analysis more complex—the compiler has to consider `this` pointers, reference binding, object lifetimes, and other issues that don't exist in C. However, mainstream compilers provide extensions: GCC and Clang use `__restrict__`, and MSVC uses `__restrict`. So you can use it in C++, but it's not standard.

## Common Pitfalls

| Pitfall | Description | Solution |
|---------|-------------|----------|
| Passing overlapping pointers with `restrict` | Undefined behavior, compiler won't check | Ensure memory pointed to by `restrict` pointers truly does not overlap |
| Using members directly after forward declaration | `sizeof`, accessing members all fail | Forward declarations can only declare pointers; full usage requires full definition |
| Confusing `.` and `->` | Pointers use `->`, variables use `.` | `ptr->x` is equivalent to `(*ptr).x` |
| Mixing up `memcpy` and `memmove` | Using `memcpy` with overlapping source and destination is UB | Use `memmove` if there is any risk of overlap |

## Summary

In this post, we looked at three independent but practical mechanisms. `restrict` allows the compiler to perform more aggressive optimizations by eliminating pointer aliasing, but it is a contract where "the programmer guarantees to the compiler"—breaking it leads to undefined behavior. Incomplete types and forward declarations allow us to design interfaces without exposing internal details, and the opaque pointer pattern is a classic technique for information hiding in C. `->` is the tool for daily manipulation of structure pointers; just remember "variables use `.`, pointers use `->`".

## Exercises

### Exercise: Implement a Simple Opaque Pointer Module

Use the opaque pointer pattern to implement a simple Stack module. Requirements:

- `stack.h`: Contains only the `struct Stack` forward declaration and function declarations.
- `stack.c`: Contains the full definition of `struct Stack` and function implementations.
- `main.c`: Tests the stack functionality.

Hint: Define the full structure of `struct Stack` in the `.c` file (you can implement it using an array + top index), and put only the forward declaration and function declarations in the `.h` file.

## References

- [restrict type qualifier - cppreference](https://en.cppreference.com/w/c/language/restrict)
- [Incomplete type - cppreference](https://en.cppreference.com/w/c/language/type)
