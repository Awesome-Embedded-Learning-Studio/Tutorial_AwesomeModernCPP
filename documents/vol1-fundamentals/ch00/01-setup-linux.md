---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: 在 Linux 上搭建 C++ 开发环境：安装编译器、CMake 和 VS Code，从零配置到编译运行第一个程序
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
title: Linux 环境搭建
---
# Linux 环境搭建

动手！CharlieChen114514如是说。

好吧，不抽象了，动笔写 C++ 之前，咱们得先把环境配齐。这一篇要做的事情很简单：在 Linux 上从零搭一套能编译、能构建、写得舒服的 C++ 开发环境。整个过程大概**十五分钟（不等，说不好）**，但如果您是第一次折腾 Linux 环境配置，留半小时，嗯，说不定也有可能是一天，比较稳妥。前提是您早就熟悉 Linux 了，不熟悉 Linux 的朋友下一篇——Windows 部署走起。不建议死磕，**但是Windows上的支持，需要您每一次都要稍微灵活的变通一些~。**

为什么选 Linux？说白了，C++ 的整个工具链生态就是围绕 Unix/Linux 生长出来的。GCC 的第一行代码诞生于 1987 年，Clang 和 CMake 也都是 Unix-first 的设计。在 Linux 上编译调试 C++ 代码，遇到问题的时候您能找到的资料、Stack Overflow 上的回答、开源项目的 CI 配置，几乎全部默认您跑的是 Linux。而且后续教程中咱们会涉及嵌入式交叉编译、WSL 开发等工作，Linux 环境是绕不过去的基础。（交代一句：Linux 排在 Windows 前面，也是因为笔者更喜欢 Linux 开发，我的电脑 Windows 纯打游戏的，谁会急头白脸地跑去 Windows 写代码啊（大雾））

## 编译器装上！上任C++之旅

> "不是哥们，啥是编译器啊？？？"

编译器（Compiler）是把 C++ **源代码翻译成机器能执行的二进制文件的工具**。Linux 世界里最主流的 C++ 编译器有两个：**GCC（GNU Compiler Collection）套件**和 **Clang（LLVM那边的）**。Ubuntu/Debian 默认的 `build-essential` 包会把 GCC 以及相关的构建工具一股脑装好，这是咱们最省事的选择。

根据您的发行版，执行对应的命令：

::: code-group

```bash [Ubuntu / Debian]
sudo apt update && sudo apt install build-essential -y
```

```bash [Arch Linux]
sudo pacman -S gcc make
```

:::

`build-essential` 是一个元包（meta package），它本身不包含任何软件，但会拉下来 `g++`、`gcc`、`make`、`libc6-dev` 等一系列编译必需的工具。咱们装完这一个包，基本的 C 和 C++ 编译环境就有了。

Arch 这边更省事，默认的 `gcc` 包已经包含 C++ 支持，咱们不用额外装 `gcc-c++`。

装好之后验证一下。打开终端，执行：

```bash

g++ --version
```

您看到的输出大概长这样（具体版本号会因发行版和更新状态而异）：

```text
g++ (Ubuntu 13.2.0-23ubuntu4) 13.2.0
Copyright (C) 2023 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
```

只要能看到版本号输出，GCC 就装好了。这里笔者建议版本不低于 11——GCC 11 全面支持 C++20 的大部分特性，后续教程中咱们会大量使用 C++17 和 C++20 的功能。如果您发行版自带的 GCC 比较老（比如 Ubuntu 20.04 默认是 GCC 9），可以考虑通过 PPA 或者编译源码来升级，这个暂时不展开。

您要是想顺便试试 Clang（后续教程中部分特性会用它做对比），可以这样装：

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

Clang 的报错信息比 GCC 更友好一些，调模板代码卡住的时候，笔者经常切到 Clang 看错误提示。不过日常开发用 GCC 完全够用，两个编译器保持装好就行，不冲突。

WSL 里装完还是 `command not found`，别慌，十有八九是漏跑了 `sudo apt update`，或者 WSL 的发行版压根没初始化好——在 WSL 终端里跑一遍 `sudo apt update && sudo apt upgrade -y`，再重装一次 `build-essential` 就行。另外 WSL 默认拉的 Ubuntu 镜像有时比较旧，拿不准就去 Microsoft Store 确认一下您的发行版版本。

