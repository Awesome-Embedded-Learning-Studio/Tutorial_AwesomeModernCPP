# 快速在WSL上开发一般的C++上位机程序

## 前言

笔者绝对记得我曾经写过这类博客，但是我找不到了，这边马上要准备起一个新的现代C++分析教程，所以这个博客笔者计划用来存档下作为环境配置的一部分。

> 说明：本文以 **WSL2 + Ubuntu（常见）** 为例；命令在 PowerShell / Windows Terminal（管理员）或 WSL 的 bash 中运行。若你选用其他 distro（Debian、Fedora 等），apt 的部分需改为相应包管理器。
>
> WSL咋安装不教了，这个网上大把教程。

------

## 准备与前置条件

- Windows 10/11（建议最新更新）；推荐开启 WSL2（性能更好、默认新安装即为 WSL2）。可用 `wsl --install` 一步安装 WSL 和常用发行版。([Microsoft Learn](https://learn.microsoft.com/en-us/windows/wsl/install?utm_source=chatgpt.com))
- 在 Windows 端安装 Visual Studio Code（从 [https://code.visualstudio.com](https://code.visualstudio.com/) 下载并安装）。
- 有一个微软账户/管理员权限以便在必要时启用虚拟化功能（Hyper-V / Virtual Machine Platform）。

## 首次进入 WSL：更新系统并安装基础编译工具

打开 Windows Terminal -> 选择 Ubuntu（或你安装的 distro），进入 shell，然后运行：

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

`build-essential` 包含 gcc/g++、make 等，是在 Debian/Ubuntu 上非常常用的构建必备包。安装命令和说明见常用社区文档。

------

## 在 Windows 上安装 VS Code，并启用 Remote - WSL 扩展

1. 在 Windows 下载并安装 Visual Studio Code。
2. 打开 VS Code，打开扩展（Extensions）面板，搜索并安装：
   - **Remote - WSL**（或名为 *WSL* 的官方扩展）——允许你直接在 WSL 环境中打开与运行 VS Code（编辑器会在 Windows，但扩展/运行在 WSL 上）。VS Code 官方有 WSL 开发文档与教程。（这个插件是真神）
3. 推荐再安装（后面在 WSL context 也会自动安装对应服务端扩展）：
   - **C/C++ (ms-vscode.cpptools)**：微软官方的 C/C++ 扩展，提供 IntelliSense、调试、代码导航等。注意，这个插件会跟clangd打架，如果你更喜欢Clang蔟的工具链，这个不要安装。安装Clangd, Clang-tidy才是你需要的
   - **CMake Tools**（或 C/C++ Extension Pack）— 用于 CMake 项目管理、配置、构建、切换 kit 等。如果你不是，VSCode有一大堆插件，这个需要您自己搜索了。笔者是喜欢用CMake
   - **CodeLLDB**（若你偏好 lldb 调试器）
   - **clang-format** 支持、GitLens（增强 Git 体验）、EditorConfig 等

------

## 在 WSL 中用 VS Code 打开项目（真正 “在 Linux 下开发”）

1. 在 Windows 中打开 VS Code，按 `F1` -> 输入 `Remote-WSL: New Window`（或在 Ubuntu 终端进入项目目录后执行 `code .`，这会在 WSL 上打开 VS Code 窗口）。
2. VS Code 会在 WSL 中自动安装必要的服务器组件，并在 “左下角绿色区域” 显示 `WSL: <distro>`，表示当前窗口已连接到 WSL。

> 当 VS Code 在 WSL context 下打开时，左侧的 Extensions 面板会提示你“安装到 WSL:Ubuntu”的扩展（即扩展会安装在 WSL 环境而不是 Windows）。推荐把 C/C++、CMake Tools 等在 WSL 上安装（点击“Install in WSL: Ubuntu”）。

------

## 创建一个最小 CMake + C++ 项目并在 VS Code 中构建/调试

在 WSL 的Home目录里创建项目文件：

```bash
mkdir -p ~/projects/hello_cmake && cd ~/projects/hello_cmake
```

新建文件 `CMakeLists.txt`：

```cmake
cmake_minimum_required(VERSION 3.10)
project(hello_cmake LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
add_executable(hello main.cpp)
```

新建 `main.cpp`：

```cpp
#include <iostream>

int main() {
    std::cout << "Hello from WSL C++ world!\n";
    int x = 42;
    std::cout << "x = " << x << std::endl;
    return 0;
}
```

构建（在 WSL 终端或 VS Code 的终端中）：

```bash
mkdir -p build && cd build
cmake .. -G "Ninja"        # 如果你安装了 ninja；否则用默认 make： cmake ..
cmake --build .
./hello
```

如果你安装并使用 **CMake Tools** 扩展：打开项目根目录，扩展会在底部状态栏提供 `Configure`、`Build` 按钮，点击即可；并可以选择不同的 kit（gcc/clang）与构建目录。

------

## 在 VS Code 中配置调试（使用 ms-vscode.cpptools 的 gdb）

在项目的 `.vscode` 目录下创建 `launch.json`（使用 cpptools 的 `cppdbg`）：

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

"program"需要填写你的应用程序的文件路径，`${workspaceFolder}`就是当前你开VSCode的目录，这里的构建放到了build下，你进这里就能看到你生成的应用程序了。

如果你用 `tasks.json` 自定义 build 任务，确保 `preLaunchTask` 名称一致；但若使用 CMake Tools，它会自动创建并管理构建任务/调试配置，通常更方便。这样的话，你切换到VSCode的调试栏上点击