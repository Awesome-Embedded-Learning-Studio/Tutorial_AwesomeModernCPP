---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: 'Setting up a C++ development environment on Windows: installing Visual
  Studio or MinGW, configuring CMake and vcpkg, from scratch to compiling and running.'
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
  source_hash: 367ebea17808e443124c101158e7f6f8152a461ad4a9d4943ae100a616396cad
  translated_at: '2026-06-16T03:39:40.224113+00:00'
  engine: anthropic
  token_count: 2316
---
# Windows Environment Setup

> ⚠️ **Note from Author**: The author has not had the energy to rigorously verify this section. Experts are welcome to provide corrections and feedback!

Honestly, setting up a C++ development environment on Windows used to be quite a hassle—dealing with various compiler versions, environment variables, and spaces in paths could drive anyone crazy. However, things have improved significantly. The C++ toolchain on Windows is now quite mature. Whether you prefer Microsoft's own MSVC or the GCC workflow you are used to on Linux, you can find a solution that suits you. In this article, we will build a Windows C++ development environment from scratch to ensure we won't get stuck on toolchain issues later when writing code.

There are two mainstream paths for C++ compilers on Windows: one is Microsoft's Visual Studio (MSVC compiler), which is the mainstream choice for native Windows development, featuring highly integrated IDE and a top-tier debugging experience; the other is MinGW-w64 (installed via MSYS2), which essentially ports the GCC toolchain to Windows. If you have written C++ on Linux before, this will feel very familiar. Both paths work perfectly with CMake and vcpkg, so the choice is purely a matter of personal preference.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Install and configure Visual Studio 2022 (MSVC) or MinGW-w64 (MSYS2) compilers
> - [ ] Use CMake to build a C++ project and run it successfully
> - [ ] Install vcpkg and use it to manage third-party library dependencies
> - [ ] Configure a C++ development and debugging environment in VS Code

## Environment Overview

This article is based on Windows 10/11. All commands and screenshots are verified against the following versions:

- **Operating System**: Windows 11 23H2 (Windows 10 21H2+ is also applicable)
- **Option A**: Visual Studio 2022 Community 17.14 (MSVC v143)
- **Option B**: MSYS2 + MinGW-w64 UCRT64 (GCC 14.x)
- **Build Tool**: CMake 3.28+
- **Editor**: VS Code 1.90+ (with C/C++ / CMake Tools extensions)

You only need to choose one path; there is no need to install both. If you don't have a strong preference, I suggest going directly with the Visual Studio route to save time and effort.

## Step 1 (Option A) — Installing Visual Studio 2022 Community

