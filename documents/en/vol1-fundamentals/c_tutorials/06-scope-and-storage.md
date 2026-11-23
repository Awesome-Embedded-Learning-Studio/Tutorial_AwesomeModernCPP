---
chapter: 1
cpp_standard:
- 11
description: Understand C scope rules, storage classes, and linkage in depth, and
  master the three distinct uses of `static`.
difficulty: beginner
order: 8
platform: host
prerequisites:
- 'Control Flow: Teaching Programs to Choose and Repeat'
reading_time_minutes: 20
tags:
- host
- cpp-modern
- beginner
- 入门
- 基础
title: Scope and Storage Classes
translation:
  source: documents/vol1-fundamentals/c_tutorials/06-scope-and-storage.md
  source_hash: 7585b8ee146fcdb4687036415e453e0e22ca428b632f0e8e760c5da41b07d2fd
  translated_at: '2026-11-22T16:39:42+00:00'
  engine: glm-5.3-refresh
  token_count: 14906
---
# Scope and Storage Classes

If you have written a project with more than two source files, you have almost certainly stepped on a rake like this: both files define a global variable named `count`, and at compile time the linker, utterly baffled, tells you `multiple definition`. Or the sneakier case — you define a helper function in some `.c` file, another file accidentally calls it too, and later, after you change that function's implementation, the caller crashes with zero warning.

The root of all this lies in **scope** and **storage classes**. The former determines which parts of the program a name can be used in; the latter decides how long the entity behind that name lives in memory and who gets to see it. These two concepts intertwine, and since the `static` keyword holds several different jobs at once in C, beginners mix them up easily.

Today we are going to untangle this mess — starting from the most basic scope rules, walking through storage classes, linkage, and lifetimes, and finishing with the question of what the three sharply different uses of `static` actually are. Once these click, you will stop organizing code in multi-file projects by gut feeling.

We use GCC 12+ or Clang 15+, compiling on Linux or WSL2. Every example can be built and run with one simple command:

```bash
gcc -Wall -Wextra -std=c11 -o scope_demo scope_demo.c && ./scope_demo
```

A multi-file project needs its files compiled separately and then linked — or you can just throw everything in at once:

```bash
gcc -Wall -Wextra -std=c11 -o multi_file_demo file1.c file2.c && ./multi_file_demo
```

## Step 1 — Sorting Out the Four Kinds of Scope

The C standard defines four kinds of scope: block scope, file scope, function scope, and function prototype scope. Let's take them one at a time.

### Block Scope

Block scope is the most common — the region enclosed by curly braces `{}` is a block, and variables declared inside a block are visible only within that block (and its nested sub-blocks). The body of an `if`, a `for`, or a `while`, or even a pair of braces you dash off casually, each creates a new block scope:

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

One point worth noting: an inner block can **shadow** an outer block's variable of the same name — the inner `x` temporarily "covers" the outer `x` until the inner block ends:

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

Since C99, the initialization clause of a `for` loop can also declare variables; that variable's scope is the entire loop (including the body and the condition part), and it is invisible outside the loop. This matches C++ behavior, but if you are stuck with an ancient C89 compiler (unlikely these days), loop variables must be declared outside the loop.

### File Scope

Variables and functions declared outside all functions have file scope — they are visible from the point of declaration all the way to the end of the current translation unit (that is, the `.c` file plus everything it pulls in through `#include`). By convention we call these "global variables," but their visibility is not truly "global" — whether other translation units can see them depends on linkage, which we will cover in detail later:

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

This scope is a bit special: it **applies only to labels** — the colon-bearing names that serve as the jump targets of `goto`. A label is visible throughout the entire function it lives in, no matter at which nesting level it is declared. Honestly, since you will probably barely ever use `goto`, just knowing this scope exists is enough:

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

