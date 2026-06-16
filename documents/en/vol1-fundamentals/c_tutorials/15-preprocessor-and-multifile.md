---
chapter: 1
cpp_standard:
- 11
- 14
- 17
description: Master how the C preprocessor works, learn to use macros, conditional
  compilation, and header guards, build modular multi-file C projects, and compare
  them with C++ alternatives like const, inline, constexpr, and templates.
difficulty: beginner
order: 19
platform: host
prerequisites:
- 动态内存管理
reading_time_minutes: 6
tags:
- host
- cpp-modern
- beginner
- 入门
- CMake
title: Preprocessor and Multi-file Projects
translation:
  source: documents/vol1-fundamentals/c_tutorials/15-preprocessor-and-multifile.md
  source_hash: 8cf6998b6006211e44d63a45a5be41d4cf14f6d0a2b8a405cbd693c349e4bc29
  translated_at: '2026-06-16T05:51:05.048081+00:00'
  engine: anthropic
  token_count: 1128
---
# The Preprocessor and Multi-File Projects

If you have been writing all your C code in a single `.c` file up to this point, you will eventually hit a wall. In real-world projects, we split code into multiple `.c` and `.h` files, where each module handles its specific responsibilities. We then compile and link them to assemble the complete program.

However, multi-file projects bring more than just organizational challenges; they introduce a frequently misunderstood role in C—the **preprocessor**. Understanding the nature of the preprocessor is the first step in avoiding baffling compilation errors, strange macro expansion behaviors, and circular header inclusions.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Understand the role of the preprocessing stage in the four-stage compilation process.
> - [ ] Correctly use preprocessor directives such as `#include`, `#define`, and conditional compilation.
> - [ ] Master macro writing techniques and common pitfalls.
> - [ ] Organize header files using header guards and `#pragma once`.
> - [ ] Build multi-file C projects and understand compilation units and the linking process.
> - [ ] Compare C approaches with C++ alternatives like `const`, `inline`, `constexpr`, templates, and modules.

## Environment Setup

We will conduct all subsequent experiments in the following environment:

- Platform: Linux x86\_64 (WSL2 is also acceptable)
- Compiler: GCC 13+ or Clang 17+
- Compiler flags: `-Wall -Wextra -std=c17`

## Step One — Understanding What the Preprocessor Does

Transforming a C program from source code into an executable file involves four stages: preprocessing, compilation, assembly, and linking. The preprocessor is the first station; it performs **pure text transformation** on the source file—all lines starting with `#` are preprocessor directives.

The preprocessor does not understand the C language. It knows nothing about types or scopes; it mechanically performs replacements, deletions, and conditional selections. You can use `gcc -E -P demo.c` to inspect the preprocessor output and see how "brutal" it is.

## #include: The Most Brutal Text Pasting

The behavior of `#include` is very direct—it inserts the entire content of the specified file exactly where it is located. This is why we say it is text pasting, not module importing.

Angle brackets `<>` search within the system header directories, while double quotes `""` search the current directory first, then the system directories. Nested includes can lead to significant code bloat.

## Step Two — Mastering Macro Writing Techniques and Pitfalls

### Object-like Macros: Constant Definitions

```c
#define kMaxBufferSize 1024
#define kVersionString "1.0.0"

char buffer[kMaxBufferSize];
```

⚠️ **Do not add a semicolon** at the end of a macro definition. `#define kMaxBufferSize 1024;` includes the semicolon as part of the replacement text.

### Function-like Macros: Text Replacement with Parameters

Parentheses are the summary of lessons learned the hard way:

```c
#define SQUARE(x) ((x) * (x))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
```

# Consequences of omitting parentheses

```c
#define BAD_SQUARE(x) x * x
int r = BAD_SQUARE(2 + 3);   // 展开为 2 + 3 * 2 + 3 = 11，而不是 25
```

However, parentheses cannot solve the **repeated evaluation** problem:

```c
int x = 5;
int r = MAX(x++, 10);
// 展开为 ((x++) > (10) ? (x++) : (10))
// x++ 被求值了两次！x 最终变成了 7 而不是 6
```

### Multiline Macros and the do-while(0) Idiom

```c
#define SAFE_FREE(ptr)         \
    do {                        \
        if ((ptr) != NULL) {     \
            free((ptr));         \
            (ptr) = NULL;        \
        }                       \
    } while (0)
```

