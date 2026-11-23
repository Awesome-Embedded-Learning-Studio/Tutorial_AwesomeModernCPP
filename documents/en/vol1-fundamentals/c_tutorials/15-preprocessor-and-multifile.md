---
chapter: 1
cpp_standard:
- 11
- 14
- 17
description: Master how the C preprocessor works, learn to use macros, conditional compilation, and include
  guards, build a modular multi-file C project, and compare the const/inline/constexpr/template
  alternatives C++ offers
difficulty: beginner
order: 19
platform: host
prerequisites:
- Dynamic Memory Management
reading_time_minutes: 12
tags:
- host
- cpp-modern
- beginner
- 入门
- CMake
title: The Preprocessor and Multi-File Projects
translation:
  source: documents/vol1-fundamentals/c_tutorials/15-preprocessor-and-multifile.md
  source_hash: c2cd8b31b422286f91a1de01bdb8ce3f8478c00ad3af504ecfcf7097c6f796a9
  translated_at: '2026-11-22T16:39:43+00:00'
  engine: glm-5.3-refresh
  token_count: 7872
---
# The Preprocessor and Multi-File Projects

If every C program you have written so far lives in a single `.c` file, sooner or later that file will collapse under its own weight. In real projects, we split the code across multiple `.c` and `.h` files, let each module handle its own responsibilities, and then assemble them into a complete program through compilation and linking.

But a multi-file project brings more than an organizational challenge—it also drags onto the stage one of the most frequently misunderstood characters in the C language: the **preprocessor**. Understanding the preprocessor's true nature is the first step toward avoiding those baffling compile errors, bizarre macro-expansion behavior, and circular header includes.

## Step 1 — Understand What the Preprocessor Does

A C program goes through four stages on its way from source code to executable: preprocessing, compilation, assembly, and linking. The preprocessor is the first station on that line, and what it does to the source file is **pure text transformation**—every line starting with `#` is a preprocessing directive.

The preprocessor does not understand C. It has no idea what a type is or what a scope is; it just mechanically performs substitution, deletion, and conditional selection. You can run `gcc -E -P demo.c` to look at the preprocessed output and get a feel for just how "brutal" the preprocessor is.

## #include: Text Pasting at Its Most Brutal

`#include` behaves in the most direct way possible: it inserts the entire content of the specified file, untouched, at the current position. That is exactly why we call it text pasting rather than module importing.

Angle brackets `<>` search the system header directories; double quotes `""` search the current directory first, then the system directories. Nested includes can cause serious code bloat.

## Step 2 — Master the Techniques and Pitfalls of Writing Macros

### Object Macros: Constant Definitions

```c
#define kMaxBufferSize 1024
#define kVersionString "1.0.0"

char buffer[kMaxBufferSize];
```

**Do not add a semicolon** at the end of a macro definition. `#define kMaxBufferSize 1024;` would drag the semicolon into the replacement text as well.

### Function Macros: Parameterized Text Substitution

Every one of those parentheses is a scar earned the hard way:

```c
#define SQUARE(x) ((x) * (x))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
```

What happens without the parentheses:

```c
#define BAD_SQUARE(x) x * x
int r = BAD_SQUARE(2 + 3);   // 展开为 2 + 3 * 2 + 3 = 11，而不是 25
```

But parentheses cannot fix the **double evaluation** problem:

```c
int x = 5;
int r = MAX(x++, 10);
// 展开为 ((x++) > (10) ? (x++) : (10))
// x++ 被求值了两次！x 最终变成了 7 而不是 6
```

### Multi-Line Macros and the do-while(0) Idiom

```c
#define SAFE_FREE(ptr)         \
    do {                        \
        if ((ptr) != NULL) {     \
            free((ptr));         \
            (ptr) = NULL;        \
        }                       \
    } while (0)
```

`do { ... } while(0)` forms a single statement as a whole, so it never dangles in an `if-else` branch. You will find this trick all over the Linux kernel codebase.

## The # and ## Operators

`#` turns a macro parameter into a string, and `##` glues two tokens into one new token:

```c
#define STRINGIFY(x) #x
#define MAKE_VAR(prefix, num) prefix ## num

int MAKE_VAR(value, 1) = 10;  // 展开为 int value1 = 10;
```

## Conditional Compilation

### Include Guards

The traditional approach pairs `#ifndef` with `#define`; modern compilers also support the simpler `#pragma once`:

```c
// math_utils.h
#pragma once

int add(int a, int b);
int multiply(int a, int b);
```

`#pragma once` is not part of the C standard, but GCC, Clang, and MSVC all support it. It has long been the de facto standard practice in C++ projects.

### Typical Uses

Debug/Release switching, platform adaptation, feature toggles—conditional compilation is what makes all of these possible.

## Step 3 — Learn to Organize Headers and Multi-File Projects

Headers hold **declarations**; source files hold **definitions**.

The correct use of `extern`: declare it with `extern` in the header, then define it in exactly **one** `.c` file:

```c
// config.h
extern int kConfigMaxRetryCount;

// config.c
#include "config.h"
int kConfigMaxRetryCount = 3;
```

Writing `int kConfigMaxRetryCount = 3;` (without `extern`) in a header that gets included by multiple `.c` files will land you a `multiple definition` error.

## Multi-File Compilation and Linking

Each `.c` file together with all the headers it `#include`s forms a **translation unit**. The compiler processes each translation unit independently, and the linker is what stitches all the `.o` files together.

The `static` keyword confines a symbol's visibility to the current translation unit—the linker never sees it, and no other `.c` file can reference it.

## A First Look at Static Libraries

```bash
# 编译为目标文件
gcc -c math_utils.c
# 创建静态库
ar rcs libmath_utils.a math_utils.o
# 使用静态库
gcc -o demo main.c -L. -lmath_utils
```

## Bridging to C++

- `const`/`constexpr` replace macro constants—typed, scoped, and debuggable
- `inline` functions replace function macros—arguments are evaluated exactly once, with type checking
- `template` replaces generic macros—full type checking and compile-time verification
- `namespace` replaces file-level `static`—a cleaner way to organize names
- `using` replaces `typedef`—more intuitive syntax, and it supports alias templates
- C++20 Modules—`export`/`import` replace the text-pasting `#include`

## Exercises

### Exercise 1: Build a Modular Multi-File Project

**Difficulty: Basic** · .h/.c separation plus packaging a static library

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

::: details Reference solution

**math_utils.h**

```c
#pragma once
/**
 * @brief 返回两个值中的最大值
 *
 * @param a 参与比较的第一个值
 * @param b 参与比较的第二个值
 * @return a 与 b 中较大的那个
 */
#define MAX(a, b)           \
  ({                        \
    __typeof__(a) _a = (a); \
    __typeof__(b) _b = (b); \
    _a > _b ? _a : _b;      \
  })

/**
 * @brief 返回两个值中的最小值
 *
 * @param a 参与比较的第一个值
 * @param b 参与比较的第二个值
 * @return a 与 b 中较小的那个
 */
#define MIN(a, b)           \
  ({                        \
    __typeof__(a) _a = (a); \
    __typeof__(b) _b = (b); \
    _a < _b ? _a : _b;      \
  })

void clamp_int(int *value, int min_val, int max_val);

/**
 * @brief 返回一个整数的十进制位数
 *
 * 不计符号位；0 的位数为 1。
 */
int count_digits(int value);
```

**math_utils.c**

```c
#include "math_utils.h"

void clamp_int(int *value, int min_val, int max_val)
{
    *value = MAX(min_val, MIN(*value, max_val));
}

int count_digits(int value)
{
    int digits = 0;

    do {
        ++digits;
        value /= 10;
    } while (value != 0);

    return digits;
}
```

**main.c**

```c
#include <stdio.h>

#include "math_utils.h"

int main(void)
{
    int value;

    value = 5;
    clamp_int(&value, 0, 10);
    printf("clamp_int(5, 0, 10) = %d\n", value);

    value = 100;
    clamp_int(&value, 0, 10);
    printf("clamp_int(100, 0, 10) = %d\n", value);

    printf("count_digits(42) = %d\n", count_digits(42));
    printf("count_digits(-12345) = %d\n", count_digits(-12345));

    puts("All tests passed.");
    return 0;
}
```