This is the smallest scope of all — a parameter name appearing in a function declaration (prototype) is valid only within the parentheses of that declaration; outside the parentheses it no longer exists. In practice the compiler does not care at all about parameter names in a prototype (it looks only at the types), so you can basically ignore this scope:

```c
// name 只在这个声明的括号里有效，出了括号就没了
// 实际上你完全可以不写参数名
void greet(const char* name);

// 和上面完全等价
void greet(const char*);
```

## Step 2 — Understanding How Storage Classes Manage Lifetimes

Scope answers "where is a name visible," while storage classes answer "when is data created, when is it destroyed, and where does it live." C defines several storage-class specifiers: `auto`, `static`, `extern`, `register`, plus `_Thread_local`, added in C11.

### auto: Default Automatic Storage

`auto` is the default storage class for local variables — writing `int x = 10;` inside a function is exactly equivalent to writing `auto int x = 10;`. Since this is the default behavior, nobody writes `auto` explicitly, so you will basically never see it in real code. It means the variable is created when execution enters its block (allocated on the stack) and destroyed when the block is left.

One easy-to-confuse point: C++11 repurposed `auto` as a type-deduction keyword, which has nothing to do with C's `auto`. When you later write C++ and see `auto x = 10;`, that is asking the compiler to deduce `x` as `int` — not a storage class at all.

### static: Persisting Through the Entire Program

`static` is one of the most heavily loaded keywords in C; it does completely different things depending on where it appears. Let's look first at its meaning as a storage-class specifier — **changing a variable's lifetime from automatic to static**.

An ordinary local variable is re-initialized every time execution enters the function and disappears when the function is left. But add `static` to a local variable, and it is initialized exactly once at program startup (zero-initialized if you provide no initial value). From then on, even after the function returns, the variable is never destroyed; the next call still sees the value left over from the previous one:

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

Although this `call_count` looks like a "local variable," it does not live on the stack — it is stored in the data segment or the BSS segment, right alongside the global variables. The only difference is that its **scope** is still block scope: only the inside of `counter` can access it.

Why would you do this? Imagine you are writing a module that needs to maintain some internal state (a buffer, a counter, some configuration), but you do not want outside code touching that data directly. A `static` local variable gives you the perfect combination of "data persists + access restricted" — a homespun form of information hiding.

### extern: Declaring Symbols Defined Elsewhere

`extern` tells the compiler: "this variable/function is defined somewhere else — don't worry about where just now; the linker will find it." Its typical use is sharing global variables across a multi-file project:

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

The key distinction here: a **definition** allocates memory and may appear only once; a **declaration** uses `extern` to say "it is defined elsewhere" and may appear many times. Declarations in headers, definitions in source files — that is the basic organizational pattern of a C multi-file project.

A common pitfall looks like this:

```c
// 头文件里
extern int kValue = 42;  // 千万别这么干！
```

If you attach an initial value to an `extern` declaration, the `extern` is ignored — this becomes a definition. If that header is `#include`d by several `.c` files, every translation unit emits its own definition of `kValue`, and at link time you will receive a `multiple definition` error.

Putting `extern int kValue = 42;` in a header is the classic wrong way to write it — an `extern` with an initializer is a definition, and a header included multiple times leads to link conflicts. Remember: headers carry declarations only (no initializers); definitions go in `.c` files.

### register: A Legacy Suggestion

`register` is early C's keyword for suggesting to the compiler "keep this variable in a register." On a PDP-11 in the 1970s, with compilers whose optimization was limited, a programmer manually tagging `register` really could improve performance.

In the face of modern compilers, however, this keyword is essentially useless — GCC's and Clang's optimizers know far better than you which variables belong in registers. In fact, you can write `register` and the compiler is entirely free to ignore it. What's more, you cannot take the address of a `register` variable (no `&` on it), because it might not live in memory at all — a restriction that occasionally bites.

Just know it exists; it is not recommended in modern code.

## Step 3 — Mastering Linkage to Control Symbol Visibility

