# Quickly Developing General C++ Host Applications on WSL

## Preface

I definitely remember writing a blog post like this before, but I cannot find it. I am about to start a new modern C++ analysis tutorial, so I plan to use this post to archive the environment setup process.

> Note: This guide uses **WSL2 + Ubuntu (common)** as an example. Commands are run in PowerShell / Windows Terminal (Administrator) or the WSL bash shell. If you use another distro (Debian, Fedora, etc.), replace the `apt` commands with the appropriate package manager.
>
> We will not cover how to install WSL here, as there are plenty of tutorials available online.

------

## Prerequisites

- Windows 10/11 (latest updates recommended); enabling WSL2 is recommended (better performance, and it is the default for new installations). You can use `wsl --install` to install WSL and common distros in one step. ([Microsoft Learn](https://learn.microsoft.com/en-us/windows/wsl/install?utm_source=chatgpt.com))
- Install Visual Studio Code on the Windows side (download and install from [https://code.visualstudio.com](https://code.visualstudio.com/)).
- A Microsoft account / administrator privileges to enable virtualization features (Hyper-V / Virtual Machine Platform) if necessary.

## First Time in WSL: Update the System and Install Basic Build Tools

Open Windows Terminal -> select Ubuntu (or your installed distro) to enter the shell, then run:

```bash

# 更新系统包索引与系统
sudo apt update && sudo apt upgrade -y

# 安装 C/C++ 常用工具（gcc/g++、make 等）
sudo apt install -y build-essential gdb cmake ninja-build pkg-config

# 建议安装 clang/clang-format（可选）
sudo apt install -y clang clang-format

# （可选）安装额外工具：python 用于一些构建脚本、ccache 等
sudo apt install -y python3 python3-pip ccache

```

`build-essential` includes gcc/g++, make, and more. It is a very commonly used essential build package on Debian/Ubuntu. See popular community documentation for installation commands and details.

------

## Install VS Code on Windows and Enable the Remote - WSL Extension

1. Download and install Visual Studio Code on Windows.
2. Open VS Code, open the Extensions panel, search for and install:
   - **Remote - WSL** (or the official extension named *WSL*) — allows you to open and run VS Code directly in the WSL environment (the editor runs on Windows, but extensions/execution run on WSL). VS Code has official WSL development documentation and tutorials. (This extension is truly a lifesaver.)
3. We also recommend installing the following (the corresponding server-side extensions will be automatically installed in the WSL context later):
   - **C/C++ (ms-vscode.cpptools)**: Microsoft's official C/C++ extension, providing IntelliSense, debugging, code navigation, etc. Note that this extension conflicts with clangd. If you prefer the Clang toolchain, do not install this; installing Clangd and Clang-tidy is what you need instead.
   - **CMake Tools** (or the C/C++ Extension Pack) — used for CMake project management, configuration, building, switching kits, etc. If you do not use CMake, there are plenty of other VS Code extensions, which you will need to search for yourself. Personally, I prefer using CMake.
   - **CodeLLDB** (if you prefer the lldb debugger)
   - **clang-format** support, GitLens (enhances the Git experience), EditorConfig, etc.

------

## Opening a Project in WSL with VS Code (Truly "Developing Under Linux")

1. Open VS Code in Windows, press `F1` -> type `Remote-WSL: New Window` (or navigate to the project directory in the Ubuntu terminal and run `code .`, which will open a VS Code window on WSL).
2. VS Code will automatically install the necessary server components in WSL, and the "green area in the bottom left corner" will display `WSL: <distro>`, indicating that the current window is connected to WSL.

> When VS Code is opened in the WSL context, the Extensions panel on the left will prompt you to install extensions "in WSL:Ubuntu" (meaning the extensions will be installed in the WSL environment rather than Windows). We recommend installing C/C++, CMake Tools, etc., on WSL (click "Install in WSL: Ubuntu").

------

## Creating a Minimal CMake + C++ Project and Building/Debugging it in VS Code

Create the project files in the WSL home directory:

```bash
mkdir -p ~/projects/hello_cmake && cd ~/projects/hello_cmake

```

Create a new file named `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.10)
project(hello_cmake LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
add_executable(hello main.cpp)

```

Create a new file named `main.cpp`:

```cpp
#include <iostream>

int main() {
    std::cout << "Hello from WSL C++ world!\n";
    int x = 42;
    std::cout << "x = " << x << std::endl;
    return 0;
}

```

Build (in the WSL terminal or the VS Code integrated terminal):

```bash
mkdir -p build && cd build
cmake .. -G "Ninja"        # 如果你安装了 ninja；否则用默认 make： cmake ..
cmake --build .
./hello

```

If you installed and are using the **CMake Tools** extension: open the project root directory, and the extension will provide `Configure` and `Build` buttons in the bottom status bar. Simply click them; you can also select different kits (gcc/clang) and build directories.

------

## Configuring Debugging in VS Code (Using gdb from ms-vscode.cpptools)

Create a `launch.json` file in the project's `.vscode` directory (using the `cpptools` `cppdbg`):

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "Debug Hello (gdb)",
      "type": "cppdbg",
      "request": "launch",
      "program": "${workspaceFolder}/build/hello",
      "args": [],
      "stopAtEntry": false,
      "cwd": "${workspaceFolder}",
      "environment": [],
      "externalConsole": false,
      "MIMode": "gdb",
      "miDebuggerPath": "/usr/bin/gdb",
      "setupCommands": [
        { "description": "Enable pretty-printing", "text": "-enable-pretty-printing", "ignoreFailures": true }
      ],
      "preLaunchTask": "CMake: build"
    }
  ]
}

```

The "program" field requires the file path of your application. `${workspaceFolder}` is the directory where you opened VS Code. Since the build output is placed in the `build` directory, you can find your generated application there.

If you use a `tasks.json` to define a custom build task, ensure the `preLaunchTask` name matches. However, if you use CMake Tools, it will automatically create and manage build tasks and debug configurations, which is usually more convenient. In that case, switch to the VS Code debug panel and click
