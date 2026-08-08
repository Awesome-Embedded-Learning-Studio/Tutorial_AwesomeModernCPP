---
title: "让 vscode 看懂您的代码——装 clangd，红线消失"
description: "篇 4 跑通了，但代码里到处画红线、点函数跳不过去。这篇三步装上 clangd，把 vscode 变聪明"
chapter: 14
order: 5
platform: host
difficulty: beginner
cpp_standard: [17, 20]
tags:
  - host
  - 入门
  - 基础
  - beginner
  - clangd
reading_time_minutes: 12
---

# 让 vscode 看懂您的代码——装 clangd，红线消失

## 开场

篇 4 咱们把三个文件的工程跑通了，终端打印 `Hello, world!` 那一刻大概率挺爽。但您接下来在 vscode 里多写几行，多半会撞上几个烦心事：

代码里的 `#include <iostream>` 时不时画着红波浪线，明明编译能过；按住 `Ctrl` 点 `greet` 这个函数名，想跳到它的定义看一眼，光标闪一下没反应；敲 `std::` 也不弹出补全列表，全靠自己一个字母一个字母手打。

这不是您写错了，编译器（g++）都说没问题。是 vscode 还没「看懂」您的项目。它不知道 `greet` 这个函数在哪、不知道 `std::` 后面能跟哪些东西，所以帮不上忙。这一篇咱们三步治好它，让编辑器跟着聪明起来。

## 为什么会这样

先把一件事说清楚：vscode 这个软件本身，其实不懂 C++。

vscode 是个通用编辑器，能写 Python、写网页、写 JSON，谁都能往里塞东西。它出厂不带任何一门语言的「理解能力」，得靠扩展（extension，您可以理解成插件）来补。篇 2 装环境的时候您装过一个叫 C/C++ 的扩展，那是微软官方出的，装上之后 vscode 就能懂一点 C++ 了，能高亮、能补全、能调试。

问题在于，这个 C/C++ 扩展「懂」得有限。它自己有一套分析 C++ 代码的逻辑，准头一般，碰到稍微复杂点的项目就经常判断错，把能编译过的代码画上红线，或者跳转到错误的地方。您可能已经体会过这种「明明没错却被骂」的憋屈。

C++ 社区现在的普遍做法是：换一个更强的工具来管「让编辑器看懂代码」这件事，那个工具叫 clangd。

clangd 是 LLVM 项目（一个开源的编译器工具链，跟 GCC 是同类东西）做的，专门干一件事：让编辑器看懂 C++ 代码。它用的分析引擎就是 Clang 编译器那套，准头比 C/C++ 扩展好一截，跳转、补全、报错都更靠谱。咱们这一篇就把它换上。

## 三步治好

治这个毛病分三步，咱们一步一步来。

### 步骤 1：让 CMake 生成一份「翻译说明书」

clangd 要靠一份叫 `compile_commands.json` 的文件才能看懂您的项目。这名字长，您先不用记。它本质上是一份「翻译说明书」，记录项目里每个 `.cpp` 文件用什么编译器、用什么 C++ 标准、引用了哪些头文件。clangd 拿到这份说明书，才知道该怎么理解您的每一行代码。才能给您丝滑的代码提示。

这份文件不用您手写，让 CMake 顺手吐出来就行。打开篇 4 那个 `greeter` 工程的 `CMakeLists.txt`，在 `project` 那行下面加一行：

```cmake
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
```

加完之后，完整的 `CMakeLists.txt` 长这样：

```cmake
cmake_minimum_required(VERSION 3.20)
project(greeter LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(greeter main.cpp greet.cpp)
```

::: tip 一行白话翻译
`CMAKE_EXPORT_COMPILE_COMMANDS ON` 的意思是「配置的时候，顺手在 build 目录里生成一份 compile_commands.json」。这个开关默认是关的，所以得手动开。
:::

保存 `CMakeLists.txt`，然后点 vscode 底部状态栏的「Configure」（重新配置才会重新生成）。配置完之后，工程里的 `build` 文件夹里会多出一个 `compile_commands.json` 文件。这就是 clangd 要的「说明书」。

::: warning 装了 CMake Tools 扩展才能点状态栏
如果您状态栏没有 Configure 按钮，说明篇 2 漏装了 CMake Tools 扩展。回去补上，然后重开 vscode。命令面板（`Ctrl+Shift+P`）搜 `CMake: Configure` 也能触发同样的动作。
:::

### 步骤 2：装 clangd 扩展

说明书有了，现在请真正的「读者」上场。

在 vscode 里点左侧活动栏的扩展图标（四个方块那个，快捷键 `Ctrl+Shift+X`），搜索框里输 `clangd`。您会看到一个发布者是 LLVM 的扩展，名字就叫 clangd。点 Install 装上。

::: warning 装扩展前，先确认本机有 clangd 这个程序
扩展只是个「遥控器」，真正干活的是您电脑上那个叫 `clangd` 的程序（有时候叫 `clangd.exe`）。光装扩展、没装程序，等于有遥控器没电视，开不起来。

