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
title: "Deep Dive into C/C++ Compilation and Linking · Part 3: How to Build and Use Static Libraries"
description: 'Pack object files into a lib<name>.a static library with ar, get clear on why the library name has to start with lib, how the linker finds libraries through the -l convention, and when you should reach for a static library.'
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/compilation/03-creating-and-using-static-libs.md
  source_hash: 7420b2a420d676bc6aec93b2752bb3bc3ff9240febde91ec4afa7c755b475a9f
  translated_at: '2026-09-25T23:47:53+00:00'
  engine: anthropic
  token_count: 4400
---
# Deep Dive into C/C++ Compilation and Linking · Part 3: How to Build and Use Static Libraries

In the previous post we briefly touched on the basic introduction to static and dynamic libraries; I'll leave the links here:

> [Deep Dive into C/C++ Compilation and Linking · Part 1: Introduction — CSDN blog](https://blog.csdn.net/charlie114514191/article/details/152921903)
>
> [Deep Dive into C/C++ Compilation and Linking · Part 2: An Introduction to Static and Dynamic Libraries — CSDN blog](https://blog.csdn.net/charlie114514191/article/details/154828385)

So earlier on we already talked through what a static library essentially is. Even though today, using dynamic libraries to share code is the more standard strategy, I still want to cover this — for the sake of completeness, and because I'm also the kind of person who likes to pack anything that depends only on the most basic `C/C++` runtime into a static library (in truth I have no technical reason whatsoever for the choice; I simply don't enjoy dumping a great pile of relocatable files straight onto the linker).

## How to Build a Static Library

### The `ar` Tool

So a very natural question pops up: earlier we learned the most basic principle behind a static library (an organic combination of a number of relocatable files) — but how do we actually make one? The answer is to use a small but powerful tool: `ar` (the Archiver).

Let me give `ar` a quick introduction! It is a tool for creating, modifying, and extracting **archive files**. These archives usually end with the `.a` extension (the a is short for archive), and their most common use is bundling object files (`.o` files) to create **static libraries**. On Linux, we like to name a static library — well, at least a static library — like so: suppose we decide the library's name is going to be `Charlie`; then the library we generate will generally be `libCharlie.a`.

Some of you might be puzzled: why must it start with `lib`? Wouldn't generating `Charlie.a` be far more intuitive? Well, here's the deal — the most essential reason is: **it's demanded by the working convention the linker follows when we later come back to do the linking**. Most of the time, when `gcc`/`g++` compiles and gets ready to link the targets, it dispatches `ld` to link the target libraries and relocatable files; generally speaking, upper-level build tools habitually pair `-L` folder-search paths with `-l` (that's a lowercase L) to find libraries. For example, when we try to hand `main.c` the math static library from the well-known directories, we might write it like this:


```cpp

gcc main.c -lmath

```

The linker will not go off searching for a file literally named `math`. Instead, following the convention, it tries to find a file named **`libmath.a`** (static library) or **`libmath.so`** (dynamic library). Put simply:

- The name after the `-l` flag (`math` in this example) is called the "library name".
- The linker automatically prepends the `lib` prefix to that name.
- Then, depending on the situation (and the priority order), it appends a suffix such as `.a` (static library) or `.so` (dynamic library), thereby building up the complete filename.