Visual Studio Community is a free version provided by Microsoft that is fully functional for individual developers and small teams. First, go to the [Visual Studio download page](https://visualstudio.microsoft.com/downloads/) to get the online installer for the Community edition. After running it, a workload selection interface will pop up.

The key here is to select the correct workload—we need **"Desktop development with C++"**. Check this option, and you can leave the default components on the right alone; the MSVC v143 compiler and Windows SDK will be included automatically. The installation requires about 6-8 GB of disk space, so it might take a while if your internet connection is slow.

After the installation is complete, let's verify that the compiler is working. Unlike GCC, Visual Studio isn't directly available in a standard terminal; it requires a special environment—the Developer Command Prompt. Search for "Developer Command Prompt" or "Developer PowerShell for VS 2022" in the Start menu, open it, and type:

```powershell
cl
```

If everything is normal, you should see output similar to this:

```text
Microsoft (R) C/C++ Optimizing Compiler Version 19.41.34120 for x86
Copyright (C) Microsoft Corporation. All rights reserved.

usage: cl [ option... ] filename... [ /link linkoption... ]
```

Seeing this usage message indicates that the MSVC compiler is in place. Note that it shows x86 here; if you opened the x64 version of the Developer Command Prompt, it will display x64. Both work, but this tutorial will consistently use the x64 version.

> ⚠️ **Warning**: If you type `cl` directly in a standard PowerShell or CMD, it will likely report "cl is not recognized as an internal or external command". This is because MSVC's environment variables are only set within the Developer Command Prompt. Do not try to add environment variables manually; just use the Developer Command Prompt.

Visual Studio 2022 has built-in native support for CMake. Open VS, select "Open Local Folder", and point it to a directory containing a `CMakeLists.txt`. VS will automatically recognize and configure the project without requiring extra installation steps. However, if you want to use the `cmake` command in the command line, you still need to confirm whether CMake is in your PATH—run `cmake --version` in the Developer Command Prompt, and if you see the version number, you are good to go.

## Step 1 (Option B) — Installing MinGW-w64 via MSYS2

If you prefer the GCC toolset, or if your project requires cross-platform compilation and a workflow consistent with Linux, then MSYS2 + MinGW-w64 is the better choice. MSYS2 essentially provides a Linux-like software package management environment on Windows, using `pacman` (yes, the pacman from Arch Linux) to install and manage the toolchain.

First, go to the [MSYS2 website](https://www.msys2.org/) to download the installer and install it to the default path `C:\msys64`. After installation, an MSYS2 terminal window will pop up automatically. Let's update the system first:

```bash
pacman -Syu
```

This process updates core system packages. After the update, the terminal might close automatically. Re-open an MSYS2 UCRT64 terminal (note that it is UCRT64, not the default MSYS2 one). Then we install the GCC toolchain and CMake:

```bash
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja
```

Here is why we choose UCRT64 instead of MINGW64. UCRT (Universal C Runtime) is the new C runtime introduced by Microsoft since Windows 10, offering better API compatibility. It is the environment recommended by MSYS2. If your system is Windows 10 or newer, just use UCRT64.

> ⚠️ **Warning**: MSYS2 has multiple sub-environments (MSYS2, MINGW32, MINGW64, UCRT64, CLANG64), and the installed package names have different prefixes. Packages in the UCRT64 environment start with `mingw-w64-ucrt-x86_64-`, so don't install them in the wrong environment. A simple way to check is to look at the terminal window title bar, or run `echo $MSYSTEM`, which should output `UCRT64`.

After installation, we need to add the MinGW `bin` directory to the system PATH so we can use `gcc` and `cmake` in standard CMD and PowerShell. Add `C:\msys64\ucrt64\bin` to the system environment variable PATH.

Then open a standard PowerShell or CMD to verify:

```text
gcc --version
```

Normally you should see:

```text
gcc (UCRT64) 14.2.0
Copyright (C) 2024 Free Software Foundation, Inc.
...
```

Verify CMake as well:

```text
cmake --version
```

```text
cmake version 3.30.2
...
```

If both commands produce output, the toolchain installation is successful.

## Step 2 — Building Your First Project with CMake

Now that the toolchain is ready, let's actually run a CMake project to ensure the entire build process works. Regardless of which path you chose, the CMake project code is the same; the only difference lies in the build commands.

First, create a project directory and place two files in it. The first is `main.cpp`:

```cpp
#include <iostream>

int main() {
#if defined(_MSC_VER)
    std::cout << "Hello from MSVC!" << std::endl;
#elif defined(__GNUC__)
    std::cout << "Hello from GCC!" << std::endl;
#else
    std::cout << "Hello from Unknown Compiler!" << std::endl;
#endif
    return 0;
}
```

This code uses preprocessor macros to detect the current compiler, so we can see at a glance whether MSVC or GCC is working. Next is the corresponding `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(HelloWorld)

set(CMAKE_CXX_STANDARD 17)

add_executable(hello main.cpp)
```

This `CMakeLists` is very simple—specifying the minimum CMake version, declaring the project name and language, setting the C++17 standard, and finally defining an executable target. There are no fancy tricks here, but it is sufficient as a scaffold to verify the toolchain.

Now let's build. If you are using Visual Studio (MSVC), open the "Developer Command Prompt for VS 2022", enter the project directory, and execute:

```text
cmake -B build -G "Ninja"
cmake --build build
```

If you are using MinGW-w64, execute the following in PowerShell or CMD:

```text
cmake -B build -G "MinGW Makefiles"
cmake --build build
```

> ⚠️ **Warning**: When using the MinGW Makefiles generator, if there are other programs with `mingw32-make.exe` in your PATH (such as those included with Qt or older versions of MinGW), it might cause the build to fail. If you encounter this problem, you can explicitly specify the make path during the build: `cmake --build build -- -f C:/msys64/ucrt64/bin/mingw32-make`.

Regardless of the path, a successful build will generate an executable named `hello.exe` (or `hello.exe.exe`) in the `build` directory. Run it:

```text
.\build\hello.exe
```

The output should look like this:

```text
Hello from MSVC!
```

Or:

```text
Hello from GCC!
```

Seeing the correct compiler name output means the entire toolchain is fully working. Great, we now have a working compilation environment.

## Step 3 — Installing vcpkg to Manage Third-Party Libraries

In the world of C++, managing third-party libraries has always been a pain point—unlike Python with pip or Rust with cargo, C++ has long relied on manually downloading source code, compiling, and linking. vcpkg is a C++ package manager open-sourced by Microsoft. Although not part of the standard, it has become one of the de facto mainstream solutions. It helps us automatically download, compile, and install third-party libraries, and integrates seamlessly with CMake.

Installing vcpkg itself is very simple; it is just a Git repository. Find a directory you like (I suggest putting it in `C:\dev` or outside your project directory), then execute in PowerShell:

```text
git clone https://github.com/microsoft/vcpkg
cd vcpkg
.\bootstrap-vcpkg.bat
```

The bootstrap script will compile vcpkg itself and generate `vcpkg.exe`. If you don't a reliable internet connection, this step might be slow because vcpkg needs to download some tools from GitHub.

Once installed, let's try installing a library. We'll choose `fmt` as an example; it's a modern C++ formatting library that we will use in later tutorials:

```text
.\vcpkg install fmt:x64-windows
```

Here `x64-windows` is a triplet, indicating the target platform. If you are using MinGW, you should switch to `x64-mingw-dynamic` or `x64-mingw-static`. vcpkg will automatically download the source code for fmt, compile it with your local compiler, and place the header files and library files in the `installed` directory.

The next crucial step is to enable CMake to find the libraries installed by vcpkg. vcpkg provides a CMake toolchain file that we just need to specify during the cmake configuration. Assuming vcpkg is installed at `C:\dev\vcpkg`, the build command becomes:

```text
cmake -B build -DCMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

Or for Visual Studio:

```text
cmake -B build -G "Visual Studio 17 2022" -DCMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

Using fmt in `CMakeLists.txt` is very simple:

```cmake
find_package(fmt REQUIRED)
add_executable(hello main.cpp)
target_link_libraries(hello PRIVATE fmt::fmt)
```

Corresponding `main.cpp`:

```cpp
#include <fmt/core.h>
#include <iostream>

int main() {
    fmt::print("Hello from fmt!\n");
    return 0;
}
```

After building and running, you will see the colorful output from fmt. This workflow of vcpkg combined with CMake is basically the standard practice for C++ third-party library management on Windows, and we will use it frequently later.

## Step 4 — Configuring the Development Environment in VS Code

Regardless of which compiler path you chose, VS Code is an excellent lightweight editor choice. We need to install the following extensions: **C/C++** (by Microsoft, provides syntax highlighting, IntelliSense, debugging support) and **CMake Tools** (for CMake project management and building). If you prefer a Chinese interface, you can also add the Chinese Language Pack.

The CMake Tools extension will automatically detect compilers in the system. After installing the extensions and opening our project directory, a "Kit" selection item will appear in the bottom status bar of VS Code. Clicking it allows you to select the compiler to use—if you have both MSVC and MinGW installed, you can switch here. Once selected, CMake Tools will automatically configure the project, and the status bar will display build configuration and compiler information.

Regarding debugging configuration, CMake Tools provides excellent integration. Move your mouse over the project name at the bottom of the status bar, and a debug button (bug icon) will appear next to it; click it to start debugging directly. If you want to control the debug configuration manually, you can write a configuration in `launch.json`. For the MinGW path, a typical configuration looks like this:

```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Debug (gdb)",
            "type": "cppdbg",
            "request": "launch",
            "program": "${workspaceFolder}/build/hello.exe",
            "miDebuggerPath": "C:\\msys64\\ucrt64\\bin\\gdb.exe",
            "setupCommands": [
                {
                    "text": "-enable-pretty-printing",
                    "ignoreFailures": true
                }
            ]
        }
    ]
}
```

For the MSVC path, just change `"cppdbg"` to `"cppvsdbg"` and remove `"miDebuggerPath"`, and VS's debugger will take over automatically.

At this point, the Windows C++ development environment setup is complete. We have a compiler (MSVC or GCC), a build system (CMake), a package manager (vcpkg), and an editor (VS Code). The entire toolchain is ready to run.

## Summary

Let's review what we have done. First, we chose a compiler path—Visual Studio (MSVC) is suitable for developers who want an out-of-the-box experience and rely heavily on debuggers, while MSYS2 + MinGW-w64 is suitable for scenarios requiring a workflow consistent with Linux. Then, we used CMake to build a test project to verify the integrity of the toolchain, installed vcpkg to manage third-party library dependencies, and finally set up the development environment in VS Code.

Next, we will officially start learning the C++ language. Before writing code, I suggest you try out the environment you just built—modify the hello project above, change the output content, run the build and debug a few times, and confirm that the entire pipeline from coding, building, and running to breakpoint debugging works smoothly. When we start learning formally, the tools will no longer be an obstacle.
