# Deep Dive into C/C++ Compilation and Linking Part 3: How to Create and Use Static Libraries

In the previous blog post, I briefly covered the basic introduction to static and dynamic libraries. I have linked them here:

> [Deep Dive into C/C++ Compilation and Linking - CSDN Blog](https://blog.csdn.net/charlie114514191/article/details/152921903)
>
> [Deep Dive into C/C++ Compilation and Linking Part 2: Introduction to Dynamic and Static Libraries - CSDN Blog](https://blog.csdn.net/charlie114514191/article/details/154828385)

So earlier, we simply explained what the essence of a static library is. Although today, using dynamic libraries for code sharing is a more fundamental strategy, for the sake of completeness—and because I personally like using static libraries to package something that only depends on `C/C++` the most basic runtime (honestly, I have no solid technical reason for this choice; I just don't like feeding a massive chunk of relocatable files directly to the linker)—let's continue.

## How Do We Create a Static Library?

### `ar` Tool

So a natural question arises: we previously learned the fundamental principle of a static library (an organic combination of several relocatable files), but how do we actually create one? The answer is a small yet powerful tool—`ar` (Archiver).

Let me briefly introduce `ar`! It is a tool used to create, modify, and extract **archive files**. These archive files typically end with the `.a` extension (where "a" stands for archive), and their most common use case is packaging object files (`.o` files) to create **static libraries**. On Linux, if we are creating a static library (at least a static library) and decide to name it `Charlie`, the generated library will generally be `libCharlie.a`.

Some might wonder why it must start with `lib`. Wouldn't generating `Charlie.a` be more intuitive? The core reason is: **this is dictated by the working conventions of the linker when we perform linking later**. Most commonly, when we `gcc/g++` compile and prepare to link objects, we dispatch `ld` to link the target libraries and relocatable files. Generally, upper-level build tools prefer using the `-L` flag to specify the search directory combined with `-l` (a lowercase L) to find the library. For example, when we try to provide the well-known `math` static library to `main.c` at a specified path, we might write something like this:

```cpp

gcc main.c -lmath

```

The linker does not directly look for a file named `math`. Instead, following convention, it attempts to find a file named **`libmath.a`** (static library) or **`libmath.so`** (dynamic library). Simply put:

- The name following the `-l` flag (`math` in this example) is called the "library name".
- The linker automatically prepends the prefix `lib` to this name.
- Then, depending on the situation (and priority), it appends a suffix like `.a` (static library) or `.so` (dynamic library) to form the complete filename.

Therefore, **naming the library file in the `lib<name>.a` format is to proactively cater to the linker's automatic search mechanism**. If the library file is not named in this format, the linker cannot find it via the convenient `-l` option. You would have to link by directly specifying the full path to the library file, which is very cumbersome. Furthermore, this leads to a serious issue that we will revisit when discussing dynamic libraries (for static libraries, it doesn't matter since they get packaged into the target file).

### Common Command Formats for `ar`

The basic syntax of `ar` is relatively simple. It requires an **operation code** (similar to a main command) and some **modifiers** to specify the exact behavior.

```bash
ar [操作码][修饰符] <归档文件名> <文件...>

```

| **Operation Code** | **Description**                                                     | **Common Modifiers**     | **Example Command**                    |
| ---------- | ------------------------------------------------------------ | ------------------ | ------------------------------- |
| **`r`**    | **Insert/Replace**: Adds files to the archive. If a file with the same name already exists, it replaces it. | `v` (verbose output) | `ar rv libmy.a file1.o file2.o` |
| **`t`**    | **List**: Displays the list of files contained in the archive.                         | `v` (verbose output) | `ar t libmy.a`                  |
| **`x`**    | **Extract**: Extracts (unpacks) files from the archive.                         | `v` (verbose output) | `ar xv libmy.a`                 |

> Checking the man page is always a good idea: [ar(1) - Linux man page](https://linux.die.net/man/1/ar)

### What About Windows?

This is actually handled by the MSVC toolchain. However, few people do this manually on Windows. On Windows, most people delegate this task to the massive IDE: Visual Studio, or like me, use lightweight Visual Studio Code and delegate to CMake. For specific details, you can check the detailed build logs from CMake, but I won't expand on that here for the sake of brevity.

## Where Do We Use Static Libraries?

I thought about this carefully, combining my shallow engineering experience (which I can honestly say is almost non-existent) with the materials I have read. In fact, today, static libraries can almost entirely be replaced by dynamic libraries. However, in the following scenarios, using static libraries is clearly more appropriate. Of course, I use static libraries more often in embedded systems, so I will frame it this way:

- **Simplified distribution:** We only need to distribute a single executable file, without carrying a bunch of `.dll` (Windows) or `.so`/`.dylib` (Linux/macOS) files.
- **Version locking:** We need to **absolutely guarantee** that our program uses a specific version of a library, free from interference by other versions on the user's system.
- **Small tools or embedded systems:** In environments with strict limitations on the number of files or dynamic linking support.

## Conversely, What Are the Reasons Not to Use Static Libraries?

Reviewing the previous blog post, we have already explained how static libraries work. So, it is easy to think of the first reason not to use them:

#### Executable Bloat

When focusing on **interface reuse**, using static libraries obviously causes the size of all dependent libraries and executables to increase dramatically (executable bloat). Therefore, **for any module whose purpose is to provide functional interfaces to other dependencies and that operates independently, please use a dynamic library**. In this case, keeping only one copy of the code dependency and letting the operating system and loader automatically coordinate all mapped symbol relationships is clearly the better approach.

#### Updates Require Recompilation and Redistribution (Hot Reloading Request)

In scenarios that prioritize **hot reloading**, using static libraries is clearly inappropriate. For example, when we cannot easily replace the executable file itself but only need to update a sub-dependency (say, a library we use has a vulnerability discovered by an enthusiastic open-source developer who promptly reports it to us)—that is, when we discover a security vulnerability or a bug in the library that needs fixing—with a static library, we must **recompile and redistribute the entire application (static linking makes this code a part of the main body rather than a required dependency)**.

#### Potential Symbol Collisions and Version Management Issues (Symbol Collisions)

If we link **multiple versions** of static libraries or static libraries with **identical symbol names** into the same executable, the compiler/linker will attempt to resolve them, but the risk is very high (if I remember correctly, it drops them randomly based on symbol strength and equality). This is truly dangerous; nobody likes playing a guessing game with their program.