判断本机有没有这个程序，最快的方式是开个终端敲 `clangd --version`：

- 能打印一串版本号（比如 `clangd version 18.x.x`），说明有，直接进步骤 3。
- 提示「不是内部或外部命令」「command not found」，说明没装，照下面的折叠盒装上。

Windows 用户注意：篇 2 咱们装的是 MSYS2 + g++ 那套工具链，里面没有 clangd。clangd 跟着 LLVM 这个大包走，得单独装。
:::

::: details 点开看：各平台怎么装 clangd 程序
Windows 上有两条路。

第一条路接着篇 2 的 MSYS2 装，最省事。打开「MSYS2 UCRT64」终端（篇 2 装的那个），敲：

```bash
pacman -S mingw-w64-ucrt-x86_64-clang-tools-extra
```

装完 clangd 就在 `C:\msys64\ucrt64\bin` 下，跟您篇 2 装的 g++ 在同一个目录，PATH 已经配好了，直接能用。

第二条路是装独立的 LLVM 包，用 winget（Win10 之后系统自带）。开 PowerShell 或 cmd，敲：

```bash
winget install LLVM.LLVM
```

装完之后 LLVM 的工具会进 `C:\Program Files\LLVM\bin`。这个路径默认不在系统的 PATH 里，您要么把它加进 PATH（让终端在任何地方都能找到 clangd），要么等会儿装好 vscode 的 clangd 扩展后，在扩展设置里手动指一下 clangd.exe 的路径。扩展通常能自己找到，找不到再手动指。

两条路二选一就行。用 scoop 或 chocolatey 的读者，命令分别是 `scoop install llvm` 和 `choco install llvm`。

Linux（Debian/Ubuntu 系）直接用 apt：

```bash
sudo apt install clangd
```

Fedora 系是 `sudo dnf install clang-tools-extra`，Arch 系是 `sudo pacman -S clang`。

macOS 用 Homebrew：

```bash
brew install llvm
```

::: tip macOS 有个坑
macOS 自带的 `clang`（来自 Xcode Command Line Tools）不带 clangd。光有系统 clang 不够，必须 `brew install llvm` 装完整 LLVM，然后还要把 `/opt/homebrew/opt/llvm/bin`（Apple Silicon）或 `/usr/local/opt/llvm/bin`（Intel）加进 PATH，否则终端还是找不到 clangd。
:::
:::

### 步骤 3：关掉 C/C++ 扩展的代码理解

这是最容易被忽略、但最关键的一步。

现在 vscode 里有两个扩展都想帮您分析 C++ 代码：篇 2 装的 C/C++ 扩展、刚装的 clangd。两个一起干活会打架，补全列表可能弹两份、跳转可能跳到不一样的地方、红线画得到处都是。咱们让它们分工：clangd 管「看懂代码」（补全、跳转、报错），C/C++ 扩展留着管调试（后面篇 6 会用到，调试那块 clangd 不管）。

> 笔者补充一下，现在的clangd插件会自己检查一下，发现有微软的intelliSense会问你你要不要Disabled掉他，选择是！

要做的就是关掉 C/C++ 扩展的代码理解功能。打开设置页：菜单 File → Preferences → Settings，或者直接 `Ctrl+,`。搜索框里输 `C_Cpp: Intellisense Engine`（ IntelliSense 是「智能提示」的英文叫法），把它的值从默认的 `Default` 改成 `disabled`。

如果您嫌点设置页麻烦，也可以直接改配置文件。在工程根目录建一个 `.vscode` 文件夹，里面放一个 `settings.json`，内容是：

```json
{
    "C_Cpp.intelliSenseEngine": "disabled"
}
```

::: tip 两种写法等价
设置页改的是 vscode 的全局配置（所有项目都生效）；写 `settings.json` 改的是这个工程的配置（只对当前工程生效）。新手两种都行，写 `settings.json` 的好处是跟着工程走，换台电脑打开同一个工程，设置还在。
:::

改完之后，您应该能在 vscode 右下角状态栏看到一个 `clangd` 字样（之前可能是 `C/C++` 或 `C/C++ IntelliSense`），这就说明现在管代码理解的是 clangd 了。

## 见证奇迹

三步做完，重新打开 `main.cpp`（或者随便点一下编辑区让它刷新）。您大概率会看到这几件事同时发生：

`#include <iostream>` 那行的红波浪线消失了。

按住 `Ctrl` 点 `greet` 这个函数名，光标嗖地跳到了 `greet.cpp` 里函数定义那一行。

在 `main` 函数里敲 `std::`，弹出一个补全列表，列出了 `cout`、`endl`、`vector` 这些标准库的东西。

之前 vscode 看不懂，现在看懂了。区别就这一份 `compile_commands.json` 加一个 clangd。

## 刚才到底发生了什么

```mermaid
flowchart LR
    A["CMakeLists.txt"] -->|configure| B["CMake"]
    B --> C["build/compile_commands.json"]
    C -->|clangd 读| D["看懂代码<br/>补全/跳转/报错"]
```


