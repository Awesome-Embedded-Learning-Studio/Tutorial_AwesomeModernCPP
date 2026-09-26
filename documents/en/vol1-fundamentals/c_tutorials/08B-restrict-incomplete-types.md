---
chapter: 1
cpp_standard:
  - 11
  - 17
description: Understand how the restrict qualifier unlocks optimizations, what
  incomplete types and forward declarations are for, the opaque pointer pattern,
  and using -> on structure pointers
difficulty: beginner
order: 12
platform: host
prerequisites:
  - Multilevel Pointers and Reading Declarations
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
  source_hash: 16c78f0b870bbb61cdffe29d8eac245b5620c8e71cd247c981b0d06ec4c5b9ca
  translated_at: '2026-09-25T13:05:23+00:00'
  engine: anthropic
  token_count: 1800
---
# restrict, Incomplete Types, and Structure Pointers

In the last article we wrapped up multilevel pointers and how to read declarations. This time we'll look at several mechanisms that are fairly independent of one another but all very useful: the `restrict` qualifier, which lets the compiler get more aggressive with optimizations; incomplete types and forward declarations, which let us design interfaces without exposing internal details; and the `->` operator, the everyday tool for working with structure pointers.

These three things look unrelated, but all of them are extremely practical in real-world C engineering—and each has a modern counterpart in C++.

## Step 1 — Understanding Why restrict Makes Code Faster

### Pointer Aliasing — The Compiler's Nightmare

Consider this function:

```c
void vector_add(int n, int* a, int* b)
{
    for (int i = 0; i < n; i++) {
        a[i] = a[i] + b[i];
    }
}
```

The compiler faces a problem here: `a` and `b` might point to the same memory. If you call `vector_add(10, arr, arr)`, for example, writing `a[i]` also changes `b[i]`. So the compiler can't optimize aggressively—after every write to `a[i]`, it has to reload `b[i]` from memory.

This is the pointer aliasing problem: the compiler cannot tell whether two pointers refer to the same memory, so all it can do is play it safe.

### restrict — A Contract Between the Programmer and the Compiler

`restrict` is a qualifier introduced in C99. It tells the compiler: "I guarantee that the memory this pointer accesses will not be accessed through any other pointer."

```c
void vector_add(int n, int* restrict a, int* restrict b)
{
    for (int i = 0; i < n; i++) {
        a[i] = a[i] + b[i];
    }
}
```

With `restrict` in place, the compiler knows `a` and `b` don't overlap and can confidently apply optimizations such as vectorization (SIMD) and loop unrolling.

Here's a more intuitive example:

```c
int foo(int* a, int* b)
{
    *a = 5;
    *b = 6;
    return *a + *b;
    // The compiler can't assume *a is still 5, because b might be a
    // It must reload *a from memory
}

int rfoo(int* restrict a, int* restrict b)
{
    *a = 5;
    *b = 6;
    return *a + *b;
    // The compiler knows a and b don't overlap, so *a is definitely 5
    // It can return 11 directly, without reloading from memory
}
```

In `rfoo`, the compiler doesn't even need to reload from memory—it already knows the value of `*a`.

`restrict` is a one-way promise from the programmer to the compiler, and the compiler never checks it at runtime. If you pass overlapping pointers, the behavior is undefined—the optimized code may produce any result at all, and the bug only surfaces under particular compiler flags, which makes it miserable to track down.

### memcpy vs memmove — The Classic Comparison

The standard library contains a classic pair of functions that illustrate exactly what `restrict` is for:

```c
void* memcpy(void* restrict dest, const void* restrict src, size_t n);
void* memmove(void* dest, const void* src, size_t n);
```

`memcpy` assumes the memory doesn't overlap, so it uses `restrict` and is faster. `memmove` allows overlap, can't use `restrict`, and has to do extra checks and buffering internally, so it's slightly slower. If you're sure the source and destination don't overlap, prefer `memcpy`.

## Step 2 — Mastering Incomplete Types and Forward Declarations

### What an Incomplete Type Is

If the compiler knows a type exists but doesn't know its size or internal layout, the type is incomplete (an incomplete type). The most common example:

```c
struct Foo;  // Forward declaration: tells the compiler "Foo is a struct" without saying what's inside

struct Foo* p;    // Legal: pointers have a fixed size; Foo's full definition isn't needed
struct Foo  obj;  // Illegal: the compiler doesn't know Foo's size, so it can't allocate space
```

There's only a little you can do with an incomplete type: declare pointers to it, and use such pointers in function declarations. To do anything more (define variables, access members, take `sizeof`), you must provide the complete definition.

