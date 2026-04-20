---
title: Program Structure and Compilation Basics
description: Understand the basic structure of C programs, the four-stage compilation
  process, the header file mechanism, and basic I/O, laying the foundation of the
  compilation model for subsequent C++ learning.
chapter: 1
order: 1
tags:
- host
- cpp-modern
- beginner
- 入门
difficulty: beginner
platform: host
reading_time_minutes: 20
cpp_standard:
- 11
prerequisites:
- 无（本系列第一篇）
translation:
  source: documents/vol1-fundamentals/c_tutorials/01-program-structure-and-compilation.md
  source_hash: c08bd273b728d1f016a330164f09b6da4461347309537a306426b1bf9d6efbd1
  translated_at: '2026-04-20T03:11:17.080931+00:00'
  engine: anthropic
  token_count: 2334
---
# Program Structure and Compilation Basics

If you have written some C code before, chances are you simply clicked "Run" in an IDE and called it a day. You might have never thought about how a source file actually becomes a runnable binary. But honestly, understanding the compilation model becomes crucial when learning C++ later: template instantiation, header file strategies, and the one definition rule (ODR) are all basically black-box magic if you don't grasp the basic compilation workflow. So let's clear this up right from the start.

> **Learning Objectives**
>
> - After completing this chapter, you will be able to:
> - [ ] Understand the basic structure of a C program (`main` function, header file inclusion)
> - [ ] Master the principles and manual execution of the four compilation stages
> - [ ] Understand the header file search mechanism and the difference between `< >` vs `" "`
> - [ ] Become proficient with common `printf`/`scanf` format specifiers
> - [ ] Independently compile and link a multi-file program

## Environment Setup

All commands and code in this article have been verified under the following environment:

- **Operating System**: Linux (Ubuntu 22.04+) / WSL2 / macOS
- **Compiler**: GCC 11+ (confirm the version via `gcc --version`)
- **Compiler flags**: `-Wall -Wextra -std=c11` (enable warnings, specify C11 standard)
- **Auxiliary Tools**: `objdump`, `readelf` (bundled with GCC, used to inspect object files)

If you use Windows without WSL, MinGW-w64 or MSVC can also compile and run the code, but the output format of some tool commands (like `objdump`, `readelf`) will differ.

## Step 1 — Understanding the Skeleton of a C Program

The entry point of a C program is always the `main` function. This isn't just a convention—it's mandated by the C standard. The C standard defines two legal `main` signatures:

```c
// 无命令行参数版本
int main(void) {
    return 0;
}

// 带命令行参数版本
int main(int argc, char *argv[]) {
    // argc: 参数个数（至少为 1，即程序自身）
    // argv: 参数字符串数组，argv[0] 是程序名
    return 0;
}
```

The return type of `main` must be `int`—on some older compilers, writing `void main()` might work, but that is non-standard behavior. A return value of `0` indicates normal exit, while a non-zero value indicates an error. The shell retrieves this value via `$?` to determine whether the program executed successfully.

> ⚠️ **Pitfall Warning**: Do not use `void main()`. Although some older compilers accept it, the C standard only recognizes `int main()`. On Linux, shell scripts and CI/CD pipelines frequently obtain a program's return value via `$?`—if your `main` doesn't return a meaningful value, upstream logic might break.

`int argc` and `char *argv[]` allow the program to receive external parameters at startup. For example, if you run `./program hello world`, then `argc` is 3, `argv[0]` is `./program`, `argv[1]` is `hello`, and `argv[2]` is `world`.

A minimal, complete C program:

```c
#include <stdio.h>

int main(void) {
    printf("Hello, World!\n");
    return 0;
}
```

Output:

```text
Hello, World!
```

The first line, `#include <stdio.h>`, is a preprocessor directive that inserts the contents of the standard I/O library header file directly into the current position. Without including this header, the compiler doesn't know what `printf` is and will issue a warning or an error.

## Step 2 — Breaking Down the Four Stages of Compilation

Now let's break down how a `.c` file becomes an executable. The entire process is divided into four stages: preprocessing → compilation → assembly → linking. We can use GCC options to manually trigger each stage and observe the intermediate artifacts.