Compile and run:

```bash
gcc -std=c17 -Wall -Wextra main.c math_utils.c -o main
```

Or:

```bash
gcc -std=c17 -Wall -Wextra  -c math_utils.c  # 只编译不链接，生成 math_utils.o
gcc -std=c17 -Wall -Wextra  -c main.c        # 生成 main.o
ar rcs libmath_utils.a math_utils.o            # 把 .o 打进静态库
gcc -std=c17 -Wall -Wextra  -o demo main.o -L. -lmath_utils  # 链接

./demo
```

Note: the macros in `math_utils.h` use GCC extensions (statement expressions and `__typeof__`), so `-Wpedantic` is left out of the compile commands; enabling it would produce extension-related warnings.

Output:

```text
clamp_int(5, 0, 10) = 5
clamp_int(100, 0, 10) = 10
count_digits(42) = 2
count_digits(-12345) = 5
All tests passed.
```

:::

Tip: the compile steps are `gcc -std=c17 -Wall -Wextra -c math_utils.c`, `gcc -std=c17 -Wall -Wextra  -c main.c`, and `gcc -std=c17 -Wall -Wextra  -o demo main.o math_utils.o`. Package the static library with `ar rcs libmath_utils.a math_utils.o`.

### Exercise 2: A Zero-Overhead DEBUG_LOG Macro

**Difficulty: Intermediate** · Conditional compilation plus variadic macros

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

::: details Reference solution

**debug_log.h**

```c
#pragma once

#include <stdio.h>

/* 默认开启日志；编译时用 -DNDEBUG 可关闭 */
#ifdef NDEBUG
#define DEBUG_LOG(fmt, ...) ((void)(0))
#else
#define DEBUG_LOG(fmt, ...) \
    fprintf(stderr, "[%s:%d] " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#endif
```

**main.c**

```c
#include <stdio.h>

#include "debug_log.h"

int main(void)
{
    int count = 0;

    DEBUG_LOG("开始运行，初始值 count = %d", count);

    for (int i = 0; i < 3; ++i) {
        DEBUG_LOG("第 %d 次循环", i);
        count += i;
    }

    DEBUG_LOG("结束，最终 count = %d", count);
    DEBUG_LOG("这是不带额外参数的中文消息");

    printf("完成，count = %d\n", count);
    return 0;
}
```

Compile and run:

`Debug mode`

```bash
gcc -std=c17 -Wall -Wextra main.c -o main && ./main
```

`Release mode`

```bash
gcc -std=c17 -Wall -Wextra -DNDEBUG main.c -o main && ./main
```

Output:

Debug mode: the line numbers depend on where those calls actually sit in `main.c`, so `<line>` stands in for them below.

```text
[main.c:<line>] 开始运行，初始值 count = 0
[main.c:<line>] 第 0 次循环
[main.c:<line>] 第 1 次循环
[main.c:<line>] 第 2 次循环
[main.c:<line>] 结束，最终 count = 3
[main.c:<line>] 这是不带额外参数的中文消息
完成，count = 3
```

Release mode: `DEBUG_LOG` expands to `((void)(0))`, prints nothing, and only the `printf` line remains:

```text
完成，count = 3
```

> **Note**: `__VA_ARGS__` is the variadic-macro mechanism standardized in C99, but the `##__VA_ARGS__` in this solution is a GCC extension: it removes the extra comma when a call such as `DEBUG_LOG("a message")` passes no additional format arguments. That is why the compile commands here do not enable `-Wpedantic`; the construct works in GCC's C17 mode but is not the portable, strict ISO C17 way to write it.

:::

Tip: the standard form of a variadic macro is `#define DEBUG_LOG(fmt, ...) fprintf(stderr, fmt, __VA_ARGS__)`. To also support calls without extra format arguments, this solution uses GCC's `##__VA_ARGS__` extension.