### What Forward Declarations Are Good For

The most direct use of forward declarations is cutting down header dependencies. Take this example:

```c
// car.h
struct Engine;  // Forward declaration — no need to #include "engine.h"

struct Car {
    struct Engine* engine;  // Only a pointer; the forward declaration is enough
    int speed;
};
```

If `Car` only holds a pointer to `Engine`, we don't need `#include "engine.h"`. Users of `car.h` then aren't forced to drag in all of `engine.h`'s dependencies, and compile times improve too.

Forward declarations only work for declaring pointers or references. If you put `struct Engine engine;` (not a pointer) directly in the header, the compiler must know `Engine`'s complete definition to work out `Car`'s size—at that point the forward declaration no longer helps, and you must `#include` the full header.

## Step 3 — Hiding Implementation Details with Opaque Pointers

Incomplete types have one hugely important application pattern in C: the opaque pointer. The idea is that the header exposes only a forward declaration plus functions that operate on the type—never the struct's internal details.

```c
// buffer.h — public header
typedef struct Buffer Buffer;  // forward declaration + typedef

Buffer* buffer_create(int capacity);
void    buffer_destroy(Buffer* buf);
int     buffer_append(Buffer* buf, const char* data, int len);
int     buffer_length(const Buffer* buf);
```

Callers can only manipulate `Buffer` through the functions; they never get to see inside `struct Buffer`. The implementation provides the complete definition in the `.c` file:

```c
// buffer.c — implementation file
#include "buffer.h"
#include <stdlib.h>
#include <string.h>

struct Buffer {
    char* data;
    int   capacity;
    int   length;
};

Buffer* buffer_create(int capacity)
{
    Buffer* buf = (Buffer*)malloc(sizeof(Buffer));
    buf->data = (char*)malloc(capacity);
    buf->capacity = capacity;
    buf->length = 0;
    return buf;
}

void buffer_destroy(Buffer* buf)
{
    if (buf) {
        free(buf->data);
        free(buf);
    }
}

int buffer_append(Buffer* buf, const char* data, int len)
{
    if (buf->length + len > buf->capacity) {
        return -1;  // not enough buffer space
    }
    memcpy(buf->data + buf->length, data, len);
    buf->length += len;
    return 0;
}

int buffer_length(const Buffer* buf)
{
    return buf->length;
}
```

The benefit: you can change `Buffer`'s internal implementation (add a growth strategy, say), and as long as the function signatures don't change, callers don't need to recompile. The standard library's `FILE` is the classic example of this pattern—you have never known what a `FILE` looks like inside; you just manipulate it through `fopen`/`fclose`/`fread`/`fwrite`.

## Step 4 — Using -> to Work with Structure Pointers

When passing structs between functions, we usually use pointers to avoid the cost of copying. There are two ways to access the member a structure pointer points to:

```c
typedef struct {
    float x;
    float y;
} Point;

Point p = {3.0f, 4.0f};
Point* ptr = &p;

// Approach 1: dereference first, then access the member with .
float x1 = (*ptr).x;   // The parentheses can't be omitted, because . binds tighter than *

// Approach 2: use the -> operator (syntactic sugar)
float x2 = ptr->x;     // equivalent to (*ptr).x
```

`->` is syntactic sugar invented so we can type less. Just remember the rule: **use `.` with struct variables, use `->` with structure pointers**.

```c
typedef struct {
    Point center;
    float radius;
} Circle;

Circle c = {{0.0f, 0.0f}, 5.0f};
Circle* cp = &c;

cp->center.x = 1.0f;        // change the center's x
cp->radius = 10.0f;          // change the radius

void move_circle(Circle* c, float dx, float dy)
{
    c->center.x += dx;
    c->center.y += dy;
}

move_circle(cp, 2.0f, 3.0f);
```

Mixing up `.` and `->` is one of the most common beginner mistakes. `cp->center.x` is correct, but `cp.center.x` won't compile (`cp` is a pointer, not a variable), and `(*cp).center.x`, while equivalent, makes the parentheses all too easy to forget. Just build the habit of using `->`.

## Bridging to C++

### PIMPL — The Modern Version of the Opaque Pointer

PIMPL (Pointer to Implementation) is the direct heir of the opaque pointer in C++. It hides a class's private implementation behind a pointer to an incomplete type; the header needs only a forward declaration:

```cpp
// widget.h — public header
class Widget {
public:
    Widget();
    ~Widget();
    void do_something();
private:
    struct Impl;          // forward declaration
    Impl* pimpl_;         // pointer to an incomplete type
};

// widget.cpp — implementation file
struct Widget::Impl {
    int internal_state = 0;
    void helper() { /* ... */ }
};

Widget::Widget() : pimpl_(new Impl{}) {}
Widget::~Widget() { delete pimpl_; }

void Widget::do_something() {
    pimpl_->internal_state++;
}
```

Changing `Impl`'s internal structure no longer forces a recompile of every file that includes `widget.h`; compile times drop substantially, and the ABI becomes more stable.

### Why C++ Never Formally Adopted restrict

The C++ standard has never adopted `restrict`. Class semantics and references make pointer aliasing analysis far more complicated in C++—the compiler has to consider the `this` pointer, reference binding, object lifetimes, and other issues that don't exist in C. Still, every mainstream compiler offers an extension: GCC and Clang use `__restrict`, and MSVC uses `__restrict` too. So you can use it in C++ as well—it's just not standard.

## Common Pitfalls

| Pitfall                              | Explanation                          | Fix                                        |
| ------------------------------------ | ------------------------------------ | ------------------------------------------ |
| Passing overlapping pointers to restrict parameters | Undefined behavior; the compiler won't check | Make sure the memory a restrict pointer targets really doesn't overlap |
| Using members right after a forward declaration | `struct Foo; Foo f; f.x = 1;` — all wrong | Forward declarations only support pointers; full use requires the full definition |
| Mixing up `.` and `->`               | Pointers take `->`, variables take `.` | `ptr->member` is equivalent to `(*ptr).member` |
| Confusing memcpy and memmove         | memcpy on overlapping source and destination is UB | If there's any risk of overlap, use memmove |

## Exercises

### Exercise: Build a Simple Opaque Pointer Module

**Difficulty: Intermediate** · Implement a stack module with the opaque pointer pattern

Implement a simple Stack module using the opaque pointer pattern. Requirements:

```c
// stack.h — expose only the interface, not the internal structure
typedef struct Stack Stack;

Stack* stack_create(int capacity);
void   stack_destroy(Stack* s);
int    stack_push(Stack* s, int value);   // returns 0 on success, -1 when the stack is full
int    stack_pop(Stack* s, int* out);     // returns 0 on success, -1 when the stack is empty
int    stack_size(const Stack* s);
```

::: details Reference Solution

```c
// stack.h — expose only the interface, not the internal structure
typedef struct Stack Stack;

Stack* stack_create(int capacity);
void   stack_destroy(Stack* s);
int    stack_push(Stack* s, int value);   // returns 0 on success, -1 when the stack is full
int    stack_pop(Stack* s, int* out);     // returns 0 on success, -1 when the stack is empty
int    stack_size(const Stack* s);
```

```c
// stack.c — implementation file, defines the complete struct
#include <stdlib.h>
#include "stack.h"

struct Stack {
    int* data;
    int capacity;
    int size;
};

Stack* stack_create(int capacity){
    if (capacity <= 0) {
        return NULL;
    }

    Stack* s = (Stack*)malloc(sizeof(Stack));
    if (s == NULL){
        return NULL;
    }

    s->capacity = capacity;
    s->size = 0;
    s->data = (int*)malloc(capacity * sizeof(int));

    if (s->data == NULL) {
        free(s);
        return NULL;
    }

    return s;
}

void stack_destroy(Stack* s){
    if (s) {
        free(s->data);
        free(s);
    }
}

int stack_push(Stack* s, int value){
    if(s == NULL){
        return -1;  // the stack doesn't exist, return -1
    }

    if(s->size == s->capacity){
        return -1;  // the stack is full, return -1
    }

    s->data[stack_size(s)] = value;
    s->size++;

    return 0;
}

int stack_pop(Stack* s, int* out){
    if(s == NULL || out == NULL){
        return -1;  // the stack doesn't exist or the output pointer is null, return -1
    }
    if(stack_size(s) == 0){
        return -1;  // the stack is empty, return -1
    }
    *out = s->data[stack_size(s) - 1];
    s->size--;
    return 0;
}

int stack_size(const Stack* s){
    if(s == NULL){
        return 0;  // treat a nonexistent stack as empty
    }
    return s->size;
}
```

Hint: define the complete layout of `struct Stack` in the `.c` file (an array plus a top-of-stack index works fine), and put only the forward declaration and the function declarations in the `.h` file.

:::

## References

- [The restrict qualifier - cppreference](https://en.cppreference.com/w/c/language/restrict)
- [Incomplete types - cppreference](https://en.cppreference.com/w/c/language/type)
