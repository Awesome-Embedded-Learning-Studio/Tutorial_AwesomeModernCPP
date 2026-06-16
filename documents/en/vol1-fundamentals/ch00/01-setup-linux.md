---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: 'Setting up a C++ development environment on Linux: installing compilers,
  CMake, and VS Code, configuring from scratch to compiling and running your first
  program'
difficulty: beginner
order: 1
platform: host
reading_time_minutes: 12
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: Linux Environment Setup
translation:
  source: documents/vol1-fundamentals/ch00/01-setup-linux.md
  source_hash: 0ee00a722a8be338ca862b41034c4448ffeed6bc87bcb2a92dd7680b70089c1d
  translated_at: '2026-06-16T03:39:28.567794+00:00'
  engine: anthropic
  token_count: 1994
---
# Setting Up the Linux Environment

Before we start writing C++, we need to set up our workspace. The goal of this article is simple: to build a C++ development environment from scratch on Linux that can compile, build, and facilitate comfortable coding. The whole process takes about fifteen minutes, but if this is your first time configuring a Linux environment, set aside half an hour—or, honestly, maybe a day, just to be safe. The prerequisite is that you are already familiar with Linux; if you aren't, jump to the next chapter—Windows Deployment.

Why Linux? To put it plainly, the entire C++ toolchain ecosystem grew up around Unix/Linux. The first line of GCC code was written in 1987, and Clang and CMake are also Unix-first designs. When compiling and debugging C++ code on Linux, the resources you can find, the answers on Stack Overflow, and the CI configurations of open-source projects almost all assume you are running Linux. Furthermore, subsequent tutorials will involve embedded cross-compilation and WSL development, so a Linux environment is an unavoidable foundation. (Personal note: I put Linux before Windows because I prefer developing on Linux; my Windows PC is strictly for gaming. Who wouldn't rush to Linux to write code, right? *Just kidding*.)

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Install the GCC or Clang compiler on a Linux system and verify the version.
> - [ ] Install the CMake build tool and understand its basic role.
> - [ ] Configure VS Code for a handy C++ development environment.
> - [ ] Create a CMake-managed C++ project from scratch and successfully compile and run it.

## Environment Overview

All commands in this article have been verified under the following environments:

- **Operating System**: Ubuntu 22.04 / 24.04 (applicable to Debian-based systems), Fedora 39+, Arch Linux.
- **Shell**: Both Bash and Zsh are acceptable.
- **WSL**: WSL2 (Ubuntu 22.04) built into Windows 11 is also applicable; I will mention WSL-specific considerations later.

If you are using other distributions, the package manager commands will differ slightly, but the logic is exactly the same—install the compiler, install CMake, install the editor. Three things. Here, we assume a beginner is using Linux!

## Step 1 — Install the Compiler

The compiler is the tool we use to translate C++ source code into binary files that the machine can execute. In the Linux world, the two most mainstream C++ compilers are GCC (GNU Compiler Collection) and Clang. The default `build-essential` package on Ubuntu/Debian installs GCC and related build tools all at once, which is our most hassle-free choice.

Execute the corresponding command based on your distribution:

::: code-group

```bash Debian/Ubuntu
sudo apt update && sudo apt install -y build-essential
```

```bash Fedora
sudo dnf install gcc gcc-c++ cmake ninja-build
```

```bash Arch Linux
sudo pacman -S base-devel cmake ninja
```

:::

`build-essential` is a meta package; it doesn't contain any software itself, but it pulls down a series of tools necessary for compilation, such as `gcc`, `g++`, `make`, and `libc6-dev`. Once this package is installed, the basic C and C++ compilation environment is ready.

Arch's default `base-devel` package already includes C++ support, so there is no need to explicitly install `gcc`.

After installation, let's verify it. Open a terminal and execute:

```bash
g++ --version
```

Your output will look something like this (specific version numbers will vary by distribution and update status):

```text
g++ (Ubuntu 11.4.0-1ubuntu1~22.04) 11.4.0
Copyright (C) 2021 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
```

As long as you can see the version number output, GCC is installed successfully. We recommend a GCC version no lower than 11—GCC 11 fully supports most C++20 features, and subsequent tutorials will make extensive use of C++17 and C++20 features. If your distribution's default GCC is older (like Ubuntu 20.04 defaulting to GCC 9), you can consider upgrading via a PPA or compiling from source, though we won't expand on that here.

If you also want to try Clang (we will use Clang for comparison for some features in later tutorials), you can install it like this:

```bash
# Debian/Ubuntu
sudo apt install -y clang

# Fedora
sudo dnf install clang
```

Clang's error messages are a bit friendlier than GCC's. When debugging template metaprogramming, it's common to switch to Clang to see the error hints. However, GCC is completely sufficient for daily development. Keep both installed; they don't conflict.

> ⚠️ **Troubleshooting Warning**: If you execute `sudo apt install` in WSL and get an `E: Unable to locate package` error, don't panic. Most likely, you forgot to run `sudo apt update`, or the WSL distribution wasn't initialized correctly. Run `sudo apt update` in the WSL terminal, then reinstall the package. Also, the default Ubuntu image for WSL can sometimes be old; it's recommended to check your WSL distribution version in the Microsoft Store.

## Step 2 — Install CMake

With the compiler ready, we also need a build tool to manage the project's compilation process. You might ask—can't I just run `g++` directly? For a single file, that's fine, but real-world projects often have dozens or even hundreds of source files with dependencies on each other; manually typing compilation commands is simply unrealistic.

> What, you haven't seen one? Check out GitHub and look at:
>
> - CFBox: <https://github.com/Awesome-Embedded-Learning-Studio/CFBox>
> - CFDesktop: <https://github.com/Awesome-Embedded-Learning-Studio/CFDesktop>
>
> Browse around a bit. I bet you won't want to type compiler commands manually.
> (Of course, I'm not promoting my projects again. I'm sure of it.)

CMake does exactly this: it reads a configuration file called `CMakeLists.txt` and automatically generates the corresponding build scripts (like Makefiles or Ninja files), handling the dirty work of compiling and linking for you.

Installing CMake is also a one-command affair:

```bash
# Debian/Ubuntu
sudo apt install -y cmake

# Fedora
sudo dnf install cmake
```

Verify the installation:

```bash
cmake --version
```

```text
cmake version 3.22.1
```

We recommend a CMake version no lower than 3.16—starting from 3.16, CMake introduced support for C++20 modules and presets, which we will use in our `CMakeLists.txt` later. If the CMake version in your distribution's repository is low, you can install a newer version from the official Kitware repository or pip:

```bash
# Using pip (version often newer)
pip install cmake
```

## Step 3 — Configure VS Code

The choice of editor is subjective. Vim and Emacs are certainly fine, but if you want a C++ development environment that works out of the box and has a mature plugin ecosystem, VS Code is currently the most mainstream choice. Plus, its remote development experience under WSL is excellent—code compiles and runs on Linux, while the editor interface stays on Windows. The best of both worlds.

> Yes, I wrote this tutorial in VS Code! It's great, highly recommended!

There are many ways to install VS Code. The simplest is to go to the [official website](https://code.visualstudio.com/) to download the `.deb` package (Ubuntu/Debian) or `.rpm` package (Fedora), then double-click to install. Arch users can directly `sudo pacman -S code`.

After installing VS Code, we need to install a few key extensions. Open VS Code, press `Ctrl+Shift+X` to enter the Extensions panel, search for and install the following three:

- **C/C++** (by Microsoft) — Provides syntax highlighting, IntelliSense, and debugging support; the cornerstone of VS Code C++ development.
- **CMake Tools** (by Microsoft) — Configure, build, and debug CMake projects directly in VS Code without switching to the terminal.
- **CMake** (by twxs) — Provides syntax highlighting and completion for `CMakeLists.txt`.

## Step 4 — Run Your First CMake Project

Now that all the tools are ready, let's practice—creating a CMake-managed C++ project from scratch, compiling, and running it. If this step goes smoothly, it means the toolchain configuration is problem-free, and we can confidently write code in subsequent chapters.

First, find a place to create a project directory:

```bash
mkdir hello_cmake
cd hello_cmake
```

Then create our first C++ source file `main.cpp`:

```cpp
#include <iostream>

int main() {
    std::cout << "Hello, CMake!" << std::endl;
    return 0;
}
```

This is a simplest C++ program—`#include <iostream>` introduces the standard input/output library, `std::cout` is the standard output stream, and the `<<` operator sends the string to the output stream. `std::endl` inserts a newline and flushes the output buffer, ensuring the content is displayed immediately.

Next, create `CMakeLists.txt`—this file tells CMake how to build our project:

```cmake
cmake_minimum_required(VERSION 3.16)
project(HelloCmake LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(hello main.cpp)
```

Let's break this down line by line. `cmake_minimum_required(VERSION 3.16)` declares the minimum CMake version required for this project; if your CMake version is lower than 3.16, the configuration phase will error out directly rather than producing inexplicable build failures. `project(HelloCmake LANGUAGES CXX)` defines the project name and supported languages—`CXX` is CMake's identifier for C++. `set(CMAKE_CXX_STANDARD 20)` sets the C++ standard to C++20, `set(CMAKE_CXX_STANDARD_REQUIRED True)` ensures the compiler errors out if it doesn't support C++20 instead of silently downgrading. Finally, `add_executable(hello main.cpp)` declares that we are building an executable named `hello` with the source file `main.cpp`.

Now let's build. CMake recommends building in a separate directory to avoid polluting the source code directory with generated temporary files:

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

You will see output similar to this:

```text
-- The C compiler identification is GNU 11.4.0
-- The CXX compiler identification is GNU 11.4.0
-- Detecting compiler CXX features...
-- Detecting compiler CXX features - done
-- Configuring done
-- Generating done
-- Build files have been written to: /.../hello_cmake/build
[ 50%] Building CXX object CMakeFiles/hello.dir/main.cpp.o
[100%] Linking CXX executable hello
[100%] Built target hello
```

Build successful. Now run our program:

```bash
./hello
```

```text
Hello, CMake!
```

If you see this output, congratulations—the compiler, CMake, and the entire toolchain are in place. You are ready for the formal C++ learning journey. If you open this project directory (`hello_cmake`) with VS Code, the CMake Tools extension will automatically recognize `CMakeLists.txt` and configure the project. Build and Run buttons will appear in the status bar at the bottom. From now on, you can compile and run directly in VS Code with a click, no need to type commands every time.

## What to Do When You Encounter Problems

Toolchain configuration varies widely across different machines, so running into issues is normal. Here are a few common errors and their solutions.

**`command not found: g++` or `command not found: cmake`**

This means the corresponding tool isn't installed, or it's installed but not in your `PATH` environment variable. Use `which g++` and `which cmake` to check their locations first—if they return empty, reinstall the corresponding package. If they return a path but the command still isn't found, there's a problem with your `PATH` configuration. Check if `/usr/bin` was accidentally removed from your `PATH` in `~/.bashrc` or `~/.zshrc`.

**CMake reports `No CMAKE_CXX_COMPILER could be found`**

This usually happens in WSL or Docker containers—the system has CMake installed but no compiler. Go back to Step 1 and confirm `g++ --version` outputs normally, then re-run `cmake ..`.

**Compilation reports `undefined reference` linking errors**

You won't encounter this with a single file `g++` command. However, as projects get more complex later, if you see linking errors, it's basically because you forgot to link a library in `CMakeLists.txt`—the `target_link_libraries` command is missing the corresponding library. We will cover this in detail in later chapters.

**Slow file system performance under WSL**

WSL accessing the Windows file system (paths under `/mnt`) is much slower than accessing the native Linux file system. If your project is under `/mnt/c/Users`, compilation will be noticeably laggy. The solution is to put the project in the Linux home directory (e.g., `~/projects`), and edit it via VS Code's Remote - WSL extension.

**Other issues?**

- Check the community.
- Ask AI, or ask the experts around you.
- Send a private email, or go to <https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP> and open an Issue to ask me. I sometimes see Issues faster than emails. As for why this is a separate point: I'm a novice, not really an expert, but I can help look at beginner issues.

## Summary

At this point, we have completed the full setup of the C++ development environment on Linux. Let's review what we did: installed the GCC compiler (via the `build-essential` meta package), installed the CMake build tool, configured VS Code's C++ development extensions, and finally created a CMake project from scratch and successfully compiled and ran it.

This environment is the infrastructure for all subsequent tutorials. Starting from the next chapter, we will officially enter the world of C++. If you are on Windows and don't want to install WSL, the next article will cover the Windows environment setup; if you have successfully run `./hello` here, you can jump straight to the C Language Crash Course chapter and start writing real code.

---

> **Self-Assessment of Difficulty**: If you can complete the operations in this article smoothly and understand the reason for each step, your Linux basic operation skills are in place. If the meaning of certain commands isn't clear yet, don't worry—we will use these tools repeatedly in subsequent chapters, and practice makes perfect.
