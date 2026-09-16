---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: 在 Windows 上搭建 C++ 开发环境：安装 Visual Studio 或 MinGW、配置 CMake 和 vcpkg，从零开始到编译运行
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
title: Windows 环境搭建
---
# Windows 环境搭建

Windows上的C++开发属于是有点地狱的，真的，对我而言。

各种编译器版本、环境变量、路径空格，能把人逼疯，兄弟你但凡中间有一点中文，你就乐吧。CMD拉给你一长串的报错，笑都笑不出来！

但现在情况好多了，Windows 上的 C++ 工具链已经相当成熟，无论你是想用微软亲儿子 MSVC，还是更习惯 Linux 下那套 GCC 的工作流，都能找到顺手的方案。我们这篇就来把 Windows 上的 C++ 开发环境从头到尾搭一遍，确保后面写代码的时候不会在工具链上卡壳。

Windows 上主流的 C++ 编译器有两条路线：一条是微软的 Visual Studio（MSVC 编译器），这是 Windows 原生开发的主流选择，IDE 集成度极高，调试体验一流；另一条是 MinGW-w64（通过 MSYS2 安装），本质上是把 GCC 工具链搬到 Windows 上，如果你之前在 Linux 下写过 C++，用这套会觉得很熟悉。两条路线都能完美配合 CMake 和 vcpkg，选哪条纯粹看个人偏好。

**但是笔者要说一下：后续我们默认在Linux下开发！包括编译的指令，验证等。原因无他，方便。Windows的朋友，您自行变通！**

## 第一步（方案 A）——安装 Visual Studio 2022 Community

