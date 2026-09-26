---
chapter: 1
cpp_standard:
- 11
description: Understand the basic structure of a C program, the four-stage compilation pipeline, the header-file mechanism, and basic I/O — laying the compilation-model groundwork for everything that follows in C++
difficulty: beginner
order: 1
platform: host
prerequisites:
- None (first article in this series)
reading_time_minutes: 13
tags:
- host
- cpp-modern
- beginner
- 入门
title: Program Structure and Compilation Basics
translation:
  source: documents/vol1-fundamentals/c_tutorials/01-program-structure-and-compilation.md
  source_hash: c0e8c188bdfeffe7095b3f67707aceb8740f38f6e7350a92a71c59a9474ff7c6
  translated_at: '2026-09-25T12:38:24+00:00'
  engine: anthropic
  token_count: 7200
---
# Program Structure and Compilation Basics

If you've written some C code before, chances are you just clicked "Run" in an IDE and called it a day — how the code in a `.c` file turns into a runnable binary is probably a middle step you never had to care about. But honestly, understanding the compilation model becomes critical once you move on to C++: template instantiation, header-file strategy, the ODR (One Definition Rule) — without a grasp of the basic compilation pipeline, you're basically working with a black box. So let's get this sorted out from the very beginning.

## Environment Notes

All commands and code in this article have been verified in the following environment:

- **Operating system**: Linux (Ubuntu 22.04+) / WSL2 / macOS
- **Compiler**: GCC 11+ (confirm the version with `gcc --version`)
- **Compile flags**: `gcc -Wall -Wextra -std=c11` (warnings on, C11 standard pinned)
- **Companion tools**: `objdump`, `nm` (bundled with GCC, for inspecting object files)

If you're on Windows without WSL, MinGW-w64 or MSVC can compile and run everything too, but the output format of some tool commands (such as `nm`, `objdump`) will differ.

## Step 1 — Meet the Skeleton of a C Program

The entry point of a C program is always the `main` function — that's not just convention, it's what the C standard mandates. The C standard defines two legal signatures for `main`:

```c
// Version without command-line arguments
int main(void) {
    return 0;
}

// Version with command-line arguments
int main(int argc, char *argv[]) {
    // argc: the number of arguments (at least 1, namely the program itself)
    // argv: the array of argument strings; argv[0] is the program name
    return 0;
}
```

The return type of `main` must be `int` — `void main()` may happen to run on certain ancient compilers, but that's non-standard behavior. `return 0` signals a normal exit, a non-zero value signals an abnormal one, and the shell picks the value up through `$?` to judge whether the program ran cleanly.

Don't use `void main()`. Some old compilers accept it, but the C standard recognizes only `int main`. On Linux, shell scripts and CI/CD pipelines routinely fetch a program's return value via `$?` — if your `main` doesn't return a meaningful value, the upstream logic doing the checking can go wrong.

`argc` and `argv` let a program receive external arguments at startup. For example, given `./myprogram hello world`, `argc` is 3, `argv[0]` is `"./myprogram"`, `argv[1]` is `"hello"`, and `argv[2]` is `"world"`.

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

The `#include <stdio.h>` on the first line is a preprocessor directive: it splices the contents of the standard I/O library's header verbatim into that position. Without that header included, the compiler has no idea what `printf` is and will warn or even error out.

## Step 2 — Breaking Down the Four Stages of Compilation

Now let's take apart how a `.c` file becomes an executable. The whole process splits into four stages — preprocessing → compilation → assembly → linking — and we can use gcc's options to trigger each stage by hand and inspect the intermediate artifacts.

### Stage 1: Preprocessing

The preprocessor handles every directive that starts with `#` — expanding macros, splicing in header contents, and processing conditional compilation:

```bash
# Run preprocessing only; write to a file so it's easy to inspect
gcc -E hello.c -o hello.i
```

The preprocessed `.i` file gets enormous — a single `#include <stdio.h>` pulls in the entire standard I/O header plus every header it transitively includes. Open `hello.i` and take a look: the first few lines are comments, followed by hundreds or thousands of lines of header content, and only at the very end do your own few lines of code appear.

What the preprocessor does is simple to describe — plain textual substitution — but this mechanism is a major source of C's flexibility, and it's the foundation for understanding C++ templates and header organization.

### Stage 2: Compilation

The compiler translates the preprocessed C code into assembly, running through lexical analysis, syntax analysis, semantic analysis, intermediate code generation, and optimization:

```bash
gcc -S hello.c -o hello.s
```

Open `hello.s` and you'll see x86-64 assembly that looks roughly like this (output differs across platforms):

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

