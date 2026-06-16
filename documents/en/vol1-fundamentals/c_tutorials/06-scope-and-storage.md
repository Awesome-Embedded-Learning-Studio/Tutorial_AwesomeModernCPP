---
chapter: 1
cpp_standard:
- 11
description: Gain a deep understanding of C scope rules, storage classes, and linkage,
  and master the three uses of `static`.
difficulty: beginner
order: 8
platform: host
prerequisites:
- 控制流：让程序学会选择和重复
reading_time_minutes: 20
tags:
- host
- cpp-modern
- beginner
- 入门
- 基础
title: Scope and Storage Duration
translation:
  source: documents/vol1-fundamentals/c_tutorials/06-scope-and-storage.md
  source_hash: ff0a0effe58d45bcde48719d1c91fc0a24a6697857c99ed91cd766c4c06d526f
  translated_at: '2026-06-16T03:34:18.721463+00:00'
  engine: anthropic
  token_count: 3100
---
# Scope and Storage Classes

If you have written a project with more than two source files, you have likely encountered this pitfall: defining a global variable named `counter` in both files, only to have the linker look at you in confusion during compilation and report a `multiple definition` error. Or, a more subtle scenario—you define a helper function in a `.c` file, another file accidentally calls it, and later when you change that function's implementation, the caller crashes without warning.

The root of these problems lies in **scope** and **storage classes**. The former determines where a name can be used within the program, while the latter determines how long the entity corresponding to that name lives in memory and who can see it. These concepts are intertwined, and because the `static` keyword wears multiple hats in C, beginners often get confused.

Today, we will untangle this mess—starting from the most basic scope rules, moving through storage classes, linkage, and lifetimes, and finally examining what the three distinct usages of `static` actually are. Once we understand these, we can stop relying on gut feeling when organizing code in multi-file projects.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Identify the four scopes in C and their differences.
> - [ ] Explain the meanings of `auto`, `static`, `extern`, and `register`.
> - [ ] Understand how linkage (internal/external/none) controls symbol visibility.
> - [ ] Correctly use the three semantics of `static`.
> - [ ] Organize symbols in multi-file projects using `static` and `extern`.

## Environment Setup

We use GCC 12+ or Clang 15+ on Linux or WSL2. All examples can be compiled and run with a simple command:

```bash
gcc main.c -o main && ./main
```

For multi-file projects, we need to compile separately and then link, or do it all in one go:

```bash
gcc main.c module.c -o main && ./main
```

## Step 1 — Understanding the Four Scopes

The C standard defines four scopes: block scope, file scope, function scope, and function prototype scope. Let's go through them one by one.

### Block Scope

Block scope is the most common—the area enclosed by curly braces `{}` is a block. Variables declared inside a block are visible only within that block (and nested sub-blocks). The body of `if`, `for`, `while` loops, or even a pair of braces you write casually, all create new block scopes:

```c
#include <stdio.h>

int main(void) {
    int x = 10;       // x is visible from here to the end of main

    if (x > 5) {
        int y = 20;   // y is visible only inside this if block
        printf("%d %d\n", x, y);
    }

    // printf("%d\n", y); // Error! y is out of scope here
    return 0;
}
```

A point worth noting is that an inner block can **shadow** an outer block's variable with the same name—the inner `x` temporarily "covers up" the outer `x`, until the inner block ends:

```c
#include <stdio.h>

int main(void) {
    int x = 10;
    printf("Outer x: %d\n", x); // 10

    {
        int x = 20; // Shadows the outer x
        printf("Inner x: %d\n", x); // 20
    }

    printf("Outer x again: %d\n", x); // 10
    return 0;
}
```

Since C99, the initialization part of a `for` loop can also declare variables. The scope of this variable is the entire loop (including the loop body and the conditional part), and it is not visible outside the loop. This is consistent with C++ behavior, but if you are using an ancient C89 compiler (unlikely nowadays), loop variables must be declared outside the loop.

### File Scope

Variables and functions declared outside all functions have **file scope**—they are visible from the point of declaration to the end of the current translation unit (the `.c` file plus everything it `#include`s). We habitually call these "global variables," but their visibility isn't truly "global"—whether they are seen by other translation units depends on linkage, which we will discuss in detail later:

```c
int global_var = 100; // File scope

void func(void) {     // File scope
    // ...
}
```

### Function Scope

This scope is special; it **only applies to labels** (the name with the colon that is the jump target for `goto`). A label is visible throughout the entire function where it resides, regardless of which nesting level it is declared in. Honestly, since you likely won't use `goto` much, just knowing this scope exists is enough:

```c
#include <stdio.h>

int main(void) {
    goto label; // Jump forward

    {
        label: // The label is visible here, even inside a block
        printf("Jumped to label\n");
    }

    return 0;
}
```

### Function Prototype Scope

This is the smallest scope—parameter names appearing in a function declaration (prototype) are valid only within the parentheses of that declaration. They cease to exist outside the brackets. In fact, the compiler doesn't care about parameter names in prototypes (it only looks at types), so this scope can basically be ignored:

```c
int foo(int a, int b); // a and b are in function prototype scope
                       // They are irrelevant outside this line
```

## Step 2 — Understanding How Storage Classes Manage Lifetimes

Scope solves the problem of "where is a name visible," while storage classes solve "when is data created, when is it destroyed, and where does it live." C defines several storage class specifiers: `auto`, `static`, `extern`, `register`, and `thread_local` (added in C11).

### auto: Default Automatic Storage

`auto` is the default storage class for local variables—writing `auto int x = 10;` inside a function is exactly equivalent to `int x = 10;`. Because this is the default behavior, no one explicitly writes `auto`, so you basically won't see it in real code. It means the variable is created when the block is entered (allocated on the stack) and destroyed when the block is left.

A point of confusion: C++11 repurposed `auto` as a type deduction keyword, which has nothing to do with C's storage class. If you see `auto` in C++ code later, it's asking the compiler to deduce the type of `x` from the initializer, not a storage class.

### static: Persisting Through the Program

`static` is one of the keywords with the most meanings in C; it does completely different things depending on where it appears. Let's first look at its meaning as a storage class specifier—**changing a variable's lifetime from automatic to static**.

Ordinary local variables are re-initialized every time the function is entered and disappear when the function leaves. But if you add `static` to a local variable, it is initialized only once at program startup (if you don't give it an initial value, it is initialized to zero). After that, even if the function returns, the variable is not destroyed. The next time the function is called, it still retains the value from the last time:

```c
#include <stdio.h>

void counter(void) {
    static int count = 0; // Initialized only once
    count++;
    printf("Count: %d\n", count);
}

int main(void) {
    counter(); // Count: 1
    counter(); // Count: 2
    counter(); // Count: 3
    return 0;
}
```

Although this `count` looks like a "local variable," it is not stored on the stack—it resides in the Data Segment or BSS segment, living with global variables. The only difference is that its **scope** is still block scope; only the `counter` function can access it.

Why do this? Imagine you are writing a module that needs to maintain some internal state (like a buffer, counter, or configuration info), but you don't want external code to touch these data directly. Using a `static` local variable achieves the perfect combination of "data persistence + restricted access"—a simple implementation of information hiding.

### extern: Declaring Symbols Defined Elsewhere

`extern` tells the compiler "this variable/function is defined elsewhere; don't worry about where it is, the linker will find it." Its typical use is sharing global variables in multi-file projects:

```c
// config.h
#ifndef CONFIG_H
#define CONFIG_H

extern int app_config; // Declaration

#endif
```

```c
// config.c
#include "config.h"

int app_config = 100; // Definition
```

```c
// main.c
#include <stdio.h>
#include "config.h"

int main(void) {
    printf("Config: %d\n", app_config);
    return 0;
}
```

The key distinction here is: **definition** allocates memory and can appear only once; **declaration** uses `extern` to indicate "it is defined elsewhere" and can appear multiple times. Headers contain declarations, source files contain definitions—this is the basic organizational pattern for C multi-file projects.

A common pitfall is writing this:

```c
// config.h
extern int app_config = 100; // BAD: This is a definition!
```

If you assign an initial value to an `extern` declaration, `extern` is ignored—this becomes a definition. If this header is included by multiple `.c` files, each translation unit will generate a definition of `app_config`, and you will get a `multiple definition` error during linking.

> ⚠️ **Warning**
> Putting initialized variables in header files is a typical mistake—an `extern` with an initial value equals a definition. If the header is included multiple times, it causes linking conflicts. Remember: put only declarations (without initial values) in headers, and definitions in `.c` files.

### register: A Historical Suggestion

`register` was a keyword in early C used to suggest to the compiler "put this variable in a register." On the PDP-11 in the 1970s, compiler optimization was limited, and programmers manually specifying `register` could indeed improve performance.

But in front of modern compilers, this keyword is basically useless—GCC and Clang optimizers know better than you which variables should go in registers. In fact, you can write `register` and the compiler is free to ignore it. Also, you cannot take the address of a `register` variable (cannot use `&` on it) because it might not be in memory at all—this limitation can occasionally bite you.

Just understand it; it is not recommended in modern code.

## Step 3 — Mastering Linkage to Control Symbol Visibility

Linkage describes the visibility of a name between different translation units. C defines three types of linkage: external linkage, internal linkage, and no linkage.

- Names with **external linkage** can be accessed by all translation units in the program. Ordinary global variables and functions default to external linkage—as long as you declare them with `extern` in another file, you can use them.
- Names with **internal linkage** are visible only within the current translation unit; other files cannot find them even if they use `extern`. Adding `static` to a file-scope variable or function makes it internal linkage.
- Names with **no linkage** are valid only within their own scope—local variables, function parameters, and `static` variables inside block scope all have no linkage.

The relationship between these three can be summarized in a table:

| Declaration Location | Keyword | Linkage | Scope | Lifetime |
| --- | --- | --- | --- | --- |
| Inside Function | (none) | None | Block | Automatic |
| Inside Function | `static` | None | Block | Static |
| Outside Function | (none) | External | File | Static |
| Outside Function | `static` | Internal | File | Static |
| Outside Function | `extern` | (Depends on first declaration) | File | Static |

This table is worth a few looks—note that `static` outside a function changes **linkage** (from external to internal), not scope or lifetime.

Let's feel how linkage works through a multi-file example:

```c
// utils.h
#ifndef UTILS_H
#define UTILS_H

void helper_a(void); // External linkage by default

#endif
```

```c
// utils.c
#include <stdio.h>
#include "utils.h"

static void helper_b(void) { // Internal linkage
    printf("Helper B (internal)\n");
}

void helper_a(void) {
    printf("Helper A (external)\n");
    helper_b(); // Can call internal helper_b
}
```

```c
// main.c
#include <stdio.h>
#include "utils.h"

// extern void helper_b(void); // Error! Cannot declare internal linkage here

int main(void) {
    helper_a(); // OK
    // helper_b(); // Error! Not visible here
    return 0;
}
```

Compile and run:

```bash
gcc main.c utils.c -o main && ./main
```

Output:

```text
Helper A (external)
Helper B (internal)
```

`helper_b` in `utils.c` is marked with `static` for internal linkage, meaning even if another file has a global variable named `helper_b`, it won't conflict. This is the core value of `static` at the file level—**information hiding**, encapsulating the module's internal implementation details and exposing only the public interface through the header file.

If you wonder what happens without `static`—try defining a function named `helper` in two different `.c` files. You will likely see a linker `multiple definition` error during compilation. This is why global variables and helper functions that aren't meant to be exposed must be marked `static`.

## Step 4 — Clarifying the Three Uses of static

Understanding scope and linkage, the last dimension is **lifetime** (storage duration)—the time span from an object's creation to destruction. Lifetime is inseparable from the usage of `static`, so we discuss them together.

> ⚠️ **Warning**
> Never return a pointer to a local variable—after the function returns, that stack space is reclaimed, the pointer becomes a dangling pointer, and dereferencing it is undefined behavior. If you need to pass data between functions, either pass by value, use a `static` local variable, or allocate memory dynamically.

**Automatic lifetime** is the most common: ordinary local variables are created when the block is entered and destroyed when the block is left. They are stored on the stack; every time the function is called, local variables are created once, and they are gone after return. This is also why you cannot return a pointer to a local variable—after the function returns, that stack space is reclaimed, the pointer becomes a dangling pointer, and dereferencing it is undefined behavior.

**Static lifetime** objects exist from program startup until program termination. This includes all file-scope variables (whether they have `static` or not) and local variables declared with `static` inside functions. They are stored in the Data Segment (if initialized) or BSS Segment (if uninitialized, automatically initialized to zero).

**Dynamic lifetime** objects are allocated on the heap via `malloc`/`calloc`/`realloc` and managed manually by the programmer—when to `free` and when to destroy. We will discuss this in detail in the memory management chapter later.

```c
#include <stdio.h>
#include <stdlib.h>

int *create_buffer(void) {
    static int static_buf[10]; // Static lifetime, block scope
    int *auto_buf = malloc(10 * sizeof(int)); // Dynamic lifetime

    // auto_buf is lost if returned here! Memory leak!
    return static_buf; // OK
}
```

An easily overlooked fact is: the initialization order of global variables is deterministic within the same translation unit (in definition order), but **undefined** across translation units. For C, this usually isn't a big issue (since global variables are generally initialized with constant expressions), but in C++, this is a famous pitfall—C++ allows global objects to have constructors, and the construction order across files is undefined, known as the "static initialization order fiasco." We just need to know about this for now.

Since `static` has different meanings in different places, let's do a complete summary.

**Usage 1: Static Local Variable**—Inside a function, `static` gives a local variable static lifetime; the variable is not destroyed after the function returns, retaining its value for the next call, but its scope remains block scope.

**Usage 2: Static Global Variable**—Outside a function, `static` makes a global variable have internal linkage, invisible to other translation units. Scope remains file scope, lifetime remains static; the only change is linkage.

**Usage 3: Static Function**—Adding `static` to a function is similar to a static global variable; the function gets internal linkage and is visible only in the current translation unit.

Note that among these three usages, "static local variable" changes lifetime (from automatic to static), while "static global variable" and "static function" change linkage (from external to internal). The same keyword does two different things, which is a historical legacy issue in C language design, but you get used to it.

## C++ Connection

C++ has made several enhancements and improvements on top of scope and storage classes.

Most noteworthy is **namespace**. In C, if you don't want file-level helper symbols exposed to the outside, the only means is `static`—our `helper_b` earlier did exactly this. But C++ introduced `namespace`, providing a more structured way to organize symbols and avoid naming conflicts. Even better, C++17 introduced **`inline` variables**, allowing constant definitions in headers to no longer need the tedious pattern of `extern` in headers matching definitions in source files:

```cpp
// config.hpp
namespace Config {
    inline int max_connections = 100; // Definition in header, OK with inline
}
```

C++'s **`static` class members** are yet another semantic—it indicates the member belongs to the class itself rather than an instance of the class, and all objects share the same copy. This is different from C's `static` again:

```cpp
class Counter {
public:
    static int count; // Shared by all instances
};
```

Additionally, C++ anonymous namespaces can completely replace file-level `static` usage, and more thoroughly—symbols in an anonymous namespace are hidden from the outside and cannot even participate in template argument deduction. In C++ projects, using anonymous namespaces instead of `static` is recommended.

Finally, C++11's `thread_local` provides thread-level storage duration—each thread has its own independent copy of the variable. This is very useful in multithreaded programming. C11 also has corresponding `_Thread_local`, but its support and usability are not as good as C++.

## Summary

Scope, storage classes, and linkage together form the complete system of "name management" in C. Scope determines where a name is visible, storage classes determine how long data lives and where it is stored, and linkage determines whether a name can be accessed across files.

`static` is the most confusing keyword in this system—inside a function it changes lifetime, outside a function it changes linkage. But as long as you remember this distinction, you won't get mixed up again. `extern` is the tool for sharing global variables in multi-file projects, used in conjunction with the pattern of header declarations and source file definitions.

In actual projects, form a habit: **add `static` to all global variables and helper functions not intended for external exposure**. This is the most practical information-hiding measure at the C language level and can significantly reduce naming conflicts and accidental dependencies in multi-file projects.

### Key Points

- [ ] C has four scopes: block, file, function, and function prototype.
- [ ] `static` local variables have static lifetime but block scope.
- [ ] `static` global variables/functions have internal linkage and are invisible to other files.
- [ ] `extern` declares a symbol defined elsewhere.
- [ ] Global variables without `static` have external linkage; any file can access them via `extern`.
- [ ] Symbols with internal linkage do not conflict even if they have the same name in multiple files.

## Exercises

### Exercise 1: Modular Counter

Design a simple module where the header file exposes only three functions: `counter_init`, `counter_inc`, and `counter_get`. Internally, use a `static` variable to maintain the count. External code must not be able to directly access or modify this counter variable.

```c
// counter.h
#ifndef COUNTER_H
#define COUNTER_H

void counter_init(int value);
void counter_inc(void);
int  counter_get(void);

#endif
```

Please implement `counter.c` yourself.

### Exercise 2: Multi-file Symbol Visibility

Create three files: `data.c`, `helper.c`, and `main.c`. Requirements:

- `data.c` defines an external linkage global variable `g_sensor_data`, initial value `0`.
- `helper.c` defines an internal linkage helper function `process_data`.
- `main.c` also defines a same-named internal linkage helper function `process_data` (no conflict!).
- `main.c` accesses `g_sensor_data` via `extern` and provides a function to modify it.
- `main.c` calls functions provided by each module and verifies the results.

```c
// data.c
int g_sensor_data = 0;

// helper.c
static void process_data(void) {
    // Implementation
}

// main.c
#include <stdio.h>

extern int g_sensor_data;

static void process_data(void) {
    // Different implementation
}

void modify_sensor(int val) {
    g_sensor_data = val;
}

int main(void) {
    // ...
}
```

### Exercise 3: Lazy Initialization

Use a `static` local variable to implement a `get_config` function: on the first call, perform initialization (print "Initializing..." and set default values), subsequent calls directly return the initialized value without re-initializing.

```c
#include <stdio.h>

int *get_config(void) {
    static int config = 0;
    static int initialized = 0;

    if (!initialized) {
        printf("Initializing...\n");
        config = 42; // Load from EEPROM or something
        initialized = 1;
    }

    return &config;
}
```

> Hint: `static` local variables are initialized only when entering the function for the first time—perfect for implementing "initialize once" semantics.

## Reference Resources

- [Storage class specifiers - cppreference](https://en.cppreference.com/w/c/language/storage_duration)
- [Scope - cppreference](https://en.cppreference.com/w/c/language/scope)
- [Linkage - cppreference](https://en.cppreference.com/w/c/language/storage_duration#Linkage)
