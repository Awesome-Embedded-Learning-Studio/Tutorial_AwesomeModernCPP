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
reading_time_minutes: 24
tags:
- host
- cpp-modern
- beginner
- 入门
- 基础
title: Scope and Storage Duration
translation:
  source: documents/vol1-fundamentals/c_tutorials/06-scope-and-storage.md
  source_hash: 6f5f04a7650642fc294fdf8488f92465e74b56f8ba33056c3273f3747b315674
  translated_at: '2026-04-20T03:20:10.737396+00:00'
  engine: anthropic
  token_count: 3100
---
# Scope and Storage Duration

If you have ever written a project with more than two source files, you have probably run into this pitfall: defining a global variable called `count` in two files, only for the linker to get confused and throw a `multiple definition` at you. Or a more subtle scenario—you define a helper function in some `.c` file, another file happens to call it, and when you change the function's implementation, the caller crashes without any warning.

The root cause of all these problems lies in **scope** and **storage duration**. The former determines which parts of a program can use a given name, while the latter determines how long the entity behind that name lives in memory and who can see it. These two concepts are intertwined, and since the `static` keyword wears multiple hats in C, beginners often get confused.

Today, we will untangle this mess—starting from the basic scope rules, moving through storage duration and linkage, and finally examining the three entirely different uses of `static`. Once you understand these concepts, you will no longer have to rely on gut feeling when organizing code in multi-file projects.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Name the four types of scope in C and explain their differences
> - [ ] Explain the meanings of `auto`, `static`, `extern`, and `register`
> - [ ] Understand how linkage (internal/external/none) controls symbol visibility
> - [ ] Correctly use the three semantics of `static`
> - [ ] Use `extern` and `static` to organize symbols in multi-file projects

## Environment Setup

We use GCC 12+ or Clang 15+, compiling on Linux or WSL2. All examples can be compiled and run with a single command:

```bash
gcc -Wall -Wextra -std=c11 -o scope_demo scope_demo.c && ./scope_demo
```

Multi-file projects require compiling separately and then linking, or doing it all in one go:

```bash
gcc -Wall -Wextra -std=c11 -o multi_file_demo file1.c file2.c && ./multi_file_demo
```

## Step 1 — Understanding the Four Types of Scope

The C standard defines four types of scope: block scope, file scope, function scope, and function prototype scope. Let's go through them one by one.

### Block Scope

Block scope is the most common—the area enclosed by curly braces `{}` is a block, and variables declared inside a block are only visible within that block (and its nested sub-blocks). The loop bodies of `if`, `for`, and `while`, or even a pair of curly braces you casually write, all create new block scopes:

```c
#include <stdio.h>

int main(void) {
    int x = 10;  // x 在整个 main 函数体中可见

    if (x > 5) {
        int y = 20;       // y 只在这个 if 块中可见
        printf("x=%d, y=%d\n", x, y);  // OK
    }

    // printf("%d\n", y);  // 错误：y 已经不可见了

    {
        // 你甚至可以凭空创造一个块
        int z = 30;  // z 只在这个匿名块中可见
        printf("z=%d\n", z);
    }

    // printf("%d\n", z);  // 错误：z 同样不可见

    return 0;
}
```

One point worth noting is that an inner block can shadow a variable with the same name in an outer block—the inner `x` temporarily "hides" the outer `x` until the inner block ends:

```c
#include <stdio.h>

int main(void) {
    int value = 100;
    printf("Outer: %d\n", value);  // 100

    {
        int value = 200;  // 屏蔽外层的 value
        printf("Inner: %d\n", value);  // 200
    }

    printf("Outer again: %d\n", value);  // 100，外层的 value 没变
    return 0;
}
```

Since C99, the initialization part of a `for` loop can also declare variables. The scope of such a variable spans the entire loop (including the loop body and the condition check), and it is not visible outside the loop. This behavior is consistent with C++, but if you are using an ancient C89 compiler (which is unlikely these days), the loop variable must be declared outside the loop.

### File Scope

Variables and functions declared outside of all functions have file scope—they are visible from the point of declaration to the end of the current translation unit (that is, the `.c` file plus everything it `#include` in). By convention, we call these variables "global variables," but their visibility is not truly "global"—whether they can be seen by other translation units depends on linkage, which we will discuss in detail later:

```c
#include <stdio.h>

// 这两个具有文件作用域，从声明处到文件末尾可见
int kGlobalCounter = 0;
static int kInternalVar = 42;  // static 限制了链接性，但作用域仍是文件级

void increment_counter(void) {
    kGlobalCounter++;
}

int main(void) {
    increment_counter();
    printf("Counter: %d\n", kGlobalCounter);
    return 0;
}
```

### Function Scope

This scope is rather special—it **only applies to labels**, which are the names with colons used as jump targets for `goto`. A label is visible throughout the entire function where it resides, regardless of the nesting level where it is declared. Honestly, since you are unlikely to use `goto` much, just knowing that this scope exists is enough:

```c
#include <stdio.h>

void demo_function_scope(void) {
    goto cleanup;  // 跳到标签，标签在整个函数内可见

    {
        // 即使标签在嵌套块内声明，上面的 goto 也能找到它
        // （但这样写可读性很差，别这么干）
    }

cleanup:
    printf("Cleanup done.\n");
}
```

### Function Prototype Scope

This is the smallest scope—parameter names appearing in a function declaration (prototype) are only valid within the parentheses of that declaration, and cease to exist outside them. In practice, the compiler does not care about parameter names in prototypes (it only looks at the types), so this scope can basically be ignored:

```c
// name 只在这个声明的括号里有效，出了括号就没了
// 实际上你完全可以不写参数名
void greet(const char* name);

// 和上面完全等价
void greet(const char*);
```

## Step 2 — Understanding How Storage Duration Manages Lifetimes

Scope answers the question "where is a name visible?", while storage duration answers "when is data created, when is it destroyed, and where does it live?". C defines several storage class specifiers: `auto`, `static`, `extern`, `register`, and the C11 addition `_Thread_local`.

### auto: The Default Automatic Storage

`auto` is the default storage duration for local variables—writing `int x = 10;` inside a function is completely equivalent to writing `auto int x = 10;`. Because this is the default behavior, nobody explicitly writes `auto`, so you will basically never see it in real code. It means the variable is created when entering its block (allocated on the stack) and destroyed when leaving the block.

There is a point of confusion: C++11 repurposed `auto` as a type deduction keyword, which has nothing to do with the C language's `auto`. If you later write C++ code and see `auto x = 10;`, it tells the compiler to deduce the type of `x` as `int`, not any kind of storage duration.

### static: Living for the Entire Program

`static` is one of the keywords with the most meanings in C—it does completely different things depending on where it appears. Let's first look at its meaning as a storage class specifier—**changing a variable's lifetime from automatic to static**.