Linkage describes a name's visibility across translation units. C defines three kinds of linkage: external linkage, internal linkage, and no linkage.

- A name with **external linkage** can be accessed from every translation unit in the program. Ordinary global variables and functions are external-linkage by default — declare them with `extern` in another file and you are good to go.
- A name with **internal linkage** is visible only within the current translation unit; other files cannot find it even with `extern`. Adding `static` to a file-scope variable or function makes it internal-linkage.
- A name with **no linkage** is valid only inside its own scope — local variables, function parameters, and block-scope `typedef`s all have no linkage.

The relationship among the three can be summed up in a table:

| Where Declared | Keyword | Linkage | Scope | Lifetime |
| -------------- | ------- | ------------------ | ----- | -------- |
| Inside a function | (none) | None | Block | Automatic |
| Inside a function | `static` | None | Block | Static |
| Outside functions | (none) | External | File | Static |
| Outside functions | `static` | Internal | File | Static |
| Outside functions | `extern` | (depends on the first declaration) | File | Static |

This table deserves a few extra glances — note that `static` outside a function changes linkage (from external to internal), not scope or lifetime.

Let's get a feel for how linkage works through a hands-on multi-file example:

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

In `logger.c`, `log_count` and `format_prefix` are marked `static` for internal linkage, which means that even if another file also has a global variable named `log_count`, there is no conflict. This is the core value of `static` at the file level — **information hiding**: encapsulate the module's internal implementation details and expose only the public interface through the header.

Curious what happens without `static`? Try defining `int log_count = 0;` in two different `.c` files; odds are the linker will report `multiple definition of 'log_count'` at build time. That is why global variables and helper functions you do not intend to expose must get `static`.

## Step 4 — Untangling the Three Uses of static

With scope and linkage under our belt, the last dimension is **lifetime** (storage duration) — the span from an object's creation to its destruction. Lifetime is inseparable from the uses of `static`, so we cover them together.

Never return a pointer to a local variable — once the function returns, that stack space is reclaimed, the pointer dangles, and dereferencing it is undefined behavior. If you need to move data between functions, pass by value, use a `static` local variable, or allocate memory dynamically.

**Automatic lifetime** is the most common: an ordinary local variable is created when its block is entered and destroyed when the block is left. They live on the stack; every call creates the locals afresh, and they are gone after the return. This is also why you cannot return a pointer to a local variable — once the function returns, that stack space is reclaimed, the pointer becomes a dangling pointer, and dereferencing it is undefined behavior.

**Static lifetime** objects exist from program startup and live until the program ends. That includes every file-scope variable (with or without `static`) as well as locals declared `static` inside functions. They live in the data segment (those with an initial value) or the BSS segment (those without, automatically zero-initialized).

**Dynamic lifetime** objects are allocated on the heap via `malloc`/`calloc`/`realloc` and managed by hand — destroyed when the programmer calls `free`. We will discuss this in detail in the memory-management chapters later.

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

An easily overlooked fact: the initialization order of global variables is well-defined within a single translation unit (in definition order), but **undefined** across translation units. For C this is usually not a big deal (global variables are generally initialized with constant expressions), but in C++ it is a famous pitfall — C++ allows global objects to have constructors, and the cross-file construction order is undefined: the so-called "static initialization order fiasco." For now it is enough to simply know this exists.

Since `static` means different things in different places, let's do a complete summary.

**Usage 1: static local variables** — inside a function, `static` gives a local variable static lifetime: the variable is not destroyed when the function returns and keeps its value for the next call, while its scope remains block scope.

**Usage 2: static global variables** — outside a function, `static` makes a global variable internal-linkage, invisible to other translation units. Its scope remains file scope and its lifetime remains static; the only thing that changes is linkage.

**Usage 3: static functions** — adding `static` to a function works like the static global variable: the function becomes internal-linkage, visible only in the current translation unit.