咱们退一步，把这事的来龙去脉理一遍。

clangd 这个程序，本质上就是个「替编辑器读代码」的助手。它得知道两件事才能干活：您的代码用的是什么 C++ 标准（C++17？C++20？）、每个 `.cpp` 引用了哪些头文件。不知道这两样，它两眼一抹黑，连 `std::string` 是什么都认不出来，自然只能把代码画满红线。

这两样信息，正好编译器在编译的时候全都用过一遍：CMake 配置时已经定好了用 C++17、已经知道 `main.cpp` include 了 `greet.h`。`CMAKE_EXPORT_COMPILE_COMMANDS ON` 那一行，就是让 CMake 把这些编译信息原样抄一份，写成 clangd 能读的 `compile_commands.json` 文件。

clangd 启动后做的第一件事，就是从您打开的 `.cpp` 文件往上找 `compile_commands.json`，找到就读进来。有了这份说明书，它就精确知道每个文件该怎么理解，补全、跳转、报错全都准。少了这份文件，或者文件过时（您改了 `CMakeLists.txt` 但没重新 Configure），clangd 就会糊涂，红线又会回来。

所以以后您要是遇到「明明能编译、clangd 却报红线」，第一反应不是怀疑代码，是重新点一下 Configure，让 CMake 把说明书刷新一遍。

## 折叠盒：compile_commands.json 长啥样

您不用读懂它的每个字段，扫一眼有个概念就行。打开 `build/compile_commands.json`，里面是个 JSON 数组，每个 `.cpp` 文件占一项，当然，这个是样例的输出哈，别碰这个文件，你也不应该编辑他！

```json
[
  {
    "directory": "D:/code/greeter/build",
    "command": "C:\\msys64\\mingw64\\bin\\c++.exe ... -std=gnu++17 ... D:/code/greeter/main.cpp",
    "file": "D:/code/greeter/main.cpp"
  },
  {
    "directory": "D:/code/greeter/build",
    "command": "... D:/code/greeter/greet.cpp",
    "file": "D:/code/greeter/greet.cpp"
  }
]
```

三个字段的意思：

`directory` 是编译这个文件时所在的目录，通常是您的 `build` 文件夹。`command` 是完整的编译命令，里面有编译器路径、`-std=gnu++17`（用的 C++ 标准）、所有头文件搜索路径，clangd 靠这个还原编译器的视角。`file` 就是这条记录对应的源文件。

clangd 读进来，就相当于「站在编译器的位置」重新看了一遍您的代码，所以它能判断的事情跟编译器一致：能编译过的就不会画红线。

## 折叠盒：命令行重新配置

::: details 点开看：命令行怎么做
要是您习惯敲命令，配置和之前一样，在工程根目录开终端：

```bash
cmake -B build
```

CMake 会重新读 `CMakeLists.txt`（这次带着那行 `EXPORT_COMPILE_COMMANDS`），刷新 `build` 目录里的内容，包括 `compile_commands.json`。

不想改 `CMakeLists.txt` 的话，也可以在配置命令里临时加一个参数达到同样效果：

```bash
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

效果跟在 `CMakeLists.txt` 里写 `set(CMAKE_EXPORT_COMPILE_COMMANDS ON)` 一样，区别只是后者跟着工程走（换台电脑也生效），前者只在这次配置生效。咱们教程推荐写进 `CMakeLists.txt`，一劳永逸。
:::

## clangd 还是 C/C++ 扩展

到这儿您可能有个疑问：那 C/C++ 扩展是装了干嘛的，是不是可以卸了？

咱们这套教程的分工是这样的：代码理解（高亮、补全、跳转、报错）归 clangd 管，因为它准；调试（断点、单步、看变量）归 C/C++ 扩展管，因为调试这块它做得成熟，篇 6 会专门讲。两个扩展分工，各管一摊，不打架（所以步骤 3 只关掉 C/C++ 扩展的 IntelliSense，没让您卸载它）。

::: tip 跟旧文章对齐
这套教程的 vol1 老文章里，当年推荐过用 C/C++ 扩展管代码理解。clangd 这几年成熟了、准头超过 C/C++ 扩展之后，社区普遍换成 clangd 了。以这篇为准，老文章里那段过时了。
:::

到这一篇为止，您的 vscode 已经有两样本事了：会编译（篇 3、篇 4 装的 CMake Tools，管怎么把 `.cpp` 变成 `.exe`），会看懂代码（这篇装的 clangd，管补全、跳转、报错）。写 C++ 顺手的两个地基都铺好了。

接下来您可以往好几个方向走：想知道代码写错了怎么一步步调试的，去看下一篇讲调试的；想多写几行、看看 C++ 到底能干啥的，可以开始翻正文卷了。地基打牢了，往上盖楼就是。


::: details 点开看：clangd 想再多学一点

- [VS Code 插件 clangd 的用法](https://www.cnblogs.com/newtonltr/p/18867195) —— clangd 安装配置详解（LSP 工作原理 + compile_commands.json 怎么用）
:::
