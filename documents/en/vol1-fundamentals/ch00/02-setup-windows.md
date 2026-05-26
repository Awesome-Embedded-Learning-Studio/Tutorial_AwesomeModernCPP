---
title: Windows Environment Setup
description: 'Setting up a C++ development environment on Windows: installing Visual
  Studio or MinGW, configuring CMake and vcpkg, from scratch to compiling and running'
chapter: 0
order: 2
difficulty: beginner
reading_time_minutes: 15
platform: host
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
cpp_standard:
- 11
- 14
- 17
- 20
translation:
  source: documents/vol1-fundamentals/ch00/02-setup-windows.md
  source_hash: 4952192019c839bc9675709aeb7dcdcd7cc99411fc7d616fea443aee76c851b8
  translated_at: '2026-05-26T10:41:24.717946+00:00'
  engine: anthropic
  token_count: 2319
---
# Windows Environment Setup

> ⚠LLM: The author hasn't had the time to thoroughly verify this section. Experts are welcome to provide corrections!

Honestly, setting up a C++ development environment on Windows used to be quite a hassle—different compiler versions, environment variables, and spaces in paths could drive anyone crazy. But things are much better now. The C++ toolchain on Windows is quite mature; whether you prefer Microsoft's own MSVC or the GCC workflow you're used to on Linux, you'll find a setup that suits you. In this chapter, we'll set up a C++ development environment on Windows from scratch, ensuring we won't get stuck on toolchain issues later when writing code.

There are two main C++ compiler routes on Windows. One is Microsoft's Visual Studio (MSVC compiler), the mainstream choice for native Windows development, offering highly integrated IDE features and a top-tier debugging experience. The other is MinGW-w64 (installed via MSYS2), which essentially brings the GCC toolchain to Windows. If you've written C++ on Linux before, this will feel very familiar. Both routes work perfectly with CMake and vcpkg, so choosing one is purely a matter of personal preference.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Install and configure Visual Studio 2022 (MSVC) or MinGW-w64 (MSYS2) compilers
> - [ ] Use CMake to build and successfully run a C++ project
> - [ ] Install vcpkg and use it to manage third-party library dependencies
> - [ ] Configure a C++ development and debugging environment in VS Code

## Environment Overview

This chapter is based on Windows 10/11. All commands and screenshots were verified with the following versions:

- **Operating System**: Windows 11 23H2 (Windows 10 21H2+ also applies)
- **Option A**: Visual Studio 2022 Community 17.14 (MSVC v143)
- **Option B**: MSYS2 + MinGW-w64 UCRT64 (GCC 14.x)
- **Build Tool**: CMake 3.28+
- **Editor**: VS Code 1.90+ (with C/C++ / CMake Tools extensions)

You only need to choose one route; there's no need to install both. If you don't have a strong preference, we recommend going with the Visual Studio route for a more hassle-free experience.

## Step One (Option A) — Install Visual Studio 2022 Community