One fun detail: the `printf("Hello, World!\n")` we wrote got optimized by the compiler into a call to `puts` — the format string is a lone string ending in `\n` with no format specifiers at all, so the compiler knows `puts` is more efficient and just swaps it in.

### Stage 3: Assembly

The assembler translates the assembly code into machine code, producing an object file:

```bash
gcc -c hello.c -o hello.o
```

The `.o` file is a binary format (ELF on Linux) containing machine instructions, a symbol table, and relocation information. You can view the disassembly with `objdump` and the symbol table with `nm`:

```bash
objdump -d hello.o    # inspect the disassembly
nm hello.o            # inspect the symbol table
```

Inside the object file, the addresses of function calls (such as the call to `printf`) are still left blank at this point, waiting for the link stage to fill them in.

### Stage 4: Linking

The linker combines one or more object files together with whatever libraries they need into the final executable, resolving all references to external symbols:

```bash
# Full compilation (all four stages in one go)
gcc hello.c -o hello

# Or step by step
gcc -c hello.c -o hello.o
gcc hello.o -o hello
```

This stage is the key to understanding multi-file programming. Each `.c` file is compiled independently into a `.o`, and the linker then assembles them together. This separate-compilation model is a core design of C/C++ — it lets us recompile only the files we changed instead of recompiling the entire project.

### The Compilation Pipeline at a Glance

```text
hello.c → [preprocess] → hello.i → [compile] → hello.s → [assemble] → hello.o → [link] → hello
              ↑                                                  ↑
         #include expansion                             merge .o files + libraries
         #define substitution                          resolve external symbols
         conditional compilation                        emit the executable
```

## Step 3 — Figuring Out How Headers Work

`#include` comes in two syntactic forms, with different search paths:

```c
#include <stdio.h>    // Angle brackets: search only system/standard-library directories
#include "myheader.h" // Quotes: search the current file's directory first, then system directories if not found
```

The logic is intuitive — angle brackets are for "stuff the system provides", quotes are for "stuff you wrote yourself". The compiler carries a set of default search paths (view them with `gcc -E -Wp,-v - < /dev/null`), and the `-I` option adds extra search paths.

A header typically holds function declarations (prototypes), type definitions (`typedef`/`struct`), macro definitions, and external variable declarations (`extern`). A header is the "contract" modules use to talk to each other — it tells callers "what this module offers" without exposing implementation details. C++ later realizes this idea more elegantly through the public/private mechanism of `class`.

Every header should carry an include guard to prevent being included multiple times:

```c
#ifndef MYHEADER_H
#define MYHEADER_H

// header file contents

#endif /* MYHEADER_H */
```

Or use `#pragma once`:

```c
#pragma once

// header file contents
```

`#pragma once` is terser, but it can hit compatibility issues in certain edge cases (symlinked files, network path mappings). Just pick one scheme and stay consistent across the project — and if you're unsure, go with the traditional `#ifndef` scheme, which the standard guarantees.

## Step 4 — Getting Hands-On with Basic I/O

### Formatted Output with printf

`printf` is the most-used output function in the C standard library, and its format string supports a rich set of format specifiers:

```c
#include <stdio.h>

int main(void) {
    int i = 42;
    unsigned int u = 0xDEAD;
    double f = 3.14159265359;
    const char* s = "Hello";
    int* p = &i;

    printf("整数: %d\n", i);             // Decimal: 42
    printf("十六进制: %x / %X\n", u, u); // Lowercase dead / uppercase DEAD
    printf("浮点: %f\n", f);             // 6 decimal places by default: 3.141593
    printf("浮点精度: %.2f\n", f);       // 2 decimal places: 3.14
    printf("字符串: %s\n", s);           // Hello
    printf("指针: %p\n", (void*)p);      // Pointer address

    // Width and alignment
    printf("[%10d]\n", i);    // Right-aligned, width 10: [        42]
    printf("[%-10d]\n", i);   // Left-aligned, width 10: [42        ]
    printf("[%010d]\n", i);   // Zero-padded: [0000000042]
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

One often-ignored detail: `printf` returns the number of characters it successfully wrote, and a negative value means an error occurred. In embedded development, using that return value for a quick error check is sometimes quite handy.

### Reading User Input with scanf

`scanf` reads data from standard input; its format specifiers mirror `printf`'s but come with a few subtle differences:

```c
int age;
float weight;
char name[32];

printf("请输入姓名 年龄 体重: ");
scanf("%31s %d %f", name, &age, &weight);

