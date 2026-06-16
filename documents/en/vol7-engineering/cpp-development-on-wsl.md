---
chapter: 1
difficulty: intermediate
order: 6
platform: host
reading_time_minutes: 5
tags:
- cpp-modern
- host
- intermediate
title: Developing Generic C++ Host Applications on WSL Quickly
description: ''
translation:
  source: documents/vol7-engineering/cpp-development-on-wsl.md
  source_hash: 61db3c65723a774079854184c14f17581b960335a81089f1e83ccbbab85f633c
  translated_at: '2026-06-16T04:08:13.681582+00:00'
  engine: anthropic
  token_count: 1025
---
# Quickly Developing General C++ Host Programs on WSL

## Preface

I distinctly remember writing a blog post like this before, but I can no longer find it. As I am about to launch a new modern C++ analysis tutorial, I plan to use this post to archive the environment setup process.

> **Note:** This guide uses **WSL2 + Ubuntu** as an example. Commands are run in PowerShell / Windows Terminal (Administrator) or WSL bash. If you choose another distro (Debian, Fedora, etc.), please replace `apt` commands with the appropriate package manager.
>
> I will not teach how to install WSL here; there are plenty of tutorials available online.

------

## Prerequisites

- **Windows 10/11** (Latest updates recommended); WSL2 is recommended for better performance (and is the default for new installations). You can use `wsl --install` to install WSL and common distributions in one step. ([Microsoft Learn](https://learn.microsoft.com/en-us/windows/wsl/install?utm_source=chatgpt.com))
- Install **Visual Studio Code** on Windows (download from [https://code.visualstudio.com](https://code.visualstudio.com/)).
- A Microsoft account and administrator privileges are required to enable virtualization features (Hyper-V / Virtual Machine Platform) if necessary.

## First Steps in WSL: Update System and Install Basic Build Tools

Open Windows Terminal -> Select Ubuntu (or your installed distro) to enter the shell, then run:

```bash
sudo apt update && sudo apt upgrade -y
sudo apt install -y build-essential cmake git gdb
```

`build-essential` includes `gcc`/`g++`, `make`, and other packages, and is a standard build dependency on Debian/Ubuntu. Refer to community documentation for installation details.

------

## Install VS Code on Windows and Enable the Remote - WSL Extension

1. Download and install Visual Studio Code on Windows.
2. Open VS Code and navigate to the **Extensions** panel. Search for and install:
   - **Remote - WSL** (or the official extension named *WSL*) — This allows you to open and run VS Code directly within the WSL environment (the editor runs on Windows, but extensions/execution run on WSL). VS Code has official documentation and tutorials for WSL development. (This extension is a lifesaver).
3. Recommended installations (the corresponding server extensions will be automatically installed in the WSL context later):
   - **C/C++ (ms-vscode.cpptools)**: The official Microsoft C/C++ extension, providing IntelliSense, debugging, and code navigation. **Note:** This extension conflicts with `clangd`. If you prefer the Clang toolchain, do not install this; instead, install `clangd` and `clang-tidy`.
   - **CMake Tools** (or the C/C++ Extension Pack) — Used for CMake project management, configuration, building, and switching kits. If you don't use CMake, VS Code has a plethora of other plugins you can search for. I personally prefer CMake.
   - **CodeLLDB** (if you prefer the `lldb` debugger).
   - **clang-format** support, GitLens (to enhance Git experience), EditorConfig, etc.

------

## Opening a Project in WSL using VS Code (Truly "Developing under Linux")

1. In Windows, open VS Code, press `Ctrl+Shift+P` -> input `WSL: Connect to WSL` (or navigate to your project directory in the Ubuntu terminal and run `code .`, which will open the VS Code window on WSL).
2. VS Code will automatically install the necessary server components in WSL. A green indicator in the bottom-left corner will show **WSL: Ubuntu**, indicating the current window is connected to WSL.

> When VS Code opens in the WSL context, the Extensions panel on the left will prompt you to install extensions "Install in WSL:Ubuntu" (meaning the extension runs in the WSL environment rather than Windows). It is recommended to install C/C++ and CMake Tools in WSL (click "Install in WSL: Ubuntu").

------

## Creating a Minimal CMake + C++ Project and Building/Debugging in VS Code

Create project files in the WSL home directory:

```bash
mkdir -p hello_cmake/src
cd hello_cmake
```

Create a new file `src/main.cpp`:

```cpp
#include <iostream>

int main() {
    std::cout << "Hello from WSL!" << std::endl;
    return 0;
}
```

Create `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.10)
project(HelloWSL)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_executable(hello_wsl src/main.cpp)
```

Build (in the WSL terminal or VS Code's integrated terminal):

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

If you installed and are using the **CMake Tools** extension: Open the project root directory. The extension will provide **Build** and **Debug** buttons in the status bar at the bottom. You can click these to build or debug, and select different kits (gcc/clang) and build directories.

------

## Configuring Debugging in VS Code (Using gdb from ms-vscode.cpptools)

Create `.vscode/launch.json` in your project directory (using the `cpptools` generator):

```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "(gdb) Launch",
            "type": "cppdbg",
            "request": "launch",
            "program": "${workspaceFolder}/build/hello_wsl",
            "args": [],
            "stopAtEntry": false,
            "cwd": "${workspaceFolder}",
            "environment": [],
            "externalConsole": false,
            "MIMode": "gdb",
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

The `"program"` field requires the file path to your application. `${workspaceFolder}` refers to the directory you currently have open in VS Code. Since the build output is placed in the `build` folder, you will find your generated application there.

If you use `tasks.json` to define custom build tasks, ensure the `"preLaunchTask"` name matches; however, if you use CMake Tools, it automatically creates and manages build tasks and debug configurations, which is usually more convenient. In this case, simply switch to the VS Code **Run and Debug** view and click the **Start Debugging** button (or press F5).