### Stage 1: Preprocessing

The preprocessor handles all directives starting with `#`—expanding macros, inserting header file contents, and processing conditional compilation:

```bash
# 只运行预处理，输出到文件方便查看
gcc -E hello.c -o hello.i
```

The preprocessed `.i` file will be very large—a single `#include <stdio.h>` expands the entire standard I/O header along with all indirectly included headers. If you open the `.i` file, the first few lines are comments, followed by hundreds or thousands of lines of header content, with your own code appearing only at the very end.

What the preprocessor does sounds simple—pure text substitution—but this mechanism is a major source of C's flexibility and forms the foundation for understanding C++ templates and header file organization.

### Stage 2: Compilation

The compiler translates the preprocessed C code into assembly code, going through lexical analysis, syntax analysis, semantic analysis, intermediate code generation, and optimization:

```bash
gcc -S hello.c -o hello.s
```

Opening the `.s` file, you will see x86-64 assembly similar to this (output varies by platform):

```asm
    .file   "hello.c"
    .section .rodata
.LC0:
    .string "Hello, World!"
    .text
    .globl  main
main:
    pushq   %rbp
    movq    %rsp, %rbp
    leaq    .LC0(%rip), %rdi
    call    puts@PLT
    movl    $0, %eax
    popq    %rbp
    ret
```

Here's an interesting detail: our `printf` call was optimized by the compiler into a `puts` call—because the format string contains only a single string ending with `\n` and has no format placeholders, the compiler knows `puts` is more efficient and substitutes it directly.

### Stage 3: Assembly

The assembler translates assembly code into machine code, generating an object file:

```bash
gcc -c hello.c -o hello.o
```

The `.o` file is in a binary format (ELF on Linux) containing machine instructions, a symbol table, and relocation information. You can use `objdump -d` to view the disassembly and `nm` to view the symbol table:

```bash
objdump -d hello.o    # 反汇编查看
nm hello.o            # 查看符号表
```

Function calls within the object file (such as the call to `printf`) still have empty addresses at this point, waiting for the linking stage to fill them in.

### Stage 4: Linking

The linker combines one or more object files along with required library files into the final executable, resolving all external symbol references:

```bash
# 完整编译（四阶段一步到位）
gcc hello.c -o hello

# 也可以分步
gcc -c hello.c -o hello.o
gcc hello.o -o hello
```

This stage is key to understanding multi-file programming. Each `.c` file is first independently compiled into an `.o` file, and then the linker assembles them together. This separate compilation model is a core design of C/C++—it allows us to recompile only the modified files without rebuilding the entire project.

### Compilation Pipeline Summary

```text
hello.c → [预处理] → hello.i → [编译] → hello.s → [汇编] → hello.o → [链接] → hello
              ↑                                              ↑
         #include 展开                                   合并 .o + 库
         #define 替换                                    解析外部符号
         条件编译                                        生成可执行文件
```

## Step 3 — Understanding How Header Files Work

`#include` has two syntax forms, which use different search paths:

```c
#include <stdio.h>    // 尖括号：只在系统/标准库目录搜索
#include "myheader.h" // 引号：先搜索当前文件所在目录，找不到再搜索系统目录
```

The logic is intuitive—angle brackets are for "system-provided" items, while quotes are for "your own" items. The compiler has a set of default search paths (viewable with `gcc -xc -E -v -`), and the `-I` option can add extra search paths.

Header files typically contain function declarations (prototypes), type definitions (`struct`/`typedef`), macro definitions, and external variable declarations (`extern`). A header file is the "contract" for communication between modules—it tells the caller "what this module provides" without exposing implementation details. In C++, this idea is more elegantly implemented through the `class` public/private mechanism.

Every header file should have an include guard to prevent multiple inclusion:

```c
#ifndef MYHEADER_H
#define MYHEADER_H

// 头文件内容

#endif /* MYHEADER_H */
```

Or use `#pragma once`:

```c
#pragma once

// 头文件内容
```