An ordinary local variable is reinitialized every time the function is entered, and disappears when the function leaves. But if you add `static` to a local variable, it is initialized only once when the program starts (if you don't provide an initial value, it is initialized to zero). Even after the function returns, the variable is not destroyed, and the next time you call the function, you can still see the previous value:

```c
#include <stdio.h>

void counter(void) {
    static int call_count = 0;  // 只初始化一次
    call_count++;
    printf("Called %d times\n", call_count);
}

int main(void) {
    counter();  // Called 1 times
    counter();  // Called 2 times
    counter();  // Called 3 times
    return 0;
}
```

Although this `call_count` looks like a "local variable," it is not stored on the stack—it lives in the Data Segment or BSS Segment, alongside global variables. The only difference is that its **scope** is still block scope; only the code inside the `counter` function can access it.

Why do this? Imagine you are writing a module that needs to maintain some internal state (such as a buffer, counter, or configuration info), but you don't want external code to touch this data directly. Using a `static` local variable achieves the perfect combination of "data persistence + restricted access"—a simple implementation of information hiding.

### extern: Declaring a Symbol Defined Elsewhere

`extern` tells the compiler "this variable/function is defined somewhere else; don't worry about where, the linker will find it." Its typical use case is sharing global variables in multi-file projects:

```c
// === config.c（定义） ===
#include "config.h"

int kMaxRetryCount = 3;  // 定义，分配内存
const char* kServerAddress = "192.168.1.100";
```

```c
// === config.h（声明） ===
#ifndef CONFIG_H
#define CONFIG_H

extern int kMaxRetryCount;  // 声明，不分配内存
extern const char* kServerAddress;

#endif
```

```c
// === main.c（使用） ===
#include <stdio.h>
#include "config.h"

int main(void) {
    printf("Server: %s, Retry: %d\n", kServerAddress, kMaxRetryCount);
    return 0;
}
```

The key distinction here is: a **definition** allocates memory and can only appear once; a **declaration** using `extern` means "it is defined elsewhere" and can appear multiple times. Putting declarations in header files and definitions in source files is the basic organizational pattern for multi-file C projects.

A common pitfall is writing it like this:

```c
// 头文件里
extern int kValue = 42;  // 千万别这么干！
```

If you assign an initial value to an `extern` declaration, the `extern` is ignored—this becomes a definition. If this header file is `#include` by multiple `.c` files, each translation unit will generate a definition for `kValue`, and you will get a `multiple definition` error at link time.

> ⚠️ **Watch Out**
> Putting `extern int kValue = 42;` in a header file is a typical mistake—an `extern` with an initial value equals a definition, and including the header multiple times will cause link conflicts. Remember: only put declarations (without initial values) in header files, and put definitions in `.c` files.

### register: A Historical Suggestion

`register` is a keyword from early C used to suggest to the compiler "put this variable in a register." On 1970s PDP-11 machines, compiler optimization capabilities were limited, and manually specifying `register` could genuinely improve performance.

But in front of modern compilers, this keyword is basically useless—GCC and Clang's optimizers know better than you do which variables should go in registers. In fact, even if you write `register`, the compiler is free to ignore it. Furthermore, you cannot take the address of a `register` variable (you cannot use `&` on it) because it might not even be in memory—this restriction can occasionally trip you up.

Just be aware of it; it is not recommended for modern code.

## Step 3 — Mastering Linkage to Control Symbol Visibility

Linkage describes the visibility of a name across different translation units. C defines three types of linkage: external linkage, internal linkage, and no linkage.

- Names with **external linkage** can be accessed by all translation units in the program. Ordinary global variables and functions have external linkage by default—as long as you declare them with `extern` in another file, you can use them.
- Names with **internal linkage** are only visible within the current translation unit; other files cannot find them even if they `extern` them. Adding `static` to a file-scope variable or function makes it internal linkage.
- Names with **no linkage** are only valid within their own scope—local variables, function parameters, and `typedef` in block scope all have no linkage.

The relationship between these three can be summarized in a table:

| Declaration Location | Keyword | Linkage | Scope | Lifetime |
| --- | --- | --- | --- | --- |
| Inside a function | (none) | None | Block | Automatic |
| Inside a function | `static` | None | Block | Static |
| Outside a function | (none) | External | File | Static |
| Outside a function | `static` | Internal | File | Static |
| Outside a function | `extern` | (Depends on first declaration) | File | Static |

This table is worth a few extra looks—note that `static` outside a function changes the linkage (from external to internal), not the scope or lifetime.

Let's feel how linkage works through a practical multi-file example:

```c
// === logger.c ===
#include <stdio.h>

// 内部链接——只有 logger.c 内部能用
static int log_count = 0;

// 内部链接的辅助函数
static void format_prefix(const char* level) {
    printf("[%s #%d] ", level, ++log_count);
}

// 外部链接——其他文件可以调用
void log_info(const char* message) {
    format_prefix("INFO");
    printf("%s\n", message);
}

void log_error(const char* message) {
    format_prefix("ERROR");
    printf("%s\n", message);
}
```

```c
// === logger.h ===
#ifndef LOGGER_H
#define LOGGER_H

void log_info(const char* message);
void log_error(const char* message);

// 注意：log_count 和 format_prefix 不出现在头文件里
// 它们是 logger.c 的内部实现细节

#endif
```

```c
// === main.c ===
#include "logger.h"

int main(void) {
    log_info("System starting");
    log_error("Something went wrong");
    log_info("Retrying...");
    return 0;
}
```

Compile and run:

```bash
gcc -Wall -Wextra -std=c11 -o logger_demo main.c logger.c && ./logger_demo
```

Output:

```text
[INFO #1] System starting
[ERROR #2] Something went wrong
[INFO #3] Retrying...
```

The `log_count` and `format_prefix` in `logger.c` are marked with `static` for internal linkage, meaning even if another file has a global variable also named `log_count`, there will be no conflict. This is the core value of `static` at the file level—**information hiding**, encapsulating a module's internal implementation details and only exposing the public interface through header files.

If you are curious about what happens without `static`—try defining a `int log_count = 0;` in two different `.c` files, and you will most likely see the linker report a `multiple definition of 'log_count'` during compilation. This is why global variables and helper functions that are not intended to be exposed externally must always have `static` added.

## Step 4 — Clarifying the Three Uses of static

Now that we understand scope and linkage, the final dimension is **lifetime** (storage duration)—the time span from an object's creation to its destruction. Lifetime is closely tied to the uses of static, so we will discuss them together.

> ⚠️ **Watch Out**
> You must not return a pointer to a local variable—after the function returns, that stack space is reclaimed, the pointer becomes a dangling pointer, and dereferencing it is undefined behavior. If you need to pass data between functions, either pass by value, use a `static` local variable, or allocate memory dynamically.

**Automatic lifetime** is the most common: ordinary local variables are created when entering their block and destroyed when leaving it. They are stored on the stack, and each time the function is called, the local variables are created once and gone after the function returns. This is also why you cannot return a pointer to a local variable—after the function returns, that stack space is reclaimed, the pointer becomes a dangling pointer, and dereferencing it is undefined behavior.

Objects with **static lifetime** exist from program startup and live until the program ends. This includes all file-scope variables (regardless of whether they have `static`), as well as local variables declared with `static` inside functions. They are stored in the Data Segment (if they have initial values) or the BSS Segment (if they don't, automatically initialized to zero).

Objects with **dynamic lifetime** are allocated on the heap via `malloc`/`calloc`/`realloc`, and are manually managed by the programmer—when to `free` and when to destroy. We will discuss this in detail in a later chapter on memory management.

```c
#include <stdio.h>
#include <stdlib.h>

int kGlobalVar = 10;             // 静态生命周期，数据段
static int kInternalVar = 20;    // 静态生命周期，数据段，内部链接
int kUninitialized;              // 静态生命周期，BSS 段，自动为 0

void demonstrate_lifetime(void) {
    int auto_var = 30;           // 自动生命周期，栈上
    static int static_var = 40;  // 静态生命周期，数据段

    int* heap_var = malloc(sizeof(int));  // 动态生命周期，堆上
    *heap_var = 50;

    printf("auto=%d, static=%d, heap=%d\n",
           auto_var, static_var, heap_var);

    free(heap_var);  // 手动销毁
    // auto_var 在函数返回时自动销毁
    // static_var 继续活着
}
```

An easily overlooked fact is: the initialization order of global variables is deterministic within the same translation unit (following the definition order), but the initialization order across translation units is **undefined**. For C, this is usually not a big problem (because global variables are typically initialized with constant expressions), but in C++ it is a famous pitfall—C++ allows global objects to have constructors, and the construction order across files is undefined. This is the so-called "static initialization order fiasco." We just need to know about it for now.

Since `static` has different meanings in different locations, let's do a complete summary.

**Use 1: Static local variables**—Inside a function, `static` gives a local variable static lifetime; the variable is not destroyed after the function returns, and the next call retains the previous value, but the scope remains block scope.

**Use 2: Static global variables**—Outside a function, `static` makes a global variable have internal linkage, invisible to other translation units. The scope is still file scope, the lifetime is still static, and the only thing that changes is the linkage.

**Use 3: Static functions**—Adding `static` to a function works the same way as static global variables; the function gets internal linkage and is only visible in the current translation unit.

Note that among these three uses, "static local variables" changes the lifetime (from automatic to static), while "static global variables" and "static functions" change the linkage (from external to internal). The same keyword does two different things—this is a historical design issue in C, but you get used to it after a while.

## C++ Connection

C++ has made quite a few enhancements and improvements on top of scope and storage duration.

Most notably, there are **namespaces**. In C, if you don't want file-level helper symbols exposed to the outside, the only tool is `static`—our `logger.c` earlier did exactly this. But C++ introduced `namespace`, providing a more structured way to organize symbols and avoid naming conflicts. Even better, C++17 introduced **`inline` variables**, eliminating the tedious pattern of needing `extern` paired with a source file definition for constants in header files:

```cpp
// C++17 的头文件——不需要配套的 .cpp 文件
#ifndef CONFIG_HPP
#define CONFIG_HPP

inline constexpr int kMaxRetryCount = 3;  // inline 允许多重定义
inline constexpr const char* kServerAddress = "192.168.1.100";

#endif
```

C++ **`static` class members** carry yet another semantic—they indicate that the member belongs to the class itself rather than to any particular instance, and all objects share the same copy. This is again different from C's `static`:

```cpp
class Counter {
public:
    static int count;  // 声明，所有 Counter 对象共享
    static void reset() { count = 0; }
};

int Counter::count = 0;  // 定义，在类外（C++17 可以用 inline static）
```

Additionally, C++ anonymous namespaces can completely replace file-level `static` usage, and they do it more thoroughly—symbols in an anonymous namespace are hidden from the outside, and they cannot even participate in template argument deduction. In C++ projects, using anonymous namespaces instead of `static` is recommended.

Finally, C++11's `thread_local` provides thread-local storage duration—each thread has its own independent copy of the variable. This is very useful in multithreaded programming. C11 also has a corresponding `_Thread_local`, but its compiler support and ease of use are not as good as C++.

## Summary

Scope, storage duration, and linkage together form the complete system of "name management" in C. Scope determines where a name is visible, storage duration determines how long data lives and where it resides, and linkage determines whether a name can be accessed across files.

`static` is the most confusing keyword in this system—inside a function it changes the lifetime, and outside a function it changes the linkage. But as long as you remember this distinction, you won't get mixed up again. `extern` is the tool for sharing global variables in multi-file projects, used in conjunction with the pattern of declarations in header files and definitions in source files.

In real projects, build a habit: **always add `static` to global variables and helper functions that are not intended to be exposed externally**. This is the most practical information-hiding mechanism at the C language level, and it can drastically reduce naming conflicts and unintended dependencies in multi-file projects.

### Key Takeaways

- [ ] C has four types of scope: block, file, function, and function prototype
- [ ] `static` local variables have static lifetime but block scope
- [ ] `static` global variables/functions have internal linkage and are invisible to other files
- [ ] `extern` declares a symbol that is defined elsewhere
- [ ] Global variables without `static` have external linkage and can be accessed from any file via `extern`
- [ ] Internally linked symbols do not conflict even if they share the same name across multiple files

## Exercises

### Exercise 1: Modular Counter

Design a simple module where the header file only exposes three functions: `counter_increment`, `counter_get`, and `counter_reset`. Internally, use a `static` variable to maintain the count. External code must not be able to directly access or modify this counter variable.

```c
// === counter.h ===
void counter_increment(void);
int counter_get(void);
void counter_reset(void);
```

Please implement `counter.c` yourself.

### Exercise 2: Multi-File Symbol Visibility

Create three files: `a.c`, `b.c`, and `main.c`. Requirements:

- `a.c` defines an external linkage global variable `int kSharedValue` with an initial value of `0`
- `a.c` defines an internal linkage helper function `static void helper_a(void)`
- `b.c` also defines a helper function with the same name, `static void helper_a(void)`, as internal linkage (no conflict!)
- `b.c` accesses `kSharedValue` via `extern` and provides a function to modify it
- `main.c` calls the functions provided by each module and verifies the results

```c
// a.h —— 请自行设计
// b.h —— 请自行设计
// 各 .c 文件的实现留给你
```

### Exercise 3: Lazy Initialization

Use a `static` local variable to implement a `get_config` function: on the first call, it performs initialization (prints "Initializing..." and sets a default value); on subsequent calls, it directly returns the already-initialized value without reinitializing.

```c
typedef struct {
    int max_connections;
    int timeout_ms;
    const char* server_name;
} Config;

const Config* get_config(void);
```

> Tip: A `static` local variable is only initialized the first time the function is entered—perfect for implementing "initialize exactly once" semantics.

## References

- [Storage duration specifiers - cppreference](https://en.cppreference.com/w/c/language/storage_duration)
- [Scope - cppreference](https://en.cppreference.com/w/c/language/scope)
- [Linkage - cppreference](https://en.cppreference.com/w/c/language/storage_duration#Linkage)
