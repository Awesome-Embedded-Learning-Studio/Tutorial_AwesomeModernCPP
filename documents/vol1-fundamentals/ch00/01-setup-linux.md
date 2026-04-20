---
title: "Linux 环境搭建"
description: "在 Linux 上搭建 C++ 开发环境：安装编译器、CMake 和 VS Code，从零配置到编译运行第一个程序"
chapter: 0
order: 1
difficulty: beginner
reading_time_minutes: 15
platform: host
tags:
  - cpp-modern
  - host
  - beginner
  - 入门
  - 基础
cpp_standard: [11, 14, 17, 20]
---

# Linux 环境搭建

在开始写 C++ 之前，我们得先把工位收拾好。这一篇要做的事情很简单——在 Linux 上从零搭建一套能编译、能构建、能舒服写代码的 C++ 开发环境。整个过程大概十五分钟，但如果你是第一次折腾 Linux 环境配置，留半小时比较稳妥。

为什么选 Linux？说白了，C++ 的整个工具链生态就是围绕 Unix/Linux 生长出来的。GCC 的第一行代码诞生于 1987 年，Clang 和 CMake 也都是 Unix-first 的设计。在 Linux 上编译调试 C++ 代码，遇到问题的时候你能找到的资料、 Stack Overflow 上的回答、开源项目的 CI 配置——几乎全部默认你跑的是 Linux。而且后续教程中我们会涉及嵌入式交叉编译、WSL 开发等工作，Linux 环境是绕不过去的基础。

> **学习目标**
>
> - 完成本章后，你将能够：
> - [ ] 在 Linux 系统上安装 GCC 或 Clang 编译器并验证版本
> - [ ] 安装 CMake 构建工具并理解其基本定位
> - [ ] 配置 VS Code 为顺手的 C++ 开发环境
> - [ ] 从零创建一个 CMake 管理的 C++ 项目并成功编译运行

## 环境说明

本篇的所有命令在以下环境下验证通过：

- **操作系统**：Ubuntu 22.04 / 24.04（Debian 系通用）、Fedora 39+、Arch Linux
- **Shell**：Bash / Zsh 均可
- **WSL**：Windows 11 自带的 WSL2（Ubuntu 22.04）同样适用，后面会单独提一下 WSL 的注意事项

如果你用的是其他发行版，包管理器命令会有些不同，但思路完全一样——装编译器、装 CMake、装编辑器，三件事。

## 第一步——把编译器装上

编译器是我们把 C++ 源代码翻译成机器能执行的二进制文件的工具。Linux 世界里最主流的 C++ 编译器有两个：GCC（GNU Compiler Collection）和 Clang。Ubuntu/Debian 默认的 `build-essential` 包会把 GCC 以及相关的构建工具一股脑装好，这是我们最省事的选择。

根据你的发行版，执行对应的命令：

=== "Ubuntu / Debian"

    ```bash
    sudo apt update && sudo apt install build-essential -y
    ```

    `build-essential` 是一个元包（meta package），它本身不包含任何软件，但会拉下来 `g++`、`gcc`、`make`、`libc6-dev` 等一系列编译必需的工具。装完这一个包，基本的 C 和 C++ 编译环境就有了。

=== "Fedora"

    ```bash
    sudo dnf install gcc-c++ make -y
    ```

=== "Arch Linux"

    ```bash
    sudo pacman -S gcc make
    ```

    Arch 默认的 `gcc` 包已经包含 C++ 支持，不需要额外装 `gcc-c++`。

装好之后，我们先来验证一下。打开终端，执行：

```bash
g++ --version
```

你看到的输出大概长这样（具体版本号会因发行版和更新状态而异）：

```text
g++ (Ubuntu 13.2.0-23ubuntu4) 13.2.0
Copyright (C) 2023 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
```

只要你能看到版本号输出，就说明 GCC 安装成功。这里我们建议 GCC 的版本不低于 11——GCC 11 全面支持 C++20 的大部分特性，后续教程中我们会大量使用 C++17 和 C++20 的功能。如果你的发行版自带的 GCC 版本比较老（比如 Ubuntu 20.04 默认是 GCC 9），可以考虑通过 PPA 或者编译源码来升级，不过这个我们暂时不展开。

如果你也想试试 Clang（后续教程中部分特性会用 Clang 做对比），可以这样装：

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

Clang 的报错信息比 GCC 更友好一些，在做模板元编程的调试时经常会切换到 Clang 来看错误提示。不过日常开发用 GCC 完全够用，两个编译器保持装好就行，不冲突。

> ⚠️ **踩坑预警**：如果你在 WSL 中执行 `g++ --version` 提示 `command not found`，先别慌。大概率是你忘记执行 `sudo apt update` 了，或者 WSL 的发行版没有正确初始化。在 WSL 终端里跑一遍 `sudo apt update && sudo apt upgrade -y`，然后重新安装 `build-essential` 即可。另外，WSL 默认的 Ubuntu 镜像有时候会比较旧，建议在 Microsoft Store 里确认一下你的 WSL 发行版版本。

## 第二步——装好 CMake

有了编译器，我们还需要一个构建工具来管理项目的编译流程。你可能会问——直接 `g++ hello.cpp -o hello` 不就行了？对于单个文件当然没问题，但真实项目的源文件往往有几十甚至上百个，彼此之间有依赖关系，手动敲编译命令根本不现实。CMake 就是干这件事的：它读取一个叫 `CMakeLists.txt` 的配置文件，然后自动生成对应的构建脚本（比如 Makefile 或 Ninja 文件），把编译、链接这些脏活累活替你打理好。

安装 CMake 同样一行命令搞定：

```bash
# Ubuntu / Debian
sudo apt install cmake -y

# Fedora
sudo dnf install cmake -y

# Arch
sudo pacman -S cmake
```

验证安装：

```bash
cmake --version
```

```text
cmake version 3.28.3

CMake suite maintained and supported by Kitware (kitware.com/cmake).
```

CMake 的版本我们建议不低于 3.16——从 3.16 开始 CMake 引入了一些对 C++20 模块和预设（presets）的支持，后续教程中我们写的 `CMakeLists.txt` 会用到这些特性。如果你的发行版仓库里的 CMake 版本偏低，可以从 Kitware 官方源或者 pip 安装更新的版本：

```bash
# 通过 pip 安装最新版 CMake（推荐）
pip install cmake --upgrade
```

> ⚠️ **踩坑预警**：通过 `apt install cmake` 在 Ubuntu 22.04 上装到的版本是 3.22，这个版本可以完成本篇的所有操作，但后续用到 CMake Presets 时至少需要 3.21+。如果你不想折腾版本问题，直接用 pip 装最新版是最省心的选择。装完后记得确认 `which cmake` 指向的是你期望的版本——有时候系统会同时存在 apt 装的和 pip 装的两个 cmake，`PATH` 里谁在前面谁先生效。

## 第三步——配好 VS Code

编辑器这东西见仁见智，vim 和 emacs 当然没问题，但如果你想要一个开箱即用、插件生态成熟的 C++ 开发环境，VS Code 是目前最主流的选择。而且它在 WSL 下的远程开发体验做得相当好——代码在 Linux 上编译运行，编辑界面留在 Windows 上，两全其美。