Visual Studio Community 是微软提供的免费版本，对于个人开发者和小团队来说功能完全够用。我们先去 [Visual Studio 下载页面](https://visualstudio.microsoft.com/downloads/) 拿到 Community 版的在线安装器，运行后会弹出一个工作负载选择界面。

这一步的关键在于选对工作负载——我们需要的是 **"使用 C++ 的桌面开发"**（Desktop development with C++）。勾选它就行，右侧的默认组件不用动，MSVC v143 编译器和 Windows SDK 都会自动包含进来。整个安装大概需要 6-8 GB 的磁盘空间，网速不好的话可能要等一会儿。

安装完成之后，我们来验证一下编译器是否可用。Visual Studio 不像 GCC 那样直接在普通终端里就能用，它需要一个特殊的环境——Developer Command Prompt。在开始菜单里搜索 "Developer Command Prompt" 或者 "Developer PowerShell for VS 2022"，打开后输入：

```powershell
cl
```

如果一切正常，你会看到类似这样的输出：

```text
用于 x64 的 Microsoft (R) C/C++ 优化编译器版本 19.42.34435.0
版权所有(C) Microsoft Corporation。保留所有权利。

用法: cl [ 选项... ] 文件名... [ /link 链接选项... ]
```

看到这个用法提示就说明 MSVC 编译器已经就位了。注意这里显示的是 x64，如果你打开的是 x86 版本的 Developer Command Prompt，会显示 x86，两者都能用，本教程后续统一使用 x64 版本。

> 如果你在普通的 PowerShell 或 CMD 里直接敲 `cl`，大概率会报 "cl 不是内部或外部命令"。这是因为 MSVC 的环境变量只在 Developer Command Prompt 里才会被设置。**不要试图手动添加环境变量，不要试图手动添加环境变量，不要试图手动添加环境变量，直接用 Developer Command Prompt 就行。**

Visual Studio 2022 自带了对 CMake 的原生支持。打开 VS，选择 "打开本地文件夹" 指向一个包含 `CMakeLists.txt` 的目录，VS 就会自动识别并配置项目，不需要额外的安装步骤。不过如果你想在命令行里用 `cmake` 命令，还是需要确认一下 CMake 是否在 PATH 里——在 Developer Command Prompt 中运行 `cmake --version`，如果能看到版本号就没问题。

关于调试等，请查看其他的博客，笔者这里想了想，不准备教授了。这个IDE笔者没好感。

## 第一步（方案 B）——通过 MSYS2 安装 MinGW-w64

如果你更习惯 GCC 那套东西，或者你的项目需要跨平台编译、跟 Linux 保持一致的工作流，那 MSYS2 + MinGW-w64 是更好的选择。MSYS2 本质上在 Windows 上提供了一个类似 Linux 的软件包管理环境，通过 `pacman`（没错，就是 Arch Linux 那个 pacman）来安装和管理工具链。

首先去 [MSYS2 官网](https://www.msys2.org/) 下载安装器，默认安装到 `C:\msys64`。安装完成后会自动弹出一个 MSYS2 终端窗口，我们先更新一下系统：

```bash
pacman -Syu
```

这个过程会更新核心系统包，更新完后终端可能会自动关闭，重新打开一个 MSYS2 UCRT64 终端（注意是 UCRT64，不是 MSYS2 默认的那个）。然后我们安装 GCC 工具链和 CMake：

```bash
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja
```

这里解释一下为什么选 UCRT64 而不是 MINGW64。UCRT（Universal C Runtime）是微软在 Windows 10 以后推出的新版 C 运行时，API 兼容性更好，是 MSYS2 官方推荐的环境。如果你的系统是 Windows 10 以上，直接用 UCRT64 就对了。

> MSYS2 有多个子环境（MSYS2、MINGW32、MINGW64、UCRT64、CLANG64），安装的包名前缀不同。UCRT64 环境下的包名是 `mingw-w64-ucrt-x86_64-` 开头的，别装错了环境。一个简单的判断方法是看终端窗口标题栏，或者运行 `echo $MSYSTEM`，应该输出 `UCRT64`。

安装完成后，我们需要把 MinGW 的 bin 目录加到系统 PATH 里，这样在普通的 CMD 和 PowerShell 里也能使用 gcc 和 cmake。把 `C:\msys64\ucrt64\bin` 添加到系统的环境变量 PATH 中。

然后打开一个普通的 PowerShell 或 CMD，验证一下：

```powershell
g++ --version
```

正常的话会看到：

```text
g++ (Rev2, Built by MSYS2 project) 14.2.0
Copyright (C) 2024 Free Software Foundation, Inc.
本程序是自由软件；请参看源代码的版权声明。本软件没有任何担保；
包括没有适销性和某一专用目的下的适用性担保。
```

再验证一下 CMake：

```powershell
cmake --version
```

```text
cmake version 3.28.3

CMake suite maintained and supported by Kitware (kitware.com/cmake).
```

两个命令都有输出，说明工具链安装成功。

## 第二步——用 CMake 构建第一个项目

工具链装好了，我们来实际跑一个 CMake 项目，确保整套构建流程是通的。不管你选了哪条路线，CMake 项目的写法是一样的，区别只在构建命令上。

我们先创建一个项目目录，里面放两个文件。首先是 `hello.cpp`：

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

这段代码用预处理器宏来检测当前编译器，这样可以一眼看出是 MSVC 还是 GCC 在工作。然后是对应的 `CMakeLists.txt`：

```cmake
cmake_minimum_required(VERSION 3.16)
project(HelloWindows LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(hello hello.cpp)
```

这个 CMakeLists 非常简洁——指定最低 CMake 版本、声明项目名和语言、设定 C++17 标准、最后定义一个可执行目标。没有任何花哨的东西，但作为验证工具链的脚手架已经足够了。

现在我们来进行构建。如果你用的是 Visual Studio（MSVC），打开 Developer Command Prompt for VS 2022，进入项目目录后执行：

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

如果你用的是 MinGW-w64，在 PowerShell 或 CMD 中执行：

```powershell
cmake -B build -G "MinGW Makefiles"
cmake --build build
```

使用 MinGW Makefiles 生成器时，如果 PATH 里有其他带 `make.exe` 的程序（比如 Qt 自带的、或者某些旧版 MinGW 的），可能会导致构建失败。如果碰到这个问题，可以在构建时显式指定 make 路径：`cmake -B build -G "MinGW Makefiles" -DCMAKE_MAKE_PROGRAM=C:/msys64/ucrt64/bin/mingw32-make.exe`。

无论哪条路线，构建成功后都会在 `build` 目录下生成一个 `hello.exe`（或者 `Release/hello.exe`）。运行它：

```powershell
# MSVC 路线
.\build\Release\hello.exe

# MinGW 路线
.\build\hello.exe
```

输出应该类似这样：

```text
Hello from Windows C++ toolchain!
Compiler: MSVC 1942
```

或者：

```text
Hello from Windows C++ toolchain!
Compiler: GCC 14.2
```

看到编译器名称正确输出，整个工具链就完全通了。很好，到这里我们已经有了一个能正常工作的编译环境。

## 第三步——在 VS Code 里配置开发环境

不管你用了哪条编译器路线，VS Code 都是一个很不错的轻量级编辑器选择。我们需要安装以下几个扩展：**C/C++**（Microsoft 出品，提供语法高亮、调试支持）和 **CMake Tools**（CMake 项目管理和构建）。如果你习惯用中文界面，再加一个 Chinese Language Pack 就行。

> 代码补全、跳转这些"IntelliSense"功能，这套教程推荐另装 **clangd** 扩展来管（比 C/C++ 扩展准），C/C++ 扩展留着管调试就行。详细做法见 [新手起步卷 · 装 clangd](/getting-started/05-vscode-clangd)。

CMake Tools 扩展会自动检测系统中的编译器。安装好扩展后打开我们的项目目录，VS Code 底部状态栏会出现一个 "Kit" 选择项，点击它就能选择要用的编译器——如果你同时装了 MSVC 和 MinGW，这里可以切换。选好之后 CMake Tools 会自动配置项目，状态栏上会显示构建配置和编译器信息。

调试配置方面，CMake Tools 做了很好的集成。把鼠标移到状态栏底部的项目名称上，旁边会出现一个调试按钮（虫子图标），直接点击就能启动调试。如果你想手动控制调试配置，可以在 `.vscode/launch.json` 里写一份配置。对于 MinGW 路线，一个典型的配置如下：

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

对于 MSVC 路线，把 `MIMode` 改成 `"vsdbg"` 并去掉 `miDebuggerPath` 就行，VS 的调试器会自动接管。

到这里，Windows 上的 C++ 开发环境就搭建完成了。我们有编译器（MSVC 或 GCC）、有构建系统（CMake）、有包管理器（vcpkg）、有编辑器（VS Code），整套工具链已经可以跑起来了。
