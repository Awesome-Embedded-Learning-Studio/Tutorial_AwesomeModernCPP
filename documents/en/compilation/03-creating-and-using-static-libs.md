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
title: "A Deep Dive into C/C++ Compilation and Linking, Part 3: How to Build and Use Static Libraries"
description: 'Use ar to pack object files into a lib<name>.a static library, get clear on why the library name has to start with lib, how the linker finds the library through the -l convention, and when you should actually reach for a static library.'
cpp_standard: [11, 14, 17, 20]
---
# A Deep Dive into C/C++ Compilation and Linking, Part 3: How to Build and Use Static Libraries

In the last post I briefly touched on the basic theory behind static and dynamic libraries. I'll drop the links here:

> [A Deep Dive into C/C++ Compilation and Linking — CSDN blog](https://blog.csdn.net/charlie114514191/article/details/152921903)
>
> [A Deep Dive into C/C++ Compilation and Linking, Part 2: An Introduction to Static and Dynamic Libraries — CSDN blog](https://blog.csdn.net/charlie114514191/article/details/154828385)

So earlier on we already talked through what a static library actually is at its core. Even though today, shipping code through dynamic libraries is the more default strategy, I want to cover static libraries anyway for completeness — and also because I personally like packing anything that only depends on the bare-bones `C/C++` runtime into a static library. (Honestly I don't have some deep technical reason for the choice; I just don't enjoy dumping a giant pile of relocatable files straight onto the linker.)

## So how do you actually make a static library?

### The `ar` tool

So a pretty natural question comes up: last time we learned the basic idea behind a static library (an organic bundle of several relocatable files), but how do you actually build one? The answer is a small but powerful tool called `ar` (Archiver).

Let me give `ar` a quick intro. It's a tool for creating, modifying, and extracting **archive files**. These archives usually end in `.a` (the *a* stands for archive), and the most common use is to bundle up object files (`.o` files) into a **static library**. On Linux, we like to — for a static library at least — say we decide the library's name is going to be `Charlie`. Then the file we generate is generally `libCharlie.a`.

Some of you might be puzzled: why does it have to start with `lib`? Wouldn't `Charlie.a` be way more intuitive? Right, so the core reason is this: **it's a working convention the linker relies on when we come back around to do the linking**. Most of the time, when `gcc`/`g++` is getting ready to link against some target, it dispatches `ld` to link the target libraries and relocatable files, and the upper-layer build tools are in the habit of using `-L` to set the folder search path together with `-l` (that's a lowercase L) to find the library. For example, when we want to feed `main.c` the well-known `math` static library, we'd write something like:


```cpp

gcc main.c -lmath

```

The linker isn't going to go looking for a file literally named `math`. Instead, following the convention, it tries to find a file named **`libmath.a`** (static library) or **`libmath.so`** (dynamic library). Put simply:

- The name after the `-l` flag (`math` in this example) is called the "library name".
- The linker automatically prepends the prefix `lib` to that name.
- Then, depending on the situation (and the priority order), it appends `.a` (static library) or `.so` (dynamic library) and so on, to build the full filename.

So **naming your library file `lib<name>.a` is you proactively playing along with the linker's auto-lookup mechanism**. If you don't name the file this way, the linker can't find it through the convenient `-l` option, and you're stuck with the clumsy fallback of pointing it at the library's full path by hand, which is really annoying. There's also a nastier problem hiding in here, and we'll dig it back up when we get to dynamic libraries (static libraries don't care; their code just gets packed into the target file anyway).

### Some common `ar` command forms

The basic syntax of `ar` is fairly simple. It wants an **operation code** (think of it as a main command) and some **modifiers** to spell out the exact behavior.

```bash
ar [operation code][modifiers] <archive filename> <files...>

```

| **Code** | **Description**                                                              | **Common modifier** | **Example**                     |
| -------- | --------------------------------------------------------------------------- | ------------------- | ------------------------------- |
| **`r`**  | **Insert / replace**: adds files to the archive. If a same-named file already exists in the archive, it gets replaced. | `v` (verbose)       | `ar rv libmy.a file1.o file2.o` |
| **`t`**  | **List**: shows the list of files contained in the archive.                  | `v` (verbose)       | `ar t libmy.a`                  |
| **`x`**  | **Extract**: pulls (unpacks) files out of the archive.                       | `v` (verbose)       | `ar xv libmy.a`                 |

> Reading the man page is always a good idea: [ar(1) - Linux man page](https://linux.die.net/man/1/ar)

### What about Windows?

This part is really handled by the MSVC toolchain, but honestly very few people do it by hand anymore. On Windows, almost everybody delegates to the giant IDE, Visual Studio — or, like me, they prefer the lighter Visual Studio Code and let CMake handle it. You can dig into the verbose CMake build log to see the actual details; I'm not going to expand on it here, mostly for space reasons.

## So where do we actually use static libraries?

I thought about it for a while, pooled together my own shallow engineering experience (you could almost call it none at all) and the bits of material I've read, and honestly, today, static libraries are almost entirely replaceable by dynamic ones. But in these scenarios, a static library is clearly the better fit. I tend to use static libraries more in embedded work, so I'll frame it that way:

- **Simpler distribution:** you only ship one executable, no need to drag along a pile of `.dll` (Windows) or `.so` / `.dylib` (Linux/macOS) files.
- **Version lock:** when you need to **absolutely guarantee** your program is using a specific version of a library and won't get messed with by whatever other versions happen to live on the user's system.
- **Small tools or embedded systems:** in environments with strict limits on file count, or on dynamic-linking support.

## And on the flip side, reasons not to use a static library

Looking back at the last post, we already explained how a static library actually works. So it's easy to come up with the first reason not to use one:

#### Executable bloat

When you care about **reusing an interface**, going static obviously makes every library and executable that depends on it blow up in size (Executable Bloat). So **for anything whose whole purpose is to expose a functional interface to other dependencies — a module that's otherwise fully standalone — please use a dynamic library**. In that case we want the code dependency to live exactly once, and let the OS and loader sort out all the symbol mapping. That's clearly the better call.

#### Updates force a recompile and a re-release (Hot Reloading Request)

In scenarios that care about **hot updates**, going static obviously doesn't make sense. For instance, sometimes it's awkward to just swap out the whole executable, and we'd rather only update one sub-dependency — say a library we use gets a vulnerability found by an enthusiastic open-source programmer who reports it back to you in time. In other words, once we find a security hole in the library, or a bug that needs fixing, going static means we have to **recompile and redistribute the entire application** (static linking has turned that code into part of the body, not just a dependency you swap).

#### Potential symbol collisions and version-management headaches (Symbol Collisions)

If we link **multiple versions** of a static library, or libraries with **same-named symbols**, into a single executable, the compiler / linker will try to sort it out, but the risk is high (if I'm remembering right, it goes by symbol strong/weak rules, and on a tie it just drops one at random). It really is dangerous — nobody likes their program playing a guessing game.

## The modern CMake perspective

This whole hand-rolled `ar rvs lib<name>.a` plus `-l<name>` / `-L<dir>` flow has basically been taken over by CMake in modern projects. One line, `add_library(Charlie STATIC src/foo.cpp src/bar.cpp)`, automatically compiles the source files into `.o` and then calls `ar` to pack out `libCharlie.a` — the `STATIC` keyword maps to a static library, `SHARED` maps to a dynamic library, and if you leave it off CMake picks one based on the `BUILD_SHARED_LIBS` switch. On the linking side you don't have to hand-write `-l` / `-L` anymore either; `target_link_libraries(myapp PRIVATE Charlie)` gets it done in one shot, and CMake auto-expands that into `-lCharlie` and stuffs the library's directory into `-L`. That `lib` prefix convention from the start of this post? It's quietly carrying that load for you behind the scenes. As for "simpler distribution" and "version lock" — those reasons to pick static still hold; it's just that today you don't have to type `ar` by hand for them anymore.