## 装好 CMake

有了编译器，还需要一个构建工具来管理项目的编译流程。咱们可能会问——直接 `g++ hello.cpp -o hello` 不就行了？单个文件当然没问题，但真实项目的源文件往往有几十上百个，彼此之间还有依赖关系，手动敲编译命令根本不现实。

> 啥，你没见过CMake项目？这样，你打开Github，翻到
>
> - CFBox: <https://github.com/Awesome-Embedded-Learning-Studio/CFBox>
> - CFDesktop: <https://github.com/Awesome-Embedded-Learning-Studio/CFDesktop>
>
> 随便转转，我打赌你肯定不会手敲编译器命令的
> （当然没有再推广我的项目，我确信）

CMake 就是干这件事的：它读取一个叫 `CMakeLists.txt` 的配置文件，然后自动生成对应的构建脚本（比如 Makefile 或 Ninja 文件），把编译、链接这些脏活累活替咱们打理好。

安装 CMake 同样一行命令搞定：

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

验证安装：

```bash
cmake --version
```

```text
cmake version 3.28.3

CMake suite maintained and supported by Kitware (kitware.com/cmake).
```

**CMake 的版本笔者建议不低于 3.16**。理由你可能看不懂，非要看的话，我的回答是：从 3.16 开始 CMake 引入了一些对 C++20 模块和预设（presets）的支持，后续教程中咱们写的 `CMakeLists.txt` 会用到这些特性。如果您发行版仓库里的 CMake 版本偏低，可以从 Kitware 官方源或者 pip 安装更新的版本。放弃理解了吧，记得`cmake --verison`反复确认一下。

## 配好 VS Code

编辑器这东西见仁见智，vim 和 emacs 当然没问题，但如果您想要一个**开箱即用、插件生态成熟**的 C++ 开发环境，VS Code 是目前最主流的选择。（开箱即用是IDE，但是笔者建议别用，直面搭建环境的痛苦，从最开始就知道发生了什么，比懵懵懂懂搞到后面问题都不会查好多了）。而且它在 WSL 下的远程开发体验做得相当好：代码在 Linux 上编译运行，编辑界面留在 Windows 上，两全其美。

> 是的，教程我就是VSCode写的！这个东西很好用，强烈安利！

![vscode text](assets/01-linux/vscode-interfaces.png)

