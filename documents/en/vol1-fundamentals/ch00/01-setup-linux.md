# Setting Up the Linux Environment

Before we start writing C++, we need to set up our workspace. The goal here is simple: build a C++ development environment on Linux from scratch that can compile, build, and provide a comfortable coding experience. The whole process takes about fifteen minutes, but if this is your first time configuring a Linux environment, set aside thirty minutes—or, honestly, maybe a whole day, just to be safe. This assumes you are already familiar with Linux. If you are not, skip ahead to the next chapter on Windows setup.

Why Linux? To put it bluntly, the entire C++ toolchain ecosystem grew up around Unix/Linux. The first line of GCC code was written in 1987, and both Clang and CMake were designed with a Unix-first philosophy. When compiling and debugging C++ on Linux, the documentation you find, the answers on Stack Overflow, and the CI configurations of open-source projects almost all assume you are running Linux. Furthermore, later in this tutorial, we will cover embedded cross-compilation and WSL development, making a Linux environment an unavoidable foundation. (Personal bias: I put Linux before Windows simply because I prefer developing on Linux. My Windows machine is purely for gaming—who wouldn't eagerly rush to Linux to write code? *cough*)

> **Learning Objectives**
>
> - After completing this chapter, you will be able to:
> - [ ] Install the GCC or Clang compiler on a Linux system and verify the version
> - [ ] Install the CMake build system and understand its basic role
> - [ ] Configure VS Code as a handy C++ development environment
> - [ ] Create a CMake-managed C++ project from scratch and successfully compile and run it

## Environment Notes

All commands in this chapter have been verified under the following environments:

- **Operating System**: Ubuntu 22.04 / 24.04 (applicable to Debian-based distros), Fedora 39+, Arch Linux
- **Shell**: Bash / Zsh
- **WSL**: WSL2 (Ubuntu 22.04) built into Windows 11 is also applicable; we will mention a few WSL-specific notes later

If you are using a different distribution, the package manager commands will differ slightly, but the approach is exactly the same: install the compiler, install CMake, install the editor—three things. Here, we will assume you are a beginner using Linux!

## Step One — Install the Compiler

A compiler is the tool that translates our C++ source code into binary files that the machine can execute. In the Linux world, the two most mainstream C++ compilers are GCC (GNU Compiler Collection) and Clang. On Ubuntu/Debian, the `build-essential` package conveniently installs GCC along with related build tools all at once, making it our most effortless choice.

Run the corresponding command based on your distribution:

::: code-group

```bash [Ubuntu / Debian]
sudo apt update && sudo apt install build-essential -y
```

```bash [Fedora]
sudo dnf install gcc-c++ make -y
```

```bash [Arch Linux]
sudo pacman -S gcc make
```

:::

`build-essential` is a meta package. It does not contain any software itself, but it pulls down `g++`, `gcc`, `make`, `libc6-dev`, and a series of other tools essential for compilation. Once you install this single package, the basic C and C++ compilation environment is ready to go.

On Arch, the default `gcc` package already includes C++ support, so there is no need to install `gcc-c++` separately.

After installation, let's verify it. Open a terminal and run:

```bash

g++ --version
```

Your output will look something like this (the exact version number will vary depending on your distribution and update status):

```text
g++ (Ubuntu 13.2.0-23ubuntu4) 13.2.0
Copyright (C) 2023 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
```

As long as you can see a version number, GCC is installed successfully. We recommend using GCC version 11 or higher—GCC 11 fully supports most C++20 features, and we will heavily use C++17 and C++20 features in later chapters. If your distribution's default GCC version is older (for example, Ubuntu 20.04 defaults to GCC 9), you can consider upgrading via a PPA or by compiling from source, but we will not dive into that right now.

If you also want to try Clang (we will use Clang for comparisons with certain features in later chapters), you can install it like this:

```bash
# Ubuntu / Debian
sudo apt install clang -y

# 验证
clang++ --version
```

```text
Ubuntu clang version 17.0.6 (++20231206065830+6009708b4367-1~exp1~20231206065905.65)
Target: x86_64-pc-linux-gnu
Thread model: posix
InstalledDir: /usr/bin
```

Clang's error messages are a bit friendlier than GCC's. When debugging template metaprogramming, we often switch to Clang just to read the error messages. However, GCC is perfectly sufficient for daily development. Keep both compilers installed; they do not conflict.

> ⚠️ **Pitfall Warning**: If running `g++ --version` in WSL gives you a `command not found` error, don't panic. Most likely, you forgot to run `sudo apt update`, or your WSL distribution was not initialized correctly. Run `sudo apt update && sudo apt upgrade -y` in your WSL terminal, then reinstall `build-essential`. Additionally, the default Ubuntu image for WSL can sometimes be outdated; we recommend checking your WSL distribution version in the Microsoft Store.

## Step Two — Install CMake

With the compiler in place, we still need a build system to manage the project's compilation process. You might ask—can't we just run `g++ hello.cpp -o hello` directly? For a single file, that is fine. But real-world projects often have dozens or even hundreds of source files with interdependencies, making manual compilation commands completely impractical.

> What, you haven't seen one? Try this: open GitHub and browse
>
> - CFBox: <https://github.com/Awesome-Embedded-Learning-Studio/CFBox>
> - CFDesktop: <https://github.com/Awesome-Embedded-Learning-Studio/CFDesktop>
>
> Look around—I bet you definitely will not want to type out compiler commands manually
> (Of course, I am not promoting my projects again, I am certain of it)

This is exactly what CMake does: it reads a configuration file called `CMakeLists.txt` and automatically generates the corresponding build scripts (like Makefiles or Ninja files), handling the dirty work of compilation and linking for you.

Installing CMake is equally simple—one command does the trick:

```bash
# Ubuntu / Debian
sudo apt install cmake -y

# Fedora
sudo dnf install cmake -y

# Arch
sudo pacman -S cmake

# Yay用户狂喜
yay -S cmake
```

Verify the installation:

```bash
cmake --version
```

```text
cmake version 3.28.3

CMake suite maintained and supported by Kitware (kitware.com/cmake).
```

We recommend a CMake version of 3.16 or higher—starting from 3.16, CMake introduced support for C++20 modules and presets, which we will use in the `CMakeLists.txt` files written later in this tutorial. If the CMake version in your distribution's repository is too low, you can install a newer version from the official Kitware repository or via pip:

## Step Three — Configure VS Code

The choice of editor is subjective. Vim and Emacs are perfectly fine, but if you want an out-of-the-box C++ development environment with a mature plugin ecosystem, VS Code is currently the most mainstream choice. Plus, its remote development experience under WSL is quite polished—code compiles and runs on Linux while the editing interface stays on Windows, giving you the best of both worlds.

> Yes, I wrote this tutorial in VS Code! It is a great tool, highly recommended!

There are many ways to install VS Code. The easiest is to go to the [official website](https://code.visualstudio.com/) and download the `.deb` package (for Ubuntu/Debian) or the `.rpm` package (for Fedora), then double-click to install. Arch users can simply run `sudo pacman -S code`.

Once VS Code is installed, we need to add a few key extensions. Open VS Code, press `Ctrl+Shift+X` to open the Extensions panel, and search to install the following three:

- **C/C++** (by Microsoft) — provides syntax highlighting, IntelliSense, and debugging support; the cornerstone of writing C++ in VS Code
- **CMake Tools** (by Microsoft) — allows you to configure, build, and debug CMake projects directly in VS Code without switching to a terminal
- **CMake** (by twxs) — provides syntax highlighting and autocompletion for `CMakeLists.txt`

## Step Four — Run Your First CMake Project

Now that all the tools are in place, let's get some hands-on practice—creating a CMake-managed C++ project from scratch, then compiling and running it. If this step goes smoothly, it means the entire toolchain is configured correctly, and we can confidently move on to writing code in the following chapters.

First, find a place to create a project directory:

```bash
mkdir -p ~/projects/hello_cmake && cd ~/projects/hello_cmake
```

Then, create our first C++ source file, `hello.cpp`:

```cpp
#include <iostream>

int main()
{
    std::cout << "Hello, Modern C++!" << std::endl;
    return 0;
}
```

This is the simplest possible C++ program—`#include <iostream>` includes the standard input/output library, `std::cout` is the C++ standard output stream, and the `<<` operator sends the string to the output stream. `std::endl` not only adds a newline but also flushes the output buffer, ensuring the content is displayed immediately.

Next, create `CMakeLists.txt`—this file tells CMake how to build our project:

```cmake
cmake_minimum_required(VERSION 3.16)
project(hello_cmake LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(hello hello.cpp)
```

Let's break this down line by line. `cmake_minimum_required(VERSION 3.16)` declares the minimum CMake version required for this project; if your CMake version is lower than 3.16, it will error out during the configuration phase rather than causing a confusing build failure later. `project(hello_cmake LANGUAGES CXX)` defines the project name and supported languages—`CXX` is CMake's internal identifier for C++. `set(CMAKE_CXX_STANDARD 20)` sets the C++ standard to C++20, and `CMAKE_CXX_STANDARD_REQUIRED ON` ensures that if the compiler does not support C++20, it will error out immediately instead of silently downgrading. Finally, `add_executable(hello hello.cpp)` declares that we want to build an executable named `hello` from the source file `hello.cpp`.

Now, let's build. CMake's recommended approach is to build in a separate directory to avoid polluting the source code directory with generated temporary files:

```bash
mkdir build && cd build
cmake ..
make
```

You will see output similar to this:

```text
-- The CXX compiler identification is GNU 13.2.0
-- Detecting CXX compiler ABI info
-- Detecting CXX compiler ABI info - done
-- Check for working CXX compiler: /usr/bin/c++ - skipped
-- Detecting CXX compile features
-- Detecting CXX compile features - done
-- Configuring done (0.3s)
-- Generating done (0.0s)
-- Build files have been written to: /home/charlie/projects/hello_cmake/build
[ 50%] Building CXX object CMakeFiles/hello.dir/hello.cpp.o
[100%] Linking CXX executable hello
[100%] Built target hello
```

Build successful. Now let's run our program:

```bash
./hello
```

```text
Hello, Modern C++!
```

If you see this line of output, congratulations—the compiler, CMake, and the entire toolchain are all in place, and you are ready to begin your formal C++ learning journey. If you open this project directory in VS Code (`code ~/projects/hello_cmake`), the CMake Tools extension will automatically recognize `CMakeLists.txt` and configure the project. Build and run buttons will appear in the bottom status bar, so you can compile and run directly in VS Code from now on without typing commands every time.

## What to Do If You Run Into Problems

Toolchain configuration can vary significantly from machine to machine, so running into issues is normal. Here are a few of the most common errors and their solutions.

**`g++: command not found` or `cmake: command not found`**

This means the corresponding tool is not installed, or it is installed but not in your `PATH` environment variable. First, use `which g++` and `which cmake` to check their locations—if they return empty, reinstall the corresponding packages. If they return a path but the command is still not found, then there is an issue with your `PATH` configuration. Check if `/usr/bin` was removed from `PATH` in your `~/.bashrc` or `~/.zshrc`.

**CMake reports `CMake Error: Could not find CMAKE_CXX_COMPILER`**

This usually happens in WSL or Docker containers—the system has CMake installed but no compiler. Go back to step one, confirm that `g++ --version` outputs normally, and then re-run `cmake ..`.

**Linker errors like `undefined reference to symbol` during compilation**

You will not encounter this issue with a single-file `hello.cpp`. However, as projects become more complex later on, if you hit a linker error, it almost always means you forgot to link a library in `CMakeLists.txt`—the `target_link_libraries` command is missing the corresponding library. We will cover this in detail in later chapters.

**Slow file system performance under WSL**

WSL accesses the Windows file system (paths under `/mnt/c/`) much more slowly than the native Linux file system. If your project is located under `/mnt/c/Users/.../projects/`, compilation speed will be noticeably sluggish. The solution is to place your project in the Linux-side home directory (`~/projects/`) and edit it via VS Code's Remote - WSL extension.

**Other issues?**

- Ask the community
- Ask AI, or ask the experts around you
- Send me a direct message or email, or open an issue at <https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP>. I actually check issues faster than I check emails—for the reason I mentioned above, I am not an expert, but I can help take a look at beginner-level questions

## Summary

At this point, we have completed the full setup of a C++ development environment on Linux. Let's review what we did: we installed the GCC compiler (via the `build-essential` meta package), set up the CMake build system, configured VS Code's C++ development extensions, and finally created a CMake project from scratch and successfully compiled and ran it.

This environment serves as the infrastructure for all subsequent tutorials. Starting from the next chapter, we will officially enter the world of C++. If you are on Windows and do not want to install WSL, the next chapter will cover Windows environment setup separately. If you have already successfully run `Hello, Modern C++!` here, you can skip straight to the C language crash course chapter and start writing real code.

---

> **Self-Assessment**: If you were able to smoothly complete all the operations in this chapter and understand the reason behind each step, your basic Linux skills are up to par. If you are still unclear about the meaning of certain commands, don't worry—we will use these tools repeatedly in subsequent chapters, and practice makes perfect.