The `do { ... } while(0)` construct forms a single statement, preventing dangling `else` issues within `if-else` branches. This technique is ubiquitous throughout the Linux kernel codebase.

## # and ## Operators

`#` converts a macro parameter into a string, while `##` concatenates two tokens into a new token:

```c
#define STRINGIFY(x) #x
#define MAKE_VAR(prefix, num) prefix ## num

int MAKE_VAR(value, 1) = 10;  // 展开为 int value1 = 10;
```

## Conditional Compilation

### Header Guards

The traditional approach uses a combination of `#ifndef` and `#define`, while modern compilers support the more concise `#pragma once`:

```c
// math_utils.h
#pragma once

int add(int a, int b);
int multiply(int a, int b);
```

`#pragma once` is not part of the C standard, but GCC, Clang, and MSVC all support it. It has become the de facto standard practice in C++ projects.

### Typical Use Cases

Debug/Release switching, platform adaptation, and feature toggles all rely on conditional compilation.

## Step 3 — Learn to Organize Header Files and Multi-file Projects

Place **declarations** in header files, and **definitions** in source files.

Correct usage of `extern`: declare with `extern` in the header file, and define in **one** `.c` file:

```c
// config.h
extern int kConfigMaxRetryCount;

// config.c
#include "config.h"
int kConfigMaxRetryCount = 3;
```

⚠️ Writing `int kConfigMaxRetryCount = 3;` (without `extern`) in a header file and including it in multiple `.c` files will cause a `multiple definition` error.

## Multi-file Compilation and Linking

Each `.c` file, together with all the header files it `#include`s, constitutes a **compilation unit**. The compiler processes each compilation unit independently, and the linker is responsible for combining all the `.o` files.

The `static` keyword limits symbol visibility to the current compilation unit—the linker cannot see it, and other `.c` files cannot reference it.

## Introduction to Static Libraries

```bash
# 编译为目标文件
gcc -c math_utils.c
# 创建静态库
ar rcs libmath_utils.a math_utils.o
# 使用静态库
gcc -o demo main.c -L. -lmath_utils
```

## C++ Interoperability

- `const`/`constexpr` instead of macro constants—typed, scoped, and debuggable
- `inline` functions instead of function macros—parameters evaluated once, type-safe
- `template` instead of generic macros—full type checking and compile-time validation
- `namespace` instead of file-level `static`—clearer namespace organization
- `using` instead of `typedef`—more intuitive syntax, supports alias templates
- C++20 Modules—using `export`/`import` instead of the textual paste of `#include`

## Summary

Although the preprocessor is primitive, it remains an indispensable glue for multi-file projects in C. C++ gradually replaces preprocessor functionality with safer mechanisms like `constexpr`, `inline`, `template`, `namespace`, and Modules. Understanding the nature of the preprocessor allows us to understand why C++ implements these improvements.

## Exercises

### Exercise 1: Build a Multi-File Modular Project

```c
// math_utils.h
#pragma once
// 练习： 声明 clamp_int 和 count_digits

// math_utils.c
#include "math_utils.h"
// 练习： 实现 clamp_int（将 value 限制在 [min_val, max_val] 范围内）
// 练习： 实现 count_digits（计算整数的十进制位数）

// main.c
#include <stdio.h>
#include "math_utils.h"
int main(void) {
    // 练习： 调用两个函数，验证结果
    return 0;
}
```

> **Tip:** The compilation steps are `gcc -c math_utils.c`, `gcc -c main.c`, and `gcc -o demo main.o math_utils.o`. To package a static library, use `ar rcs libmath_utils.a math_utils.o`.

### Exercise 2: Zero-Overhead DEBUG_LOG Macro

```c
// debug_log.h
#pragma once

#ifdef NDEBUG
// 练习： Release 模式——DEBUG_LOG 展开为空
#else
// 练习： Debug 模式——输出 [DEBUG] 文件名:行号: 格式化消息
// 提示：使用 __FILE__、__LINE__、__VA_ARGS__
#endif
```

**Tip:** The syntax for variadic macros is `#define DEBUG_LOG(fmt, ...) fprintf(stderr, fmt, __VA_ARGS__)`. GCC provides the `##__VA_ARGS__` extension to handle the trailing comma when there are no additional arguments.