// name is an array, so no & needed (the array name is already an address)
// age and weight are ordinary variables, so you must pass their addresses
```

`scanf`'s `%s` stops at the first whitespace character and performs no buffer-size checking. If the input exceeds the buffer's length, you get a buffer overflow, plain and simple. The safe approach is to specify a maximum width (`%63s`), or replace it with an `fgets` + `sscanf` combination. Real-world projects rarely use `scanf`, but understanding how it works still matters at the learning stage.

## Step 5 — Build a Multi-File Project by Hand

Let's build a small multi-file project and get a feel for the payoff of separate compilation. The project layout:

```text
calc/
├── main.c      // main program
├── math_ops.h  // declarations of the math operation functions
└── math_ops.c  // implementations of the math operation functions
```

**math_ops.h** — the header, the module's "public interface":

```c
#ifndef MATH_OPS_H
#define MATH_OPS_H

int add(int a, int b);
int subtract(int a, int b);
int multiply(int a, int b);
float divide(int a, int b);

#endif /* MATH_OPS_H */
```

**math_ops.c** — the implementation file:

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

**main.c** — the main program:

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
# Compile each source file into an object file, then link
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

This step-by-step compilation pattern is enormously useful. When you modify `math_ops.c` but leave the header and `main.c` untouched, you only need to recompile `math_ops.o` and link again — build tools like `Makefile` and `CMake` are, at their core, automating exactly this process.

## Bridging to C++

C++ keeps the same separate-compilation model but layers more elaborate machinery on top. Headers remain C++'s primary modularization tool (up until Modules arrived in C++20), yet C++ templates bring a new problem — template code usually has to live in headers, because the compiler needs to see the complete definition before it can instantiate it. This is exactly why understanding the compilation model matters: template instantiation happens during the compilation stage, and the linker only ever sees symbols that were already instantiated.

C++ recommends the `<cxxx>` form of headers (e.g. `<cstdio>` instead of `<stdio.h>`); these headers place the C library functions into the `std` namespace. `<iostream>` provides type-safe I/O, but `printf` is usually faster in practice — it skips `iostream`'s locale handling, virtual function calls, and formatter-object construction overhead. In performance-sensitive embedded scenarios, C-style `printf`/`snprintf` remains the better choice.

The ODR (One Definition Rule) is the core rule of C++'s linkage model: an entity may have exactly one definition in the entire program. Violating the ODR causes trouble in C as well, but C++'s templates, inline functions, and `constexpr` push the issue to the forefront — we'll dig into it in detail in the C++ chapters.

## Common Compilation Errors — Quick Reference

| Error message | Cause | Fix |
|----------|------|----------|
| `undefined reference to 'xxx'` | No function definition found at the link stage | Check whether you forgot to link a `.o` file or a library |
| `implicit declaration of function` | An undeclared function was used | Add the matching `#include` or a function declaration |
| `redefinition of 'xxx'` | The same symbol got defined multiple times | Check whether the header is missing an include guard |
| `No such file or directory` | Wrong header path | Check the filename spelling and the `-I` path |
| `multiple definition of 'xxx'` | A global variable/function defined in a header | Put only declarations in headers; definitions belong in `.c` files |

## Exercises

### Exercise 1: Multi-File Compilation in Practice

**Difficulty: Basic** · step-by-step multi-file compilation and the symbol table

Build a multi-file project containing the following files:

**utils.h**:

```c
#ifndef UTILS_H
#define UTILS_H

int add(int a, int b);
void print_result(const char* label, int value);

#endif /* UTILS_H */
```

Complete these on your own:

1. **utils.c** — implement the `add` and `print_result` functions
2. **main.c** — call the functions from utils and test various operations
3. Compile and link manually from the gcc command line, recording each step's intermediate artifacts (the `.i`, `.s`, `.o` files)
4. Inspect the object file's symbol table with `nm` or `objdump`

### Exercise 2: printf Formatting Practice

**Difficulty: Basic** · practicing width, precision, and alignment in printf

Without looking anything up, write down the expected output of these `printf` statements (then compile and run to verify):

```c
printf("[%5d]\n", 42);
printf("[%-5d]\n", 42);
printf("[%05d]\n", 42);
printf("[%.3f]\n", 3.14159);
printf("[%10.2f]\n", 3.14159);
```

## References

- [The C compilation model — cppreference](https://en.cppreference.com/w/c/language/translation_phases)
- [GCC compiler options documentation](https://gcc.gnu.org/onlinedocs/gcc/Invoking-GCC.html)
- [printf format specifiers — cppreference](https://en.cppreference.com/w/c/io/fprintf)