> ⚠️ **Pitfall Warning**: Although `#pragma once` is concise, it may have compatibility issues in certain edge cases (symbolic links, network path mappings). Just pick one approach and keep it consistent across your project—if you're unsure, use the traditional `#ifndef` approach, as it is guaranteed by the standard.

## Step 4 — Getting Hands-On with Basic I/O

### Formatted Output with printf

`printf` is the most commonly used output function in the C standard library, and its format string supports a rich set of format specifiers:

```c
#include <stdio.h>

int main(void) {
    int i = 42;
    unsigned int u = 0xDEAD;
    double f = 3.14159265359;
    const char* s = "Hello";
    int* p = &i;

    printf("整数: %d\n", i);             // 十进制：42
    printf("十六进制: %x / %X\n", u, u); // 小写 dead / 大写 DEAD
    printf("浮点: %f\n", f);             // 默认 6 位小数：3.141593
    printf("浮点精度: %.2f\n", f);       // 2 位小数：3.14
    printf("字符串: %s\n", s);           // Hello
    printf("指针: %p\n", (void*)p);      // 指针地址

    // 宽度与对齐
    printf("[%10d]\n", i);    // 右对齐宽度 10：[        42]
    printf("[%-10d]\n", i);   // 左对齐宽度 10：[42        ]
    printf("[%010d]\n", i);   // 前导零填充：[0000000042]
    return 0;
}
```

Output:

```text
整数: 42
十六进制: dead / DEAD
浮点: 3.141593
浮点精度: 3.14
字符串: Hello
指针: 0x7ffd12345678
[        42]
[42        ]
[0000000042]
```

An often-overlooked detail: the return value of `printf` is the number of characters successfully output, with a negative value indicating an error. In embedded development, using the return value for simple error checking can sometimes be useful.

### Reading User Input with scanf

`scanf` reads data from standard input. Its format specifiers are similar to `printf`'s but have some subtle differences:

```c
int age;
float weight;
char name[32];

printf("请输入姓名 年龄 体重: ");
scanf("%31s %d %f", name, &age, &weight);

// name 是数组，不需要 &（数组名即地址）
// age 和 weight 是普通变量，必须传地址
```

> ⚠️ **Pitfall Warning**: `scanf`'s `%s` stops when it encounters whitespace and does not check buffer size. If the input exceeds the buffer length, it directly causes a buffer overflow. The safe approach is to specify a maximum length (like `%9s`), or use the `fgets` + `sscanf` combination instead. In real-world projects, `scanf` is rarely used, but understanding its mechanism is still important during the learning phase.

## Step 5 — Building a Multi-File Project

Let's build a simple multi-file project to experience the benefits of separate compilation. The project structure is as follows:

```text
calc/
├── main.c      // 主程序
├── math_ops.h  // 数学运算函数声明
└── math_ops.c  // 数学运算函数实现
```

**math_ops.h** — The header file, the module's "public interface":

```c
#ifndef MATH_OPS_H
#define MATH_OPS_H

int add(int a, int b);
int subtract(int a, int b);
int multiply(int a, int b);
float divide(int a, int b);

#endif /* MATH_OPS_H */
```

**math_ops.c** — The implementation file:

```c
#include "math_ops.h"

int add(int a, int b) { return a + b; }
int subtract(int a, int b) { return a - b; }
int multiply(int a, int b) { return a * b; }

float divide(int a, int b) {
    if (b == 0) {
        return 0.0f;
    }
    return (float)a / (float)b;
}
```

**main.c** — The main program:

```c
#include <stdio.h>
#include "math_ops.h"

int main(void) {
    int x = 10, y = 3;
    printf("%d + %d = %d\n", x, y, add(x, y));
    printf("%d - %d = %d\n", x, y, subtract(x, y));
    printf("%d * %d = %d\n", x, y, multiply(x, y));
    printf("%d / %d = %.2f\n", x, y, divide(x, y));
    return 0;
}
```

Compile and run:

```bash
# 分别编译各源文件为目标文件，再链接
gcc -c main.c -o main.o
gcc -c math_ops.c -o math_ops.o
gcc main.o math_ops.o -o calc
./calc
```

Output:

```text
10 + 3 = 13
10 - 3 = 7
10 * 3 = 30
10 / 3 = 3.33
```