安装 VS Code 的方式很多，咱们走最省事的：去[官网](https://code.visualstudio.com/)下载 `.deb` 包（Ubuntu/Debian）或 `.rpm` 包（Fedora），然后双击安装。Arch 用户可以直接 `sudo pacman -S code`。

装好 VS Code 之后还差几个关键扩展。您按 `Ctrl+Shift+X` 打开扩展面板，头一个搜 C/C++（Microsoft 出品），语法高亮、智能提示、调试支持全靠它，是 VS Code 写 C++ 的基石；接着装同门的 CMake Tools，配置、构建、调试 CMake 项目都在编辑器里点按钮完成，不用切终端；再补一个 twxs 出品的 CMake，给 `CMakeLists.txt` 提供语法高亮和补全。三个装齐，这套环境就成型了。

## 跑通第一个 CMake 项目

到这里工具都齐了，咱们来实际操练一把：从零创建一个 CMake 管理的 C++ 项目，编译并运行。这一步如果顺利跑通，说明整个工具链配置没有问题，后续章节就可以安心写代码了。

找个地方建一个项目目录，Linux命令行刷起~

```bash
# 递归的创建 ~/projects 和  ~/projects/hello_cmake 目录，然后切换到这个目录下。
mkdir -p ~/projects/hello_cmake && cd ~/projects/hello_cmake
```

然后创建咱们的第一个 C++ 源文件 `hello.cpp`：

```cpp
// 你用 vscode 新建文件，还是touch，还是echo "" > hello.cpp，无所谓~
#include <iostream>

int main()
{
    std::cout << "Hello, Modern C++!" << std::endl;
    return 0;
}
```

咱们先看这个最简单的 C++ 程序：`#include <iostream>` 引入标准输入输出库，`std::cout` 是 C++ 的标准输出流，`<<` 运算符把字符串送到输出流里。`std::endl` 除了换行之外还会刷新输出缓冲区，确保内容立刻显示。

接下来创建 `CMakeLists.txt`，这个文件告诉 CMake 咱们的项目怎么构建：

```cmake
cmake_minimum_required(VERSION 3.16)
project(hello_cmake LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(hello hello.cpp)
```

欸欸欸别跑，我慢慢说。

- `cmake_minimum_required(VERSION 3.16)` 声明这个项目需要的最低 CMake 版本，如果您的 CMake 版本低于 3.16，配置阶段会直接报错而不是产生莫名其妙的构建失败。
- `project(hello_cmake LANGUAGES CXX)` 定义项目名称和支持的语言，`CXX` 是 CMake 里对 C++ 的代号。
- `set(CMAKE_CXX_STANDARD 20)` 把 C++ 标准设为 C++20，`CMAKE_CXX_STANDARD_REQUIRED ON` 确保如果编译器不支持 C++20 就直接报错，而不是悄悄降级。
- `add_executable(hello hello.cpp)` 声明咱们要构建一个叫 `hello` 的可执行文件，源文件是 `hello.cpp`。

现在咱们开始构建。CMake 推荐的做法是在单独的目录里构建，避免把生成的临时文件污染源代码目录：

```bash
mkdir build && cd build
cmake ..
make
```

您会看到类似这样的输出：

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

构建成功。现在运行咱们的程序：

```bash
./hello
```

```text
Hello, Modern C++!
```

看到这行输出，恭喜，编译器、CMake、整个工具链全部就位，可以正式开始写 C++ 了。如果您用 VS Code 打开这个项目目录（`code ~/projects/hello_cmake`），CMake Tools 扩展会自动识别 `CMakeLists.txt` 并配置项目，底部状态栏会出现构建和运行的按钮，以后直接在 VS Code 里点击就能编译运行，不用每次都敲命令。

## 遇到问题怎么办

工具链配置这步，不同机器上的差异比较大，您要是踩了坑，很正常。这里给几个最常见的报错和对应的解决思路。

**`g++: command not found` 或 `cmake: command not found`**

这说明对应的工具没有安装，或者装了但不在 `PATH` 环境变量里。您先用 `which g++` 和 `which cmake` 检查一下它们的位置——如果返回空，重新安装对应的包。如果返回了路径但命令还是找不到，那就是 `PATH` 配置有问题，检查一下 `~/.bashrc` 或 `~/.zshrc` 里有没有把 `/usr/bin` 从 `PATH` 中移除。

**CMake 报 `CMake Error: Could not find CMAKE_CXX_COMPILER`**

这个通常发生在 WSL 或者 Docker 容器里——系统装了 CMake 但没装编译器。咱们回到装编译器那一节，确认 `g++ --version` 能正常输出，然后重跑 `cmake ..`。

**编译时报 `undefined reference to symbol` 之类的链接错误**

单个文件的 `hello.cpp` 不会碰到这个问题。但后续项目变复杂之后，如果遇到链接错误，基本就是 `CMakeLists.txt` 里忘记链接某个库了：`target_link_libraries` 命令没有加上对应的库。这个咱们在后面的章节会详细讲。

**WSL 下文件系统性能慢**

WSL 访问 Windows 文件系统（`/mnt/c/` 下的路径）速度会比访问 Linux 原生文件系统慢很多。如果您的项目放在 `/mnt/c/Users/.../projects/` 下面，编译速度会明显卡顿。解决办法是把项目放到 Linux 侧的 home 目录（`~/projects/`），通过 VS Code 的 Remote - WSL 来编辑就好。

**其他问题？**

剩下的，问社区、问 AI、问周边大佬都行；也可以直接跑到<https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP>这个仓库下发 Issue 问笔者。我有时候看 Issue 比翻邮件更快，为什么跟独立上一条的原因我说过了，我很菜，真不是大佬，但是小白问题我可以帮忙看看的。
