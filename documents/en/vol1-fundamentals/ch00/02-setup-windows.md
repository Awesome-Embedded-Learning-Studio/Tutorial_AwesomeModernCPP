---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: 'Setting up a C++ development environment on Windows: install Visual Studio
  or MinGW, configure CMake and vcpkg, and go from zero to compiling and running.'
difficulty: beginner
order: 2
platform: host
reading_time_minutes: 12
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: Windows Environment Setup
translation:
  source: documents/vol1-fundamentals/ch00/02-setup-windows.md
  source_hash: c423c4bf518b24acb374c4b41c8384f5f8ab532ad75506bc8157be87d76d1cf8
  translated_at: '2026-09-25T09:44:48+00:00'
  engine: anthropic
  token_count: 2660
---
# Windows Environment Setup

C++ development on Windows is, frankly, a bit of a hellscape. Really — for me at least.

Compiler versions, environment variables, spaces in paths — enough to drive a person mad. And buddy, if even a shred of Chinese sneaks in somewhere along the way, you're in for a treat: CMD will hand you an entire screen of errors, and you won't be able to laugh it off!

But things are much better now. The C++ toolchain on Windows has become genuinely mature: whether you want Microsoft's favorite son MSVC, or you're more at home with the GCC workflow from Linux, you can find a setup that feels right in the hand. In this article we'll build the Windows C++ development environment from top to bottom, so the toolchain won't be what stalls you later when you're writing code.

There are two mainstream compiler routes on Windows. One is Microsoft's Visual Studio (the MSVC compiler) — the mainstream choice for native Windows development, with a deeply integrated IDE and a first-class debugging experience. The other is MinGW-w64 (installed via MSYS2), which essentially ports the GCC toolchain to Windows; if you've written C++ on Linux before, this will feel very familiar. Both routes pair perfectly with CMake and vcpkg, so which one you pick is purely a matter of personal preference.

**But one thing I should say up front: from here on, this tutorial assumes development on Linux by default — build commands, verification, and all the rest. No deep reason; it's just convenient. Windows folks, please adapt on your own!**

## Step 1 (Option A) — Installing Visual Studio 2022 Community

Visual Studio Community is the free edition Microsoft provides, and it's fully sufficient for individual developers and small teams. First, head to the [Visual Studio download page](https://visualstudio.microsoft.com/downloads/) and grab the online installer for the Community edition; once you run it, a workload selection screen pops up.

The key in this step is picking the right workload — what we need is **"Desktop development with C++"**. Just tick that box and leave the default components on the right untouched; the MSVC v143 compiler and the Windows SDK are included automatically. The whole installation takes roughly 6-8 GB of disk space, so if your connection is slow it might take a while.

Once the installation finishes, let's verify that the compiler actually works. Unlike GCC, Visual Studio can't be used directly from an ordinary terminal — it needs a special environment, the Developer Command Prompt. Search the Start menu for "Developer Command Prompt" or "Developer PowerShell for VS 2022", open it, and type:

```powershell
cl
```

If everything is normal, you'll see output like this:

```text
用于 x64 的 Microsoft (R) C/C++ 优化编译器版本 19.42.34435.0
版权所有(C) Microsoft Corporation。保留所有权利。

用法: cl [ 选项... ] 文件名... [ /link 链接选项... ]
```

Seeing this usage banner means the MSVC compiler is in place. Note that it says x64 here; if you opened the x86 flavor of the Developer Command Prompt, it will say x86 instead. Both work — this tutorial just uses the x64 version throughout.

> If you type `cl` in an ordinary PowerShell or CMD, you'll most likely get "'cl' is not recognized as an internal or external command". That's because MSVC's environment variables are only set inside the Developer Command Prompt. **Do not try to add the environment variables by hand. Do not try to add the environment variables by hand. Do not try to add the environment variables by hand. Just use the Developer Command Prompt.**

Visual Studio 2022 ships with native support for CMake. Open VS, choose "Open a Local Folder", and point it at a directory containing a `CMakeLists.txt`; VS will recognize and configure the project automatically, no extra installation steps required. That said, if you want to use the `cmake` command from the command line, you still need to confirm CMake is on your PATH — run `cmake --version` in the Developer Command Prompt, and if you get a version number back, you're fine.

As for debugging and the rest, please check other blog posts — I've thought it over and won't be teaching it here. This IDE and I simply don't get along.

## Step 1 (Option B) — Installing MinGW-w64 via MSYS2

If you're more at home with the GCC ecosystem, or your project needs cross-platform builds and a workflow consistent with Linux, then MSYS2 + MinGW-w64 is the better choice. MSYS2 essentially provides a Linux-like package management environment on Windows, installing and managing the toolchain through `pacman` (yes, the very same pacman from Arch Linux).