安装 VS Code 的方式很多，最简单的办法是去[官网](https://code.visualstudio.com/)下载 `.deb` 包（Ubuntu/Debian）或 `.rpm` 包（Fedora），然后双击安装。Arch 用户可以直接 `sudo pacman -S code`。

装好 VS Code 之后，我们需要装几个关键的扩展。打开 VS Code，按 `Ctrl+Shift+X` 进入扩展面板，搜索并安装以下三个：

- **C/C++**（Microsoft 出品）——提供语法高亮、智能提示、调试支持，VS Code 写 C++ 的基石
- **CMake Tools**（Microsoft 出品）——在 VS Code 里直接配置、构建、调试 CMake 项目，不用切终端
- **CMake**（twxs 出品）——为 `CMakeLists.txt` 提供语法高亮和补全

装完之后，我们来做一个简单的配置。按 `Ctrl+Shift+P` 打开命令面板，输入 `settings json`，选择 `Preferences: Open User Settings (JSON)`，把下面的内容加进去（如果文件里已有配置，合并进去就好，不要覆盖已有内容）：

```json
{
    "C_Cpp.default.compilerPath": "/usr/bin/g++",
    "C_Cpp.default.cStandard": "c17",
    "C_Cpp.default.cppStandard": "c++20",
    "C_Cpp.default.intelliSenseMode": "linux-gcc-x64",
    "cmake.configureOnOpen": true,
    "cmake.buildDirectory": "${workspaceFolder}/build"
}
```

这里的配置含义很直白：告诉 C/C++ 扩展我们的编译器路径和目标 C++ 标准，让 CMake Tools 在打开项目时自动配置，构建产物放到 `build/` 目录下。`cmake.configureOnOpen` 设成 `true` 之后，每次用 VS Code 打开一个包含 `CMakeLists.txt` 的目录，它都会自动跑一遍 `cmake` 配置，省得手动操作。

如果你用 Clang，把 `compilerPath` 改成 `/usr/bin/clang++`，`intelliSenseMode` 改成 `linux-clang-x64` 就行。

> ⚠️ **踩坑预警**：如果你在 WSL 里用 VS Code，需要额外安装 **Remote - WSL** 扩展。装好之后，在 WSL 终端里输入 `code .`，VS Code 会自动以远程模式连接到 WSL，这时候代码智能提示、构建、调试都在 Linux 侧完成。千万别在 Windows 侧装了 VS Code 然后通过网络路径（`\\wsl$\`）去编辑 Linux 文件——虽然技术上能打开文件，但编译器路径、调试配置全部对不上，属于给自己找不痛快。

## 第四步——跑通第一个 CMake 项目

到这里工具都齐了，我们来实际操练一把——从零创建一个 CMake 管理的 C++ 项目，编译并运行。这一步如果顺利跑通，说明整个工具链配置没有问题，后续章节就可以安心写代码了。

先找个地方建一个项目目录：

```bash
mkdir -p ~/projects/hello_cmake && cd ~/projects/hello_cmake
```

然后创建我们的第一个 C++ 源文件 `hello.cpp`：

```cpp
#include <iostream>

int main()
{
    std::cout << "Hello, Modern C++!" << std::endl;
    return 0;
}
```

这是一个最简单的 C++ 程序——`#include <iostream>` 引入标准输入输出库，`std::cout` 是 C++ 的标准输出流，`<<` 运算符把字符串送到输出流里。`std::endl` 除了换行之外还会刷新输出缓冲区，确保内容立刻显示。

接下来创建 `CMakeLists.txt`——这个文件告诉 CMake 我们的项目怎么构建：

```cmake
cmake_minimum_required(VERSION 3.16)
project(hello_cmake LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(hello hello.cpp)
```

我们来逐行拆解。`cmake_minimum_required(VERSION 3.16)` 声明这个项目需要的最低 CMake 版本，如果你的 CMake 版本低于 3.16，配置阶段会直接报错而不是产生莫名其妙的构建失败。`project(hello_cmake LANGUAGES CXX)` 定义项目名称和支持的语言——`CXX` 是 CMake 里对 C++ 的代号。`set(CMAKE_CXX_STANDARD 20)` 把 C++ 标准设为 C++20，`CMAKE_CXX_STANDARD_REQUIRED ON` 确保如果编译器不支持 C++20 就直接报错，而不是悄悄降级。最后 `add_executable(hello hello.cpp)` 声明我们要构建一个叫 `hello` 的可执行文件，源文件是 `hello.cpp`。

现在开始构建。CMake 推荐的做法是在单独的目录里构建，避免把生成的临时文件污染源代码目录：

```bash
mkdir build && cd build
cmake ..
make
```

你会看到类似这样的输出：

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

构建成功。现在运行我们的程序：

```bash
./hello
```

```text
Hello, Modern C++!
```

如果你看到了这行输出，恭喜——编译器、CMake、整个工具链已经全部就位，可以开始正式的 C++ 学习之旅了。如果你用 VS Code 打开这个项目目录（`code ~/projects/hello_cmake`），CMake Tools 扩展会自动识别 `CMakeLists.txt` 并配置项目，底部状态栏会出现构建和运行的按钮，以后直接在 VS Code 里点击就能编译运行，不用每次都敲命令。

## 遇到问题怎么办

工具链配置这步，不同机器上的情况差异比较大，踩坑是正常的。这里列出几个最常见的报错和对应的解决思路。

**`g++: command not found` 或 `cmake: command not found`**

这说明对应的工具没有安装，或者安装了但不在 `PATH` 环境变量里。先用 `which g++` 和 `which cmake` 检查一下它们的位置——如果返回空，重新安装对应的包。如果返回了路径但命令还是找不到，那就是 `PATH` 配置有问题，检查一下 `~/.bashrc` 或 `~/.zshrc` 里有没有把 `/usr/bin` 从 `PATH` 中移除。

**CMake 报 `CMake Error: Could not find CMAKE_CXX_COMPILER`**

这个通常发生在 WSL 或者 Docker 容器里——系统装了 CMake 但没装编译器。回到第一步，确认 `g++ --version` 能正常输出，然后重跑 `cmake ..`。

**编译时报 `undefined reference to symbol` 之类的链接错误**

单个文件的 `hello.cpp` 不会碰到这个问题。但后续项目变复杂之后，如果遇到链接错误，基本就是 `CMakeLists.txt` 里忘记链接某个库了——`target_link_libraries` 命令没有加上对应的库。这个我们在后面的章节会详细讲。

**WSL 下文件系统性能慢**

WSL 访问 Windows 文件系统（`/mnt/c/` 下的路径）速度会比访问 Linux 原生文件系统慢很多。如果你的项目放在 `/mnt/c/Users/.../projects/` 下面，编译速度会明显卡顿。解决办法是把项目放到 Linux 侧的 home 目录（`~/projects/`），通过 VS Code 的 Remote - WSL 来编辑就好。

## 小结

到这里，我们已经完成了 Linux 上 C++ 开发环境的完整搭建。回顾一下我们做的事情：安装了 GCC 编译器（通过 `build-essential` 元包），装好了 CMake 构建工具，配置了 VS Code 的 C++ 开发扩展，最后从零创建了一个 CMake 项目并成功编译运行。

这套环境就是我们后续所有教程的基础设施。从下一章开始，我们就要正式进入 C++ 的世界了——如果你用的是 Windows 并且不想装 WSL，下一篇我们会单独讲 Windows 环境的搭建方案；如果你已经在这里跑通了 `Hello, Modern C++!`，可以直接跳到 C 语言速成章节，开始写真正的代码。

---

> **难度自评**：如果本篇的操作你都能顺利完成并且理解了每一步的原因，说明你的 Linux 基础操作能力已经到位。如果某些命令的含义还不太清楚，不用担心——后续章节中我们会反复使用这些工具，熟能生巧。