Note that among these three uses, the "static local variable" changes lifetime (from automatic to static), while the "static global variable" and "static function" change linkage (from external to internal). One keyword doing two different jobs is a historical leftover in C's design, but you get used to it after a while.

## Bridging to C++

C++ builds quite a few enhancements and improvements on top of scope and storage classes.

The most noteworthy is **namespaces**. In C, if you don't want file-level helper symbols exposed to the outside world, your only tool is `static` — that is exactly what our `logger.c` did earlier. C++ introduced `namespace`, a more structured way to organize symbols and avoid naming conflicts. Even better, C++17 introduced **`inline` variables**, so a constant definition in a header no longer needs the clunky pattern of an `extern` declaration paired with a definition in a source file:

```cpp
// C++17 的头文件——不需要配套的 .cpp 文件
#ifndef CONFIG_HPP
#define CONFIG_HPP

inline constexpr int kMaxRetryCount = 3;  // inline 允许多重定义
inline constexpr const char* kServerAddress = "192.168.1.100";

#endif
```

C++ **`static` class members** are yet another semantics — the member belongs to the class itself rather than to one instance of the class, and all objects share a single copy. Again, not the same thing as C's `static`:

```cpp
class Counter {
public:
    static int count;  // 声明，所有 Counter 对象共享
    static void reset() { count = 0; }
};

int Counter::count = 0;  // 定义，在类外（C++17 可以用 inline static）
```

Additionally, C++'s anonymous namespaces can replace file-level `static`, and they reach further: `static` can only decorate variables and functions — try to give a type internal linkage with it and the compiler rejects you outright — whereas an anonymous namespace tucks the types inside it into the current translation unit as well. Symbols inside work just as usual within this file, template argument deduction unaffected; other `.cpp` files simply cannot refer to them. So in C++ projects, for functions, variables, and types used in only a single source file, an anonymous namespace is recommended; as for function-local static variables and class static members, those are the other two semantics — where `static` is the right tool, keep using it.

```cpp
// 某个 .cpp 文件内部——类型也一起藏进本翻译单元
namespace {
struct Config {          // 想给它加内部链接？static 做不到，匿名命名空间可以
    int retries;
};

template <class T>
void dump(const T&) {}
}  // namespace

void use() {
    Config c{3};
    dump(c);             // 正常推导出 T = Config，内部链接不影响模板
}
```

Finally, C++11's `thread_local` provides thread-level storage duration — each thread gets its own independent copy of the variable. This is extremely useful in multithreaded programming. C11 has the corresponding `_Thread_local`, but its support and usability both fall short of C++'s.

## Exercises

### Exercise 1: A Modular Counter

**Difficulty: Basic** · hide data with file-level internal linkage via static

Design a simple module whose header exposes exactly three functions — `counter_increment`, `counter_get`, and `counter_reset` — with a `static` variable inside maintaining the count. Outside code must not be able to access or modify the counter variable directly.

```c
// === counter.h ===
void counter_increment(void);
int counter_get(void);
void counter_reset(void);
```

Implement `counter.c` yourself.

::: details Reference answer

main.c

```c
#include <stdio.h>
#include "counter.h"

int main(void) {
    printf("%d\n",counter_get());   //输出应当是0
    counter_increment();
    printf("%d\n",counter_get());   //输出应当是1
    counter_increment();
    printf("%d\n",counter_get());   //输出应当是2
    counter_reset();
    printf("%d\n",counter_get());   //输出应当是0
    return 0;
}
```

counter.h

```c
#ifndef MODERNCPP_PRE8_1_COUNTER_H
#define MODERNCPP_PRE8_1_COUNTER_H

void counter_increment(void);
int counter_get(void);
void counter_reset(void);

#endif //MODERNCPP_PRE8_1_COUNTER_H
```

counter.c

```c
#include "counter.h"

static int counter = 0;
void counter_increment(void) {
    counter++;
}

void counter_reset(void) {
    counter = 0;
}

int counter_get(void) {
    return counter;
}
```

