---
title: 'Deep Dive into C/C++ Compilation and Linking Techniques 3: How to Create and
  Use Static Libraries'
description: ''
tags:
- cpp-modern
- host
- intermediate
difficulty: intermediate
platform: host
chapter: 13
order: 3
translation:
  source: documents/compilation/03-creating-and-using-static-libs.md
  source_hash: 79ffd88cde2473e0b07e01fd5de01422eb942c50868bd9db2293d5ac3e11a9bc
  translated_at: '2026-05-26T10:10:06.991070+00:00'
  engine: anthropic
  token_count: 871
---
# Deep Dive into C/C++ Compilation and Linking Part 3: How to Create and Use Static Libraries

In the previous blog post, I briefly touched upon the basic introduction to static and dynamic libraries. I have included the links here:

> [Deep Dive into C/C++ Compilation and Linking - CSDN Blog](https://blog.csdn.net/charlie114514191/article/details/152921903)
>
> [Deep Dive into C/C++ Compilation and Linking Part 2: Introduction to Dynamic and Static Libraries - CSDN Blog](https://blog.csdn.net/charlie114514191/article/details/154828385)

So earlier, we briefly covered what the essence of a static library is. Although today, using dynamic libraries for code sharing is a more fundamental strategy, for the sake of completeness—and because I personally like using static libraries to package something that only depends on `C/C++` the most basic runtime (honestly, I have no deep technical reason for this choice; I just don't really like feeding a massive blob of relocatable files directly to the linker)—let's continue.

## How Do We Create a Static Library?

### `ar` Tool

So a natural question arises: we previously learned the fundamental principle of a static library (an organic combination of several relocatable files), but how do we actually create one? The answer is by using a small yet powerful tool—`ar` (Archiver).

Let me briefly introduce `ar`! It is a tool used to create, modify, and extract **archive files**. These archive files typically end with the `.a` extension (where "a" stands for archive), and their most common use case is packaging object files (`.o` files) to create **static libraries**. On Linux, when we create a static library—assuming we decide to name the library `Charlie`—the generated library will generally be `libCharlie.a`.

Some of you might wonder why it must start with `lib`. Wouldn't generating `Charlie.a` be much more intuitive? The core reason is: **this is dictated by the working conventions of the linker when we perform linking later**. Most commonly, when we `gcc/g++` compile and prepare to link objects, we dispatch `ld` to link the target libraries and relocatable files. Generally, higher-level build tools prefer using the `-L` flag to specify the search directory, combined with `-l` (lowercase L) to find the library. For example, when we try to provide the `math` static library from a known path to `main.c`, we might write something like this:

```cpp

gcc main.c -lmath

```

The linker does not directly look for a file named `math`. Instead, following convention, it attempts to find a file named **`libmath.a`** (static library) or **`libmath.so`** (dynamic library). Simply put:

- The name following the `-l` flag (`math` in this example) is called the "library name".
- The linker automatically prepends the prefix `lib` to this name.
- Then, depending on the situation (and priority), it appends a suffix like `.a` (static library) or `.so` (dynamic library) to form the complete filename.

Therefore, **naming the library file in the `lib<name>.a` format is to proactively cater to the linker's automatic search mechanism**. If the library file is not named in this format, the linker cannot find it via the convenient `-l` option. You would have to rely on the clumsy method of specifying the full path to the library file to link it, which is highly inconvenient. Furthermore, this leads to a severe issue that we will revisit when discussing dynamic libraries (for static libraries, it doesn't matter since they get packaged into the target file anyway).

### Common Command Formats for `ar`

The basic syntax of `ar` is relatively simple. It requires an **operation code** (similar to a main command) and some **modifiers** to specify the exact behavior.

```bash
ar [操作码][修饰符] <归档文件名> <文件...>

```

| **Operation Code** | **Description**                                                                 | **Common Modifiers**     | **Example Command**              |
| ------------------ | ------------------------------------------------------------------------------- | ------------------------ | -------------------------------- |
| **`r`**    | **Insert/Replace**: Adds files to the archive. If a file with the same name already exists, it replaces it. | `v` (verbose output) | `ar rv libmy.a file1.o file2.o` |
| **`t`**    | **List**: Displays the list of files contained in the archive.                  | `v` (verbose output) | `ar t libmy.a`                  |
| **`x`**    | **Extract**: Extracts (unpacks) files from the archive.                         | `v` (verbose output) | `ar xv libmy.a`                 |

> Checking the man page is always a good idea: [ar(1) - Linux man page](https://linux.die.net/man/1/ar)

### What About Windows?

This is actually handled by the MSVC toolchain. However, few people do this manually on Windows. On Windows, most people delegate this task to the monolithic IDE, Visual Studio, or, like me, use lightweight Visual Studio Code and delegate to CMake. For specific details, you can check the detailed build logs from CMake, but I won't expand on that here for the sake of brevity.

## Where Do We Use Static Libraries?

I thought about this carefully, combining my shallow engineering experience (which I admit is practically non-existent) with the materials I've read. In fact, today, static libraries can almost entirely be replaced by dynamic libraries. However, in the following scenarios, using static libraries is clearly more appropriate. Of course, since I use static libraries more often in embedded systems, I'll frame it this way:

- **Simplified distribution:** We only need to distribute a single executable file, without carrying around a bunch of `.dll` (Windows) or `.so`/`.dylib` (Linux/macOS) files.
- **Version locking:** We need to **absolutely guarantee** that our program uses a specific version of a library, free from interference by other versions on the user's system.
- **Small utilities or embedded systems:** In environments with strict limitations on file count or dynamic linking support.

## Conversely, What Are the Reasons Not to Use Static Libraries?

Reviewing the previous blog post, we already explained how static libraries work. So, it's easy to think of the first reason not to use them:

#### Executable Bloat

When focusing on **interface reuse**, using static libraries obviously causes the size of all dependent libraries and executables to increase dramatically (Executable Bloat). Therefore, **for any module whose purpose is to provide functional interfaces to other dependencies and that stands completely independent, please use a dynamic library**. In this case, keeping only one copy of the code dependency and letting the operating system and loader automatically coordinate all mapped symbol relationships is clearly the better approach.

#### Updates Require Recompilation and Redistribution (Hot Reloading Request)

In scenarios that prioritize **hot reloading**, using static libraries is clearly inappropriate. For example, when we can't easily replace the executable file itself but only need to update a sub-dependency (say, a library we use has a vulnerability discovered by an enthusiastic open-source developer who promptly reports it to us)—meaning we find a security vulnerability or a bug in the library that needs fixing—with a static library, we must **recompile and redistribute the entire application (static linking makes this code a part of the main body rather than a required dependency)**.

#### Potential Symbol Collisions and Version Management Issues (Symbol Collisions)

If we link **multiple versions** of static libraries or libraries with **identical symbol names** into the same executable, the compiler/linker will attempt to resolve them, but the risk is very high (if I remember correctly, it drops them based on symbol strength, or randomly discards them if they are equal). This is truly dangerous—nobody likes playing a guessing game with their program.
