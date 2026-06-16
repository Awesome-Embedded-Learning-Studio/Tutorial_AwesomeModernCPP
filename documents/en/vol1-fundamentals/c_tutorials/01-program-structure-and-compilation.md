---
chapter: 1
cpp_standard:
- 11
description: Understand the basic structure of C programs, the four-stage compilation
  process, the header file mechanism, and basic I/O, laying the foundation for the
  compilation model in subsequent C++ studies.
difficulty: beginner
order: 1
platform: host
prerequisites:
- 无（本系列第一篇）
reading_time_minutes: 13
tags:
- host
- cpp-modern
- beginner
- 入门
title: Program Structure and Compilation Fundamentals
translation:
  source: documents/vol1-fundamentals/c_tutorials/01-program-structure-and-compilation.md
  source_hash: 3f043e00dff8972ab89649fe7f150595f7c398c5b04f5f0724ec4b12cace1859
  translated_at: '2026-06-16T03:32:30.084585+00:00'
  engine: anthropic
  token_count: 2330
---
# Program Structure and Compilation Basics

If you have written some C code before, you likely just hit "Run" in an IDE and called it a day—you might never have cared about the intermediate process of how code in a `.c` file becomes a runnable binary. However, understanding the compilation model becomes crucial when learning C++ later: template instantiation, header file strategies, and the ODR (One Definition Rule) are basically black magic if you don't understand the basic compilation workflow. So, let's clarify this from the very beginning.

> **Learning Objectives**
>
> - After completing this chapter, you will be able to:
> - [ ] Understand the basic structure of a C program (`main` function, header file inclusion).
> - [ ] Master the principles of the four compilation stages and how to perform them manually.
> - [ ] Understand the header file search mechanism and the difference between `<>` and `""`.
> - [ ] Proficiently use common format specifiers for `printf`/`scanf`.
> - [ ] Independently complete the compilation and linking of multi-file programs.

## Environment Setup

All commands and code in this article have been verified in the following environment:

- **Operating System**: Linux (Ubuntu 22.04+) / WSL2 / macOS
- **Compiler**: GCC 11+ (Confirm version via `gcc --version`)
- **Compiler Flags**: `-Wall -std=c11` (Enable warnings, specify C11 standard)
- **Auxiliary Tools**: `objdump`, `nm` (Included with GCC, used to inspect object files)

If you are using Windows without WSL, MinGW-w64 or MSVC can also compile and run the code, but the output format of some tool commands (like `objdump`, `nm`) will differ.

## Step 1 — Understanding the Skeleton of a C Program

The entry point of a C program is always the `main` function—this isn't just a convention; it is mandated by the C standard. The C standard defines two valid signatures for `main`:

```c
int main(void);
int main(int argc, char *argv[]);
```

The return type of `main` must be `int`—while some older compilers might accept `void`, that is non-standard behavior. A return value of `0` indicates normal exit, while a non-zero value indicates an anomaly; the shell retrieves this value via `$?` to determine if the program executed successfully.

> ⚠️ **Pitfall Warning**: Do not use `void main`. Although some older compilers accept it, the C standard only recognizes `int main`. On Linux, shell scripts and CI/CD pipelines often obtain the program's return value via `$?`—if your `main` does not return a meaningful value, upstream logic may fail.

`argc` and `argv` allow the program to receive external parameters at startup. For example, when executing `git commit -m "fix"`, `argc` is 3, `argv[0]` is `git`, `argv[1]` is `commit`, and `argv[2]` is `-m "fix"`.

A minimal, complete C program:

```c
#include <stdio.h>

int main(void) {
    printf("Hello, Embedded World!\n");
    return 0;
}
```

Output:

```text
Hello, Embedded World!
```

The `#include <stdio.h>` in the first line is a preprocessor directive. It inserts the contents of the standard I/O library header verbatim into the current location. Without this header, the compiler doesn't know what `printf` is and will issue a warning or error.

## Step 2 — Breaking Down the Four Stages of Compilation

Now let's break down how a `.c` file is transformed into an executable. The entire process is divided into four stages: Preprocessing → Compilation → Assembly → Linking. We can use GCC options to manually trigger each stage and observe the intermediate products.

### Stage 1: Preprocessing

The preprocessor handles all directives starting with `#`—expanding macros, inserting header file contents, and processing conditional compilation:

```bash
gcc -E hello.c -o hello.i
```

The preprocessed `.i` file will be very large—a single `#include` will expand the entire standard I/O header and all headers it indirectly includes. You can open `hello.i` to see that the first few lines are comments, followed by hundreds or thousands of lines of header content, and finally, the few lines of code you wrote.

What the preprocessor does is simple in theory—pure text replacement—but this mechanism is a significant source of C's flexibility and the foundation for understanding C++ templates and header file organization.

### Stage 2: Compilation

The compiler translates the preprocessed C code into assembly code, undergoing lexical analysis, syntax analysis, semantic analysis, intermediate code generation, and optimization:

```bash
gcc -S hello.i -o hello.s
```

Opening `hello.s`, you will see x86-64 assembly similar to this (output varies by platform):

```asm
... (omitted) ...
    lea     rdi, [rip + str.LC0]
    call    puts
... (omitted) ...
```

An interesting detail: the `printf` we wrote was optimized by the compiler into a `puts` call—because the format string contains only a string constant ending in `\n` with no format placeholders, the compiler knows `puts` is more efficient and substitutes it directly.

### Stage 3: Assembly

The assembler translates assembly code into machine code, generating an object file:

```bash
gcc -c hello.s -o hello.o
```

The `hello.o` file is in binary format (ELF on Linux), containing machine instructions, a symbol table, and relocation information. You can use `objdump -d hello.o` to view the disassembly and `nm hello.o` to view the symbol table:

```text
... (omitted) ...
0000000000000000 T main
                 U puts
... (omitted) ...
```

Function calls within the object file (such as the call to `puts`) have placeholder addresses at this stage, waiting for the linking stage to fill them in.

### Stage 4: Linking

The linker combines one or more object files and required library files into the final executable, resolving all external symbol references:

```bash
gcc hello.o -o hello
```

This stage is key to understanding multi-file programming. Each `.c` file is compiled independently into a `.o` file, and then the linker assembles them. This separate compilation model is a core design of C/C++—it allows us to recompile only modified files without rebuilding the entire project.

### Compilation Pipeline Summary

```mermaid
graph LR
    A[Source Code .c] --> B(Preprocessing<br/>gcc -E)
    B --> C[Preprocessed File .i]
    C --> D(Compilation<br/>gcc -S)
    D --> E[Assembly Code .s]
    E --> F(Assembly<br/>gcc -c)
    F --> G[Object File .o]
    G --> H(Linking<br/>gcc)
    H --> I[Executable]
```

## Step 3 — Figuring Out How Headers Work

`#include` has two syntax forms with different search paths:

```c
#include <stdio.h>   // System headers
#include "myheader.h" // User-defined headers
```

The logic is straightforward—angle brackets are for "system-provided items", while quotes are for "items you wrote yourself". The compiler has a set of default search paths (viewable with `gcc -v`), and the `-I` option can add additional search paths.

Headers typically contain function declarations (prototypes), type definitions (`struct`/`enum`), macro definitions, and external variable declarations (`extern`). The header is the "contract" between modules—it tells the caller "what this module provides" without exposing implementation details. This concept is implemented more elegantly in C++ by the `class` public/private mechanism.

Every header should have an include guard to prevent multiple inclusions:

```c
#ifndef MATH_OPS_H
#define MATH_OPS_H

// ... declarations ...

#endif
```

Or use `#pragma once`:

```c
#pragma once
// ... declarations ...
```

> ⚠️ **Pitfall Warning**: While `#pragma once` is concise, it may have compatibility issues in certain edge cases (symbolic link files, network path mappings). Choosing one strategy and keeping consistent is fine—if unsure, use the traditional `#ifndef` scheme, as it is guaranteed by the standard.

## Step 4 — Getting Hands-On with Basic I/O

### Formatted Output with `printf`

`printf` is the most commonly used output function in the C standard library, supporting rich format specifiers:

```c
#include <stdio.h>

int main(void) {
    int    integer_val = 42;
    float  float_val   = 3.14f;
    char   char_val    = 'A';
    char  *str_val     = "Embedded";

    printf("Integer: %d\n", integer_val);
    printf("Float  : %.2f\n", float_val);
    printf("Char   : %c\n", char_val);
    printf("String : %s\n", str_val);

    return 0;
}
```

Output:

```text
Integer: 42
Float  : 3.14
Char   : A
String : Embedded
```

An often overlooked detail: the return value of `printf` is the number of characters successfully output, with a negative value indicating an error. In embedded development, using the return value for simple error checking can sometimes be useful.

### Reading User Input with `scanf`

`scanf` reads data from standard input. Format specifiers are similar to `printf` but have subtle differences:

```c
#include <stdio.h>

int main(void) {
    int age;
    char name[32];

    printf("Enter age: ");
    scanf("%d", &age); // Note the & operator

    printf("Enter name: ");
    scanf("%31s", name); // Limit length to prevent overflow

    printf("User: %s, %d years old\n", name, age);
    return 0;
}
```

> ⚠️ **Pitfall Warning**: `scanf`'s `%s` stops when it encounters whitespace and does not check buffer size. If input exceeds the buffer length, it directly causes a buffer overflow. The safe approach is to specify a maximum length (`%31s`), or use `fgets` + `sscanf` instead. While `scanf` is rarely used in production projects, understanding its mechanism is still important during the learning phase.

## Step 5 — Building a Multi-File Project

Let's build a simple multi-file project to experience the benefits of separate compilation. The project structure is as follows:

```text
.
├── math_ops.h
├── math_ops.c
└── main.c
```

**math_ops.h** — Header file, the "public interface" of the module:

```c
#pragma once

int add(int a, int b);
int multiply(int a, int b);
```

**math_ops.c** — Implementation file:

```c
#include "math_ops.h"

int add(int a, int b) {
    return a + b;
}

int multiply(int a, int b) {
    return a * b;
}
```

**main.c** — Main program:

```c
#include <stdio.h>
#include "math_ops.h"

int main(void) {
    int x = 5, y = 10;
    printf("%d + %d = %d\n", x, y, add(x, y));
    printf("%d * %d = %d\n", x, y, multiply(x, y));
    return 0;
}
```

Compiling and running:

```bash
gcc -c math_ops.c -o math_ops.o
gcc -c main.c -o main.o
gcc math_ops.o main.o -o myapp
./myapp
```

Output:

```text
5 + 10 = 15
5 * 10 = 50
```

This step-by-step compilation mode is very useful. When you modify `math_ops.c` but haven't touched the header or `main.c`, you only need to recompile `math_ops.c` and link—build tools like `Make` or `CMake` essentially automate this process.

## C++ Transition

C++ retains the same separate compilation model but adds more complex mechanisms. Header files remain C++'s primary modularization tool (until C++20 Modules arrived), but C++ templates introduce a new issue—template code usually must be placed in header files because the compiler needs to see the complete definition to instantiate. Understanding the compilation model is important because template instantiation happens at the compilation stage, and the linker only sees the already instantiated symbols.

C++ recommends using header names without the `.h` suffix (such as `<cstdio>` rather than `<stdio.h>`), which place C library functions into the `std` namespace. C++ iostreams provide type-safe I/O, but performance-wise `printf` is usually faster—because it lacks the overhead of locale, virtual function calls, and formatting object construction found in `iostream`. In performance-sensitive embedded scenarios, C-style `printf`/`scanf` remain the better choice.

The ODR (One Definition Rule) is the core rule of the C++ linking model: an entity can have only one definition throughout the program. Violating ODR causes problems in C as well, but C++ templates, inline functions, and `inline` variables make this issue more prominent—we will discuss this in detail in later C++ chapters.

## Common Compilation Errors Quick Reference

| Error Message | Cause | Solution |
|---|---|---|
| `undefined reference to ...` | Function definition not found during linking | Check if you forgot to link the `.o` file or library |
| `implicit declaration of function` | Used an undeclared function | Add the corresponding `#include` or function declaration |
| `multiple definition of ...` | The same symbol defined more than once | Check if the header file is missing an include guard |
| `No such file or directory` | Incorrect header file path | Check filename spelling and `-I` path |
| `multiple definition of global variable` | Global variables/functions defined in headers | Place only declarations in headers, definitions in `.c` files |

## Summary

At this point, we have a clear understanding of the complete path of a C program from source code to executable. Preprocessing expands all `#` directives, the compiler translates C code to assembly, the assembler generates binary object files, and the linker assembles everything. Headers are the contracts between modules, `printf`/`scanf` are the most basic I/O tools, and multi-file compilation is the inevitable choice as project scale grows.

### Key Takeaways

- [ ] C program entry is `int main(void)` or `int main(int argc, char* argv[])`.
- [ ] Four compilation stages: Preprocessing → Compilation → Assembly → Linking.
- [ ] `#include <>` searches system directories; `#include ""` searches the current directory first.
- [ ] Use include guards in headers to prevent multiple inclusion.
- [ ] Multi-file compilation: Compile `.c` to `.o` separately, then link.
- [ ] Understanding the compilation model is a prerequisite for learning C++ templates and ODR.

## Exercises

### Exercise 1: Multi-File Compilation Practice

Build a multi-file project containing the following files:

**utils.h**:

```c
#pragma once

int add(int a, int b);
int sub(int a, int b);
```

Please complete the following:

1. **utils.c** — Implement the `add` and `sub` functions.
2. **main.c** — Call functions from utils and test various operations.
3. Manually compile and link using the gcc command line, recording the intermediate products of each step (`.i`, `.o`, executable files).
4. Use `nm` or `objdump` to view the symbol table of the object files.

### Exercise 2: `printf` Formatting Practice

Without looking up resources, write the expected output of the following `printf` statements (then compile and run to verify):

```c
int x = 10;
printf("%d\n", x);      // Output: ?
printf("%5d\n", x);     // Output: ?
printf("%05d\n", x);    // Output: ?
printf("%-5d\n", x);    // Output: ?
```

## References

- [C Language Compilation Model - cppreference](https://en.cppreference.com/w/c/language/translation_phases)
- [GCC Compiler Options Documentation](https://gcc.gnu.org/onlinedocs/gcc/Invoking-GCC.html)
- [printf Format Specifiers - cppreference](https://en.cppreference.com/w/c/io/fprintf)