Therefore, **naming the library file in the `lib<name>.a` format means proactively playing along with the linker's automatic lookup mechanism**. If the library file is not named in this format, the linker cannot find it through the convenient `-l` option; your only recourse is the clumsy route of specifying the library file's full path directly, which is highly inconvenient. Worse, a rather serious problem shows up — one we will dig back out when we get to dynamic libraries (with static libraries it doesn't matter; they just get packed into the target file anyway).

### Some Common `ar` Command Forms

The basic syntax of `ar` is relatively simple: it takes an **operation code** (something like a main command) and some **modifiers** to pin down the exact behavior.

```bash
ar [operation code][modifiers] <archive filename> <files...>

```

| **Operation code** | **Description**                                                        | **Common modifier** | **Example command**             |
| ------------------ | ---------------------------------------------------------------------- | ------------------- | ------------------------------- |
| **`r`**            | **Insert/replace**: adds files to the archive. If a file with the same name already exists in the archive, it is replaced. | `v` (verbose)       | `ar rv libmy.a file1.o file2.o` |
| **`t`**            | **List**: shows the list of files contained in the archive.             | `v` (verbose)       | `ar t libmy.a`                  |
| **`x`**            | **Extract**: extracts (unpacks) files from the archive.                 | `v` (verbose)       | `ar xv libmy.a`                 |

> Reading the man page is always a good idea: [ar(1) - Linux man page](https://linux.die.net/man/1/ar)

### What About Windows

This job is really handled by the MSVC toolchain, but hardly anyone does it by hand these days. On Windows, most people simply delegate to the colossal IDE — Visual Studio — or, the way I prefer, use the lightweight Visual Studio Code and hand the job to CMake. For the concrete details, go read CMake's verbose build logs; for the sake of space I don't plan to expand on them here.

## Where We Use Static Libraries

I thought it over carefully, pooling my own shallow engineering experience (nearly nonexistent, honestly) and the bits of material I've read: today, static libraries can be almost entirely replaced by dynamic ones, but in these scenarios a static library is clearly the more appropriate choice. I do tend to use static libraries more in embedded work, so that's the angle I'll take:

- **Simpler distribution:** you only need to ship one executable, with no need to carry along a pile of `.dll` (Windows) or `.so`/`.dylib` (Linux/macOS) files.
- **Version lock:** you need to **absolutely guarantee** that your program uses a specific version of a library and won't be disturbed by whatever other versions happen to live on the user's system.
- **Small tools or embedded systems:** environments with strict limits on file count, or on support for dynamic linking.

## And on the Flip Side: Reasons Not to Use a Static Library

Looking back at the previous post, we already explained how static libraries work. So it's easy to arrive at the first reason not to use one:

#### Executable Bloat

When the priority is **interface reuse**, going static clearly makes every library and executable that depends on this one swell dramatically in size (Executable Bloat). So, **for any module whose whole purpose is to provide a functional interface for other dependencies while remaining absolutely self-contained, please use a dynamic library**. In that case we want the code dependency to exist exactly once, and we let the OS and the loader coordinate all the symbol mapping automatically — clearly the better deal.

#### Updates Force a Recompile and a Re-release (Hot Reloading Request)

In scenarios that care about **hot updates**, going static clearly makes no sense. For instance, sometimes directly swapping out the executable is inconvenient, and we only want to update one of its sub-dependencies (say, a library we use had a vulnerability discovered by an enthusiastic open-source-minded programmer who reported it back to you in time) — that is, once we discover a security hole in the library or a bug that needs fixing, going static means we must **recompile and redistribute the entire application (static linking has turned that code into part of the body proper rather than a dependency you swap in and out)**.

#### Potential Symbol Collisions and Version-Management Headaches (Symbol Collisions)

If we link static libraries of **multiple versions**, or with **same-named symbols**, into the same executable, the compiler/linker will try to sort it out, but the risk is high (if I remember correctly, it goes by symbol strong/weak rules, and on a tie it just drops one at random). That is genuinely dangerous — nobody likes their program playing a guessing game.

## The Modern CMake Perspective

In modern projects, this whole hand-driven flow of `ar rvs lib<name>.a` plus `-l<name>`/`-L<dir>` has basically been taken over by CMake. A single line — `add_library(Charlie STATIC src/foo.cpp src/bar.cpp)` — automatically compiles the source files into `.o` and then calls `ar` to pack out `libCharlie.a`: the `STATIC` keyword corresponds to a static library, `SHARED` to a dynamic library, and if you write neither, CMake picks one based on the `BUILD_SHARED_LIBS` switch. On the linking side you no longer have to hand-roll `-l`/`-L` either; `target_link_libraries(myapp PRIVATE Charlie)` gets it done in one line, with CMake automatically expanding it into `-lCharlie` and stuffing the library's directory into `-L`. That `lib`-prefix convention from the start of this post? It's quietly carrying that load for you behind the scenes. As for reasons to pick static like "simpler distribution" or "version lock" — they still stand; it's just that today you no longer have to type `ar` by hand for their sake.