First, go to the [MSYS2 website](https://www.msys2.org/) and download the installer; by default it installs into `C:\msys64`. When installation finishes, an MSYS2 terminal window pops up automatically. Let's update the system first:

```bash
pacman -Syu
```

This updates the core system packages; when it's done, the terminal may close on its own — just reopen an MSYS2 UCRT64 terminal (note: UCRT64, not the default MSYS2 one). Then we install the GCC toolchain and CMake:

```bash
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja
```

A quick word on why we pick UCRT64 over MINGW64. UCRT (Universal C Runtime) is the newer C runtime Microsoft introduced with Windows 10 and later; it has better API compatibility and is the environment officially recommended by MSYS2. If your system is Windows 10 or newer, UCRT64 is simply the right choice.

> MSYS2 has several sub-environments (MSYS2, MINGW32, MINGW64, UCRT64, CLANG64), and the installed package names carry different prefixes. In the UCRT64 environment, package names start with `mingw-w64-ucrt-x86_64-` — don't install into the wrong environment. A simple way to check is the terminal window's title bar, or run `echo $MSYSTEM`, which should print `UCRT64`.

After installation, we need to add MinGW's bin directory to the system PATH, so gcc and cmake also work in ordinary CMD and PowerShell. Add `C:\msys64\ucrt64\bin` to the system PATH environment variable.

Then open an ordinary PowerShell or CMD and verify:

```powershell
g++ --version
```

If things are normal, you'll see:

```text
g++ (Rev2, Built by MSYS2 project) 14.2.0
Copyright (C) 2024 Free Software Foundation, Inc.
本程序是自由软件；请参看源代码的版权声明。本软件没有任何担保；
包括没有适销性和某一专用目的下的适用性担保。
```

Let's verify CMake as well:

```powershell
cmake --version
```

```text
cmake version 3.28.3

CMake suite maintained and supported by Kitware (kitware.com/cmake).
```

With both commands producing output, the toolchain is installed successfully.

## Step 2 — Building Your First Project with CMake

With the toolchain in place, let's actually run a CMake project to make sure the whole build pipeline works. Whichever route you picked, the CMake project is written the same way — only the build commands differ.

First, create a project directory and put two files in it. The first is `hello.cpp`:

```cpp
#include <iostream>

int main()
{
    std::cout << "Hello from Windows C++ toolchain!" << std::endl;
    std::cout << "Compiler: "
#if defined(_MSC_VER)
              << "MSVC " << _MSC_VER
#elif defined(__GNUC__)
              << "GCC " << __GNUC__ << "." << __GNUC_MINOR__
#else
              << "Unknown"
#endif
              << std::endl;
    return 0;
}
```

This code uses preprocessor macros to detect the current compiler, so at a glance you can tell whether MSVC or GCC is doing the work. Next, the matching `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.16)
project(HelloWindows LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(hello hello.cpp)
```

This CMakeLists is minimal: set the minimum CMake version, declare the project name and language, set the C++17 standard, and finally define an executable target. Nothing fancy at all, but as scaffolding to verify the toolchain it's plenty.

Now let's build. If you're on Visual Studio (MSVC), open the Developer Command Prompt for VS 2022, cd into the project directory, and run:

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

If you're on MinGW-w64, run this in PowerShell or CMD:

```powershell
cmake -B build -G "MinGW Makefiles"
cmake --build build
```

When using the MinGW Makefiles generator, other programs on your PATH that ship a `make.exe` (say, the one bundled with Qt, or some older MinGW) can break the build. If you hit that problem, you can explicitly point CMake at the make executable when configuring: `cmake -B build -G "MinGW Makefiles" -DCMAKE_MAKE_PROGRAM=C:/msys64/ucrt64/bin/mingw32-make.exe`.

On either route, a successful build produces a `hello.exe` under the `build` directory (or `Release/hello.exe`). Run it:

```powershell
# MSVC route
.\build\Release\hello.exe

# MinGW route
.\build\hello.exe
```

The output should look something like this:

```text
Hello from Windows C++ toolchain!
Compiler: MSVC 1942
```

Or:

```text
Hello from Windows C++ toolchain!
Compiler: GCC 14.2
```

Once you see the compiler name printed correctly, the entire toolchain is fully working. Great — at this point we have a functioning compilation environment.

## Step 3 — Configuring the Development Environment in VS Code

Whichever compiler route you took, VS Code is a very nice lightweight editor choice. We need to install the following extensions: **C/C++** (from Microsoft, providing syntax highlighting and debugging support) and **CMake Tools** (CMake project management and building). If you prefer a Chinese interface, just add the Chinese Language Pack as well.

> For the "IntelliSense" features — code completion, jump-to-definition, and friends — this tutorial recommends installing the **clangd** extension separately to manage them (more accurate than the C/C++ extension); keep the C/C++ extension in charge of debugging. For the detailed how-to, see [Getting Started volume · Installing clangd](/getting-started/05-vscode-clangd).

The CMake Tools extension automatically detects the compilers on your system. With the extensions installed, open our project directory and a "Kit" selector appears in VS Code's bottom status bar — click it to choose the compiler to use; if you have both MSVC and MinGW installed, you can switch between them here. Once you've picked one, CMake Tools configures the project automatically, and the status bar shows the build configuration and compiler info.

On the debugging side, CMake Tools integrates very well. Hover the mouse over the project name at the bottom of the status bar and a debug button (the little bug icon) appears next to it — click it to launch debugging directly. If you want manual control over the debug configuration, you can write one in `.vscode/launch.json`. For the MinGW route, a typical configuration looks like this:

```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Debug hello",
            "type": "cppdbg",
            "request": "launch",
            "program": "${workspaceFolder}/build/hello.exe",
            "args": [],
            "stopAtEntry": false,
            "cwd": "${workspaceFolder}",
            "environment": [],
            "externalConsole": false,
            "MIMode": "gdb",
            "miDebuggerPath": "C:/msys64/ucrt64/bin/gdb.exe",
            "setupCommands": [
                {
                    "description": "Enable pretty-printing for gdb",
                    "text": "-enable-pretty-printing",
                    "ignoreFailures": true
                }
            ]
        }
    ]
}
```

For the MSVC route, just change `MIMode` to `"vsdbg"` and drop `miDebuggerPath`; VS's debugger takes over automatically.

And with that, the C++ development environment on Windows is fully set up. We have a compiler (MSVC or GCC), a build system (CMake), a package manager (vcpkg), and an editor (VS Code) — the whole toolchain is ready to run.