Visual Studio Community is a free version provided by Microsoft, and its features are fully sufficient for individual developers and small teams. First, we go to the [Visual Studio download page](https://visualstudio.microsoft.com/downloads/) to get the Community edition online installer. After running it, a workload selection interface will pop up.

The key here is selecting the right workload—we need **"Desktop development with C++"**. Just check that box, and leave the default components on the right alone. The MSVC v143 compiler and Windows SDK will be included automatically. The entire installation requires about 6-8 GB of disk space, so it might take a while if your internet connection is slow.

After the installation is complete, let's verify that the compiler is available. Unlike GCC, Visual Studio can't be used directly in a regular terminal. It requires a special environment—the Developer Command Prompt. Search for "Developer Command Prompt" or "Developer PowerShell for VS 2022" in the Start menu, open it, and type:

```powershell
cl
```

If everything is normal, you'll see output similar to this:

```text
用于 x86 的 Microsoft (R) C/C++ 优化编译器版本 19.42.34435.0
版权所有(C) Microsoft Corporation。保留所有权利。

用法: cl [ 选项... ] 文件名... [ /link 链接选项... ]
```

Seeing this usage prompt means the MSVC compiler is in place. Note that this shows x86. If you opened the x64 version of the Developer Command Prompt, it will show x64. Both work fine, but this tutorial will consistently use the x64 version.

> ⚠️ **Pitfall Warning**: If you directly type `cl` in a regular PowerShell or CMD, you'll most likely get "'cl' is not recognized as an internal or external command". This is because MSVC's environment variables are only set in the Developer Command Prompt. Don't try to add environment variables manually; just use the Developer Command Prompt.

Visual Studio 2022 has built-in native support for CMake. Open VS, select "Open a Local Folder" and point it to a directory containing a `CMakeLists.txt`, and VS will automatically recognize and configure the project without any additional installation steps. However, if you want to use the `cmake` command on the command line, you still need to confirm that CMake is in your PATH—run `cmake --version` in the Developer Command Prompt, and if you can see the version number, you're good to go.

## Step One (Option B) — Install MinGW-w64 via MSYS2

If you're more comfortable with the GCC ecosystem, or if your project requires cross-platform compilation and a workflow consistent with Linux, then MSYS2 + MinGW-w64 is the better choice. MSYS2 essentially provides a Linux-like package management environment on Windows, using `pacman` (yes, the same pacman from Arch Linux) to install and manage the toolchain.

First, go to the [MSYS2 website](https://www.msys2.org/) to download the installer, and install it to the default `C:\msys64` location. After installation, an MSYS2 terminal window will automatically pop up. Let's update the system first:

```bash
pacman -Syu
```

This process updates the core system packages. After the update, the terminal might close automatically. Reopen an MSYS2 UCRT64 terminal (make sure it's UCRT64, not the default MSYS2 one). Then we install the GCC toolchain and CMake:

```bash
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja
```

Let's explain why we chose UCRT64 instead of MINGW64. UCRT (Universal C Runtime) is the new C runtime introduced by Microsoft since Windows 10, offering better API compatibility. It is the environment officially recommended by MSYS2. If your system is Windows 10 or later, just use UCRT64.

> ⚠️ **Pitfall Warning**: MSYS2 has multiple sub-environments (MSYS2, MINGW32, MINGW64, UCRT64, CLANG64), and the installed packages have different name prefixes. Packages in the UCRT64 environment start with `mingw-w64-ucrt-x86_64-`, so don't install them in the wrong environment. A simple way to check is to look at the terminal window title bar, or run `echo $MSYSTEM`, which should output `UCRT64`.

After installation, we need to add the MinGW bin directory to the system PATH so that gcc and cmake can be used in regular CMD and PowerShell. Add `C:\msys64\ucrt64\bin` to the system's PATH environment variable.

Then open a regular PowerShell or CMD and verify:

```powershell
g++ --version
```

If everything is normal, you'll see:

```text
g++ (Rev2, Built by MSYS2 project) 14.2.0
Copyright (C) 2024 Free Software Foundation, Inc.
本程序是自由软件；请参看源代码的版权声明。本软件没有任何担保；
包括没有适销性和某一专用目的下的适用性担保。
```

Now verify CMake:

```powershell
cmake --version
```

```text
cmake version 3.28.3

CMake suite maintained and supported by Kitware (kitware.com/cmake).
```

If both commands produce output, the toolchain is successfully installed.

## Step Two — Build Your First Project with CMake

With the toolchain installed, let's actually run a CMake project to ensure the entire build process works. Regardless of which route you chose, the CMake project is written the same way; the only difference is in the build commands.

First, we create a project directory with two files. First, `hello.cpp`:

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

This code uses preprocessor macros to detect the current compiler, so we can tell at a glance whether MSVC or GCC is working. Then the corresponding `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.16)
project(HelloWindows LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(hello hello.cpp)
```

This CMakeLists is very minimal—it specifies the minimum CMake version, declares the project name and language, sets the C++17 standard, and finally defines an executable target. There's nothing fancy here, but it's sufficient as a scaffold to verify the toolchain.

Now let's build. If you're using Visual Studio (MSVC), open the Developer Command Prompt for VS 2022, navigate to the project directory, and run:

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

If you're using MinGW-w64, run this in PowerShell or CMD:

```powershell
cmake -B build -G "MinGW Makefiles"
cmake --build build
```

> ⚠️ **Pitfall Warning**: When using the MinGW Makefiles generator, if there are other programs with `make.exe` in your PATH (like those bundled with Qt, or from some older MinGW installations), it might cause the build to fail. If you run into this, you can explicitly specify the make path during the build: `cmake -B build -G "MinGW Makefiles" -DCMAKE_MAKE_PROGRAM=C:/msys64/ucrt64/bin/mingw32-make.exe`.

Regardless of the route, a successful build will generate a `hello.exe` (or `Release/hello.exe`) in the `build` directory. Run it:

```powershell
# MSVC 路线
.\build\Release\hello.exe

# MinGW 路线
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

Seeing the correct compiler name in the output means the entire toolchain is fully working. Great, at this point we have a properly functioning compilation environment.

## Step Three — Install vcpkg to Manage Third-Party Libraries

In the C++ world, managing third-party libraries has always been a pain point. Unlike Python with pip or Rust with cargo, C++ has long relied on manually downloading source code, compiling, and linking. vcpkg is an open-source C++ package manager from Microsoft. While it's not part of the standard, it has become one of the de facto mainstream solutions. It helps us automatically download, compile, and install third-party libraries, and it integrates seamlessly with CMake.

Installing vcpkg itself is very simple; it's just a Git repository. Find a directory you like (we recommend putting it in `C:\vcpkg` or outside your project directory), and run this in PowerShell:

```powershell
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat
```

The bootstrap script will compile vcpkg itself and generate `vcpkg.exe`. If you don't have a VPN, this step might be quite slow because vcpkg needs to download some tools from GitHub.

Once it's installed, let's try installing a library. We'll choose `fmt` as an example. It's a modern C++ formatting library that we'll use later in the tutorial:

```powershell
.\vcpkg install fmt:x64-windows
```

The `:x64-windows` here is a triplet, representing the target platform. If you're using MinGW, you should switch to `:x64-mingw-dynamic` or `:x64-mingw-static`. vcpkg will automatically download the fmt source code, compile it with your local compiler, and place the header files and library files in the `installed/` directory.

The next crucial step is making sure CMake can find the libraries installed by vcpkg. vcpkg provides a CMake toolchain file, and we just need to specify it when configuring cmake. Assuming vcpkg is installed in `C:\vcpkg`, the build command becomes:

```powershell
cmake -B build -G "MinGW Makefiles" -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

Or for Visual Studio:

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

Using fmt in CMakeLists.txt is then very simple:

```cmake
cmake_minimum_required(VERSION 3.16)
project(HelloWindows LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(fmt CONFIG REQUIRED)
add_executable(hello hello.cpp)
target_link_libraries(hello PRIVATE fmt::fmt)
```

And the corresponding `hello.cpp`:

```cpp
#include <fmt/core.h>

int main()
{
    fmt::print("Hello from {} on Windows!\n",
#if defined(_MSC_VER)
        "MSVC"
#elif defined(__GNUC__)
        "GCC"
#else
        "unknown compiler"
#endif
    );
    return 0;
}
```

After building and running, you'll see fmt's colored output. This workflow of vcpkg combined with CMake is basically the standard approach for managing third-party C++ libraries on Windows right now, and we'll use it frequently later on.

## Step Four — Configure the Development Environment in VS Code

Regardless of which compiler route you took, VS Code is a great lightweight editor choice. We need to install the following extensions: **C/C++** (by Microsoft, providing syntax highlighting, IntelliSense, and debugging support) and **CMake Tools** (for CMake project management and building). If you prefer a Chinese interface, add the Chinese Language Pack as well.

The CMake Tools extension automatically detects compilers on your system. After installing the extensions and opening our project directory, a "Kit" selection item will appear in the VS Code bottom status bar. Clicking it lets you choose which compiler to use—if you installed both MSVC and MinGW, you can switch between them here. Once selected, CMake Tools will automatically configure the project, and the status bar will display the build configuration and compiler info.

For debugging configuration, CMake Tools provides great integration. Hover your mouse over the project name at the bottom of the status bar, and a debug button (a bug icon) will appear next to it. Click it directly to start debugging. If you want manual control over the debug configuration, you can write a configuration in `.vscode/launch.json`. For the MinGW route, a typical configuration looks like this:

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

For the MSVC route, just change `MIMode` to `"vsdbg"` and remove `miDebuggerPath`; the VS debugger will take over automatically.

At this point, the C++ development environment on Windows is fully set up. We have a compiler (MSVC or GCC), a build system (CMake), a package manager (vcpkg), and an editor (VS Code). The entire toolchain is ready to go.

## Summary

Let's review what we did. First, we chose a compiler route—Visual Studio (MSVC) suits developers who want an out-of-the-box experience and rely heavily on debuggers, while MSYS2 + MinGW-w64 is for scenarios that need to maintain a workflow consistent with Linux. Then we used CMake to build a test project to verify the toolchain's integrity, installed vcpkg to manage third-party library dependencies, and finally set up the development environment in VS Code.

In the next step, we'll officially start learning the C++ language. Before diving into writing code, we recommend trying out the environment you just set up—modify the hello project above, change the output content a few times, run the build and debug several times, and confirm that the entire pipeline from writing code, building, and running to breakpoint debugging works smoothly. When we start formal learning later, the tools will no longer be an obstacle.