:::

### Exercise 2: Multi-File Symbol Visibility

**Difficulty: Intermediate** · external linkage, internal linkage, and extern combined

Create three files, `a.c`, `b.c`, and `main.c`. Requirements:

- `a.c` defines an external-linkage global variable `int kSharedValue`, initial value `0`
- `a.c` defines an internal-linkage helper function `static void helper_a(void)`
- `b.c` also defines a same-named internal-linkage helper function `static void helper_a(void)` (no conflict!)
- `b.c` accesses `kSharedValue` through `extern` and provides a function that modifies it
- `main.c` calls the functions each module provides and verifies the results

```c
// a.h —— 请自行设计
// b.h —— 请自行设计
// 各 .c 文件的实现留给你
```

::: details Reference answer

main.c

```c
#include <stdio.h>
#include "a.h"
#include "b.h"

int main(void) {

    a_greet();                      // 调用 a 模块的公共函数，触发它内部的 helper_a
    printf("%d\n",kSharedValue);    //输出应当是0
    set_kSharedValue(100);          // 内部会调用 b 模块自己的 helper_a
    printf("%d\n",kSharedValue);    //输出应当是100

    return 0;
}
```

a.h

```c
#ifndef MODERNCPP_PRE8_2_A_H
#define MODERNCPP_PRE8_2_A_H

extern int kSharedValue;
void a_greet(void);

#endif //MODERNCPP_PRE8_2_A_H
```

b.h

```c
#ifndef MODERNCPP_PRE8_2_B_H
#define MODERNCPP_PRE8_2_B_H

void set_kSharedValue(int value);

#endif //MODERNCPP_PRE8_2_B_H
```

a.c

```c
#include <stdio.h>
#include "a.h"

int kSharedValue = 0;
static void helper_a(void) {
    printf("need help?\n");
}

// a.c 暴露的公共函数，内部调用文件私有的 helper_a
void a_greet(void) {
    helper_a();
}
```

b.c

```c
#include <stdio.h>
#include "b.h"
#include "a.h"

static void helper_a(void) {
    printf("need help?\n");
}
void set_kSharedValue(int value) {
    helper_a();
    kSharedValue = value;
}
```

:::

### Exercise 3: Call Counter

**Difficulty: Basic** · keep state between function calls with a static local variable

Implement a `call_count(void)`: each time it is called, it returns "which call this is." Exploit the `static` local variable's property that its value is not destroyed when the function returns.

```c
/// @return 这是第几次调用本函数（第一次调用返回 1）
int call_count(void);
```

Hint: declare `static int n = 0;` inside the function, do `++n`, then return. Then think some more: if you swapped it for a plain local variable `int n = 0;` (dropping the static), what would the result become, and why?

::: details Reference answer

```c
#include <stdio.h>

int call_count(void) {
    static int n = 0;   // 只初始化一次，值在函数返回后依然保留
    ++n;
    return n;
}

int main(void) {
    printf("%d\n", call_count());   // 1
    printf("%d\n", call_count());   // 2
    printf("%d\n", call_count());   // 3
    return 0;
}
```

Drop the `static` and `n` gets re-initialized to 0 on every entry; after `++n` it returns 1, so no matter how many times you call it, it only ever prints 1 — you have lost the ability to "remember the last result."

While we're at it, let's clear up an easy-to-confuse point: `static int n = 0;` is initialized when the program starts (not when `call_count` is first called), and it happens exactly once over the variable's entire lifetime. Precisely because it is initialized only once and its value persists afterwards, it can serve as a counter.

:::

## Reference Resources

- [Storage class specifiers - cppreference](https://en.cppreference.com/w/c/language/storage_duration)
- [Scope - cppreference](https://en.cppreference.com/w/c/language/scope)
- [Linkage - cppreference](https://en.cppreference.com/w/c/language/storage_duration#Linkage)