This step-by-step compilation pattern is very useful. When you modify `math_ops.c` but leave the header file and `main.c` untouched, you only need to recompile `math_ops.c` and relink—build tools like `Make` and `CMake` essentially automate this process.

## Bridging to C++

C++ retains the same separate compilation model but adds more complex mechanisms. Header files remain the primary modularization tool in C++ (until C++20 Modules arrived), but C++ templates introduce a new problem—template code usually must be written in header files because the compiler needs to see the complete definition to perform template instantiation. Understanding the compilation model is important precisely because template instantiation happens at the compilation stage, and the linker only sees already-instantiated symbols.

C++ recommends using header files without the `.h` suffix (such as `<cstdio>` instead of `<stdio.h>`), which place C library functions into the `std` namespace. `std::cout` provides type-safe I/O, but `printf` is typically faster performance-wise—because it lacks the locale overhead, virtual function calls, and formatting object construction costs of `std::cout`. In performance-sensitive embedded scenarios, C-style `printf`/`scanf` remains the better choice.

The one definition rule (ODR) is the core rule of the C++ linking model: an entity can have only one definition across the entire program. Violating the ODR also causes problems in C, but C++ templates, inline functions, and `inline` variables make this issue much more prominent—we will discuss this in detail in later C++ chapters.

## Common Compilation Errors Quick Reference

| Error Message | Cause | Solution |
|---------------|-------|----------|
| `undefined reference to 'xxx'` | Function definition not found during linking | Check if you forgot to link the `.o` file or library |
| `implicit declaration of function` | Used an undeclared function | Add the corresponding `#include` or function declaration |
| `redefinition of 'xxx'` | The same symbol is defined multiple times | Check if the header file is missing an include guard |
| `No such file or directory` | Incorrect header file path | Check filename spelling and `-I` paths |
| `multiple definition of 'xxx'` | Global variables/functions defined in a header file | Put only declarations in header files; put definitions in `.c` files |

## Summary

At this point, we have a clear understanding of the complete pipeline of a C program from source code to executable. The preprocessor expands all `#` directives, the compiler translates C code into assembly, the assembler generates binary object files, and the linker assembles everything together. Header files are the contracts between modules, `printf`/`scanf` are the most basic I/O tools, and multi-file compilation is an inevitable choice as project scale grows.

### Key Takeaways

- [ ] The entry point of a C program is `int main(void)` or `int main(int argc, char *argv[])`
- [ ] Four compilation stages: preprocessing → compilation → assembly → linking
- [ ] `< >` searches system directories, `" "` searches the current directory first
- [ ] Use include guards in header files to prevent multiple inclusion
- [ ] Multi-file compilation: compile `.c` → `.o` separately, then link
- [ ] Understanding the compilation model is a prerequisite for learning C++ templates and the ODR

## Exercises

### Exercise 1: Multi-File Compilation Practice

Build a multi-file project containing the following files:

**utils.h**:

```c
#ifndef UTILS_H
#define UTILS_H

int add(int a, int b);
void print_result(const char* label, int value);

#endif /* UTILS_H */
```

Complete the following on your own:

1. **utils.c** — Implement the `add` and `multiply` functions
2. **main.c** — Call the functions in utils and test various operations
3. Use the gcc command line to manually compile and link, recording the intermediate artifacts (`.i`, `.s`, `.o` files) at each step
4. Use `nm` or `objdump` to inspect the symbol table of the object files

### Exercise 2: printf Formatting Practice

Without looking up references, write down the expected output of the following `printf` statements (then compile and run to verify):

```c
printf("[%5d]\n", 42);
printf("[%-5d]\n", 42);
printf("[%05d]\n", 42);
printf("[%.3f]\n", 3.14159);
printf("[%10.2f]\n", 3.14159);
```

## References

- [C Language Compilation Model - cppreference](https://en.cppreference.com/w/c/language/translation_phases)
- [GCC Compiler Flags Documentation](https://gcc.gnu.org/onlinedocs/gcc/Invoking-GCC.html)
- [printf Format Specifiers - cppreference](https://en.cppreference.com/w/c/io/fprintf)
