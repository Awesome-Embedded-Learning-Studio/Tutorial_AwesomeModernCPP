---
chapter: 13
difficulty: intermediate
order: 3
platform: host
reading_time_minutes: 6
tags:
- cpp-modern
- host
- intermediate
title: 'Deep Dive into C/C++ Compilation and Linking Part 3: How to Create and Use
  Static Libraries'
description: ''
translation:
  source: documents/compilation/03-creating-and-using-static-libs.md
  source_hash: 994ba6406ea27e83d4acd93cbf12656c3fd61db3683f1fc2eefe3320aa388f29
  translated_at: '2026-06-16T03:26:52.565296+00:00'
  engine: anthropic
  token_count: 877
---
# Deep Dive into C/C++ Compilation and Linking Part 3: How to Create and Use Static Libraries

In the previous blog post, I briefly introduced the basics of static and dynamic libraries. Here are the links:

> [Deep Dive into C/C++ Compilation and Linking - CSDN Blog](https://blog.csdn.net/charlie114514191/article/details/152921903)
>
> [Deep Dive into C/C++ Compilation and Linking 2: Intro to Dynamic and Static Libraries - CSDN Blog](https://blog.csdn.net/charlie114514191/article/details/154828385)

So, we have previously covered the essence of static libraries. Although using dynamic libraries is a more fundamental strategy for code sharing today, for the sake of completeness—and because I personally prefer using static libraries to package code that depends only on the most basic runtime (I don't have a strong technical reason for this, I just don't like dumping a massive pile of relocatable files directly into the linker)—let's discuss this further.

## How to Create a Static Library?

### The `ar` Tool

A natural question arises: we have learned the basic principles of static libraries (an organic combination of several relocatable files), but how do we create one? The answer is a small yet powerful tool—`ar` (Archiver).

Let me briefly introduce `ar`! It is a tool used to create, modify, and extract **archive files**. These files usually end with the `.a` extension (where 'a' stands for archive). The most common use is packaging object files (`.o` files) to create **static link libraries**. On Linux, if we decide to name a library `demo`, the generated library will typically be `libdemo.a`.

You might wonder why it must start with `lib`. Isn't generating `demo.a` more intuitive? The core reason is: **this is dictated by the working conventions of the linker we will use later.** Most often, when we compile and link objects, we dispatch `ld` to link target libraries and relocatable files. Generally, high-level build tools use `-L` to specify the search directory and `-l` (lowercase L) to find the library. For example, when we try to provide a `math` static library at a known path to `main.c`, we might write:

```bash
gcc main.c -L./lib -lmath -o app
```

The linker does not directly look for a file named `math`. Instead, following conventions, it attempts to find a file named **`libmath.a`** (static library) or **`libmath.so`** (dynamic library). Simply put:

- The name following the `-l` parameter (`math` in this example) is called the "library name".
- The linker automatically adds the prefix `lib` to this name.
- Then, based on the situation (and priority), it adds `.a` (static library) or `.so` (dynamic library) suffixes to form the complete filename.

Therefore, **naming the library file in the `libname.a` format is to actively cater to the linker's automatic search mechanism**. If the library file is not named in this format, the linker cannot find it via the convenient `-l` option. You would have to link by specifying the full path to the library file, which is clumsy and inconvenient. This also leads to a serious problem that we will revisit when discussing dynamic libraries (it doesn't matter for static libraries, as they are packaged into the target file).

### Common `ar` Commands

The basic syntax of `ar` is relatively simple; it requires an **operation code** (similar to a main command) and some **modifiers** to specify specific behaviors.

```text
ar -operation modifiers archive_name member_list
```

| **Operation Code** | **Description**                                                                 | **Common Modifiers** | **Example Command**        |
| ------------------ | ------------------------------------------------------------------------------- | -------------------- | -------------------------- |
| **r**              | **Insert/Replace**: Adds files to the archive. If a file with the same name exists, it replaces it. | `v` (verbose)       | `ar r libdemo.a file1.o`   |
| **t**              | **List**: Displays the list of files contained in the archive.                  | `v` (verbose)        | `ar t libdemo.a`           |
| **x**              | **Extract**: Extracts (unpacks) files from the archive.                         | `v` (verbose)        | `ar x libdemo.a file1.o`   |

> Checking the man page is always a good idea: [ar(1) - Linux man page](https://linux.die.net/man/1/ar)

### What about Windows?

This is actually handled by the MSVC toolchain. However, few people do this manually on Windows; most people delegate the task to the massive IDE: Visual Studio, or like me, use lightweight Visual Studio Code and delegate to CMake. For specific details, you can check the detailed logs of CMake compilation. I won't expand on this here due to space constraints.

## Where Do We Use Static Libraries?

I thought about this carefully, combining my shallow engineering experience (which is practically non-existent) with the materials I've read. Actually, today static libraries can almost be replaced by dynamic libraries. However, in these scenarios, using static libraries is clearly more appropriate. Since I use static libraries more in embedded development, I will frame it this way:

- **Simplified Distribution:** You only need to distribute one executable file, without carrying a bunch of `.dll` (Windows) or `.so`/`.dylib` (Linux/macOS) files.
- **Version Locking:** You need to **absolutely guarantee** that your program uses a specific version of a library, free from interference by other versions on the user's system.
- **Small Tools or Embedded Systems:** In environments where the number of files or dynamic linking support is strictly limited.

## Conversely, Reasons Not to Use Static Libraries

Reviewing the previous blog, we explained how static libraries work. So, it is easy to think of the first reason not to use them:

#### Executable Bloat

When focusing on **interface reuse**, using static libraries obviously leads to a sizeable increase in the size of all libraries and executables that depend on them (Executable Bloat). Therefore, **for any module intended to provide functional interfaces to other dependencies and remain independent, please use a dynamic library**. In this case, we keep the code dependency in a single copy and let the operating system and loader automatically coordinate all symbol mapping relationships, which is clearly better.

#### Updates Require Recompilation and Redistribution (Hot Reloading Request)

In scenarios focusing on **hot reloading**, using static libraries is clearly unreasonable. For example, when it is inconvenient to replace the entire executable file directly, but we only need to update a sub-dependency (for instance, a library we use has a vulnerability discovered by an enthusiastic open-source programmer and promptly reported to us)—meaning we found a security vulnerability or a bug in the library—with a static library, we must **recompile and redistribute the entire application (static linking makes this code part of the main body rather than a required dependency)**.

#### Potential Symbol Collisions and Version Management Issues

If we link **multiple versions** of static libraries or libraries with **identical symbol names** into the same executable, the compiler/linker will attempt to resolve them, but the risk is high (if I recall correctly, it discards them randomly based on symbol strength and equality). This is really dangerous; no one likes to play a guessing game with their program.
