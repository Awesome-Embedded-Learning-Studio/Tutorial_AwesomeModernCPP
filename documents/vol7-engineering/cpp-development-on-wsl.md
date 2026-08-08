---
title: "在 WSL 上做 C++ 工程化——vscode + clangd 深入 + 完整调试"
description: "起步卷篇 5 装上 clangd 那篇的深入版:把 WSL2 工具链装满、把 .clangd 配到满血、把 launch.json/tasks.json 调试链配通,讲清 Remote-WSL 的客户端/服务端架构和 compile_commands.json 怎么被 clangd 找到"
chapter: 1
order: 6
platform: host
difficulty: intermediate
cpp_standard: [17, 20]
tags:
  - host
  - cpp-modern
  - intermediate
  - clangd
reading_time_minutes: 20
prerequisites:
  - "起步卷篇 5: 让 vscode 看懂您的代码——装 clangd"
related:
  - "CMake 是什么——构建系统生成器的两段式流水线"
  - "CMakePresets.json——从 cmake -D 老式到 --preset 可复现"
---

# 在 WSL 上做 C++ 工程化——vscode + clangd 深入 + 完整调试

在 Windows 上做正经 C++ 工程化，目前最顺手的组合是 **WSL2 + vscode + clangd**。这一篇把这套组合一次性配满：装好 Linux 工具链、把 clangd 用到满血、把 launch.json 和 tasks.json 调试链配通。如果您刚从 [起步卷篇 5](/getting-started/05-vscode-clangd) 过来，那边三步装上 clangd 把红线消掉了，本篇就接着往下挖——`.clangd` 配置文件每一项到底干什么、clang-tidy 怎么和 clangd 挂上、大项目后台索引慢该怎么治、调试断点打上之后 gdb 怎么显示 `std::vector`。

## 为什么是 WSL

Windows 自带的 C++ 工具链不是没有，MSVC、MinGW 都能跑。但您顺着本教程读到卷七，会发现所有命令行示例、所有 `CMakeLists.txt` 片段、所有终端输出都默认 Linux 环境。直接在 Windows 上跑，工具能跑通，但每一步都要过一道"翻译"：`g++` 变成 `g++.exe`、路径分隔符变、`arm-none-eabi-g++` 的 sysroot 路径要重设。WSL2 把这道翻译直接抹掉。

WSL2 是微软搞的、跑在 Windows 里的真 Linux 内核（不是模拟器）。它对咱们这种做 C++ 工程化的场景有三个直接好处。

Linux 工具链最完整。`gcc`、`gdb`、`make`、`cmake`、`ninja-build`、`clangd`、`clang-tidy`、`valgrind`、`binutils` 一条 `apt` 命令全装上，版本还跟得上。本教程卷六讲 AddressSanitizer、卷七讲交叉编译，这些工具在原生 Windows 上要么得绕路装 MSYS2、要么干脆没有。

跟生产环境一致。咱们写的 C++ 工程以后多半跑在 Linux 服务器上。开发环境就是 Linux，意味着「我本机能跑、上服务器就崩」这类环境差异问题从一开始就不存在。

WSL2 性能接近原生。WSL2 走的是真 Linux 内核 + 轻量虚拟机路线，跟 WSL1 那套系统调用翻译完全不同。文件系统 IO、进程调度都接近原生 Linux 性能，编译速度跟真机 Linux 没差太多。这一点是 WSL2 相比 WSL1 的关键升级，也是为什么现在做 C++ 开发都默认 WSL2。

::: warning 别在 `/mnt/c` 下做工程
WSL2 访问 Windows 文件系统（`/mnt/c/...`）要走 9P 协议，IO 慢一个数量级。把工程放在 WSL 自家文件系统（`~/projects/` 下），configure 和 build 都快得多。笔者第一次没注意这点，一个中型工程 configure 跑了 40 秒，挪进 `~/` 之后 4 秒。
:::

## 装 WSL2 和 C++ 工具链

WSL2 的安装在 PowerShell（管理员）里一行命令搞定：

```powershell
wsl --install
```

这条命令会启用需要的 Windows 功能（Virtual Machine Platform）、下载默认的 Ubuntu 发行版、装好。装完重启一次，启动 Ubuntu，第一次会让你设用户名和密码。如果您想用别的发行版（Debian、Fedora），`wsl --list --online` 看可选列表，`wsl --install -d <名字>` 装指定的。

进 Ubuntu 之后，先把系统包刷到最新，然后一把装齐 C++ 工程化的全套工具：

```bash
sudo apt update && sudo apt upgrade -y
sudo apt install -y build-essential cmake ninja-build gdb clangd clang-tidy clang-format
```

`build-essential` 是 Debian/Ubuntu 那套对 C/C++ 的元包，装上就带 `gcc`/`g++`/`make`。`cmake` 是构建系统生成器（卷七 ch00 01 专门讲过）、`ninja-build` 提供 `ninja`（比 `make` 快、是本教程的默认 Generator）、`gdb` 是调试器。后面三个属于 LLVM 工具链：`clangd` 是 clang 出的 LSP 服务器（让 vscode 看懂代码）、`clang-tidy` 是静态检查、`clang-format` 是格式化。

::: details 顺手装几个有用的

```bash
# valgrind 内存检查（卷六内存安全那卷会用）
sudo apt install -y valgrind

# ccache 加速重编（CI 上和大型项目特别值）
sudo apt install -y ccache

# 跟 cmake 一起用的几种 build 工具
sudo apt install -y ninja-build

# 看 build 产物里有什么符号、依赖哪些动态库
sudo apt install -y binutils
```

:::

装完验一下版本，确认都齐了。下面是笔者本机的输出：

```text
$ gcc --version | head -1
gcc (Ubuntu 13.2.0-23ubuntu4) 13.2.0

$ cmake --version | head -1
cmake version 3.28.3

$ ninja --version
1.11.1

$ gdb --version | head -1
GNU gdb (Ubuntu 14.1-0ubuntu3.1) 14.1

$ clangd --version
clangd version 18.1.3
Features: linux
Platform: x86_64-pc-linux-gnu
```

::: tip clangd 一定要和工具链一起装
新手经常忘了装 `clangd` 这个程序，只装了 vscode 的 clangd 扩展。扩展只是「遥控器」，真正干活的是 `clangd` 这个二进制。光装扩展、没装程序，等于有遥控器没电视。`clangd --version` 打出版本号才算装上。
:::

Ubuntu 24.04 的 apt 源里 clangd 是 18.x，已经够用（行内提示 InlayHints、include-cleaner、External 索引这些特性都齐）。如果您非要追新版，加 LLVM 官方 apt 源能装到 19/20，对本教程来说没必要。

## vscode Remote-WSL：编辑器在 Windows、活儿在 WSL

vscode 跑 C++ 这事，背后是客户端/服务端架构：vscode 界面跑在 Windows，真正干活的进程跑在 WSL 里，中间靠 Remote-WSL 这个扩展连起来。这个架构搞不清楚，后面所有问题都没法定位。

在 Windows 上做两件事：

- 装 vscode（从 [code.visualstudio.com](https://code.visualstudio.com) 下，正常下一步）
- 在 vscode 扩展市场搜 `WSL`（发布者 Microsoft），装上

装好之后，有两种方式打开 WSL 里的工程：

第一种，命令面板（`F1` 或 `Ctrl+Shift+P`）输 `Remote-WSL: New Window`，会拉起一个连着 WSL 的 vscode 新窗口。

第二种，在 WSL 终端里 cd 到工程目录，敲：

```bash
code .
```

`code` 这个命令是 Remote-WSL 扩展装上之后自动注入到 WSL 的 PATH 里的。它会让 Windows 那边的 vscode 打开，并把当前目录当成工作区。

::: details 为什么 `code .` 能用
Remote-WSL 在 WSL 里放了一个 `code` 的 shell 脚本（通常在 `/usr/bin/code`），它干的事是跟 Windows 那边 vscode 通信，让 vscode 启动并连过来。第一次跑会从 Windows 拉一个 vscode server 组件到 WSL（`~/.vscode-server/`），这个 server 才是真正跑扩展、跑终端、跑语言服务器的进程。后续打开秒开。
:::

连上之后，看 vscode 窗口左下角，应该有个绿色或蓝色的标记写着 `WSL: Ubuntu`。这表示当前窗口的所有文件操作、终端、扩展都跑在 WSL 里。

接下来这一点是新手的最大坑：**vscode 的扩展分两边装**。Windows 那边的扩展管 UI（主题、图标、快捷键），WSL 那边的扩展管 Linux 上的活儿（代码理解、调试、构建）。Remote-WSL 连上之后，扩展面板会分成「LOCAL - INSTALLED」（Windows 端）和「WSL: UBUNTU - INSTALLED」（WSL 端）两栏。您要装的 clangd、C/C++ 扩展、CMake Tools，都得装到 WSL 那栏（点扩展旁边的「Install in WSL: Ubuntu」）。

```text
扩展面板(连上 WSL 之后)
├── LOCAL - INSTALLED        ← Windows 端:主题、图标、Remote-WSL 自身
│   ├── Remote - WSL  ✓
│   ├── Material Icon Theme
│   └── ...
└── WSL: UBUNTU - INSTALLED  ← WSL 端:这里装 clangd / C/C++ / CMake Tools
    ├── clangd           ← 代码理解(补全/跳转/报错)
    ├── C/C++            ← 调试(留 cppdbg,关 IntelliSense)
    └── CMake Tools      ← CMake 配置/构建/选 kit(可选)
```

clangd 这个扩展必须装到 WSL 端。它要调 WSL 里的 `clangd` 二进制，要走 WSL 里的 `compile_commands.json`，全在 Linux 这边。装错到 Windows 端，它去找 Windows 上的 `clangd.exe`，铁定找不到。

## clangd 深入配置

[起步卷篇 5](/getting-started/05-vscode-clangd) 把 clangd 装上、把红线消掉了，但只讲了三步：开 `CMAKE_EXPORT_COMPILE_COMMANDS`、装扩展、关 C/C++ 扩展的 IntelliSense。本篇把后面的事补齐——clangd 怎么找 compile_commands、`.clangd` 配置文件每一项干什么、clang-tidy 怎么挂、include-cleaner 怎么开。

### compile_commands.json 怎么来

clangd 干活要靠一份叫 `compile_commands.json` 的文件。这是 Clang 社区定义的 Compilation Database 格式，里面是项目里每个 `.cpp` 一条记录，记下编译它用的完整命令：编译器路径、`-std=` 标准、所有 `-I` 头文件搜索路径。clangd 拿到这份文件，才能「站在编译器的位置」看代码，知道 `std::vector` 该去哪个头文件找、`-std=c++17` 下哪些特性可用。

CMake 配合这件事特别顺，一行搞定。在 `CMakeLists.txt` 里 `project()` 之后加：

```cmake
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
```

或者不想改 `CMakeLists.txt`，configure 时命令行加 `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` 也行。configure 完之后，`build/compile_commands.json` 就生成了。

::: warning 这个开关只对 Makefile / Ninja Generator 生效
`CMAKE_EXPORT_COMPILE_COMMANDS` 只在用 Makefile 或 Ninja 这两类 Generator 时才吐 `compile_commands.json`。Visual Studio Generator（`-G "Visual Studio 17 2022"`）和 Xcode Generator 不支持。WSL 里咱们默认用 Ninja，没这个问题。
:::

configure 完，`build/compile_commands.json` 长这样（笔者本机的真实输出）：

```json
[
  {
    "directory": "/home/user/wsl-clangd/build",
    "command": "/usr/bin/c++ -I/home/user/wsl-clangd -std=c++17 -o CMakeFiles/greeter.dir/main.cpp.o -c /home/user/wsl-clangd/main.cpp",
    "file": "/home/user/wsl-clangd/main.cpp",
    "output": "/home/user/wsl-clangd/build/CMakeFiles/greeter.dir/main.cpp.o"
  }
]
```

每个 `.cpp` 一项。`command` 字段是最关键的，clangd 解析它得到编译器、标准、头文件路径，然后照着这个视角去理解代码。所以您改了 `CMakeLists.txt`（比如新加一个 `target_include_directories`）之后，得重新 configure 让 `compile_commands.json` 刷新，不然 clangd 还在用旧的视角，新加的头文件路径它不知道，红线又回来。

### clangd 怎么找 compile_commands.json

这件事的官方行为是：clangd 拿到您正在编辑的源文件，沿着它所在目录一路向上找 `compile_commands.json`，找到第一个就用。也就是说，您的源文件在 `~/proj/src/foo.cpp`，clangd 会依次找：

```text
~/proj/src/compile_commands.json
~/proj/compile_commands.json
~/compile_commands.json
~/.../compile_commands.json
```

clangd 16 之后还多了一条规则：沿途每一级目录里，它也会瞄一眼那个目录下的 `build/` 子目录有没有 `compile_commands.json`。这是专门为 CMake 项目加的便利——CMake 默认把这份文件写在 `build/` 里，clangd 知道这一点，会主动去翻。

笔者在 clangd 22 上实测，把源文件放 `src/`、`compile_commands.json` 放 `build/`，工程根没有任何软链，clangd 一样能找到：

```text
I[11:23:59.322] Loading compilation database...
I[11:23:59.323] Loaded compilation database from /tmp/clangd-search-test/build/compile_commands.json
```

所以默认情况下不用您操心。但有两个场景还是要手动指一下。

第一个场景，您用了多个 build 目录（比如 `build-debug/` 和 `build-release/`），clangd 不知道挑哪个，可能在两个之间横跳。这种在 `.clangd` 里直接指定：

```yaml
CompileFlags:
  CompilationDatabase: build-debug
```

`CompilationDatabase` 字段可以是个目录路径（相对工程根），也可以是 `Ancestors`（默认行为，向上找 + 翻 `build/`）或 `None`（关掉，只用 fallback）。

第二个场景，老 clangd（15 及更早）没那套「翻 `build/` 子目录」的规则，真的只会沿父目录找根上的 `compile_commands.json`。这种情况下工程根得有个软链：

```bash
ln -sf build/compile_commands.json compile_commands.json
```

clangd 向上找时就能在工程根命中这个软链，跟着读到 `build/` 里那份真文件。新 clangd 不需要这步，但留着没坏处，对老 clangd 兼容。

### .clangd 配置文件逐项

`.clangd` 是 clangd 项目级配置，YAML 格式，放工程根目录。clangd 沿源文件目录向上找 `.clangd`，所有命中的片段按顺序合并，越靠近源文件的优先级越高。下面这份是笔者实战用的配置（同时存在仓库 `code/examples/vol7/wsl-clangd/.clangd`），逐段讲它每项干什么：

```yaml
CompileFlags:
  Add: [-Wall, -Wextra, -Wno-unused-parameter]
  Remove: [-fsanitize=thread]
  Compiler: clang++
  CompilationDatabase: build
```

`CompileFlags` 段对 `compile_commands.json` 里的编译命令做加工。`Add` 在每条命令后追加 flag——`-Wall -Wextra` 让 clangd 的报错和真编译一样严，`-Wno-unused-parameter` 让它跟项目里 callback 那种必须有但用不上的参数放过。`Remove` 用通配符干掉 flag，典型场景是 `compile_commands.json` 里有 `-fsanitize=thread`（卷五讲过 TSan），clangd 不需要重跑它、跑了反而报奇怪的 diagnostics。`Compiler` 把编译器可执行名替换成指定值，写 `clang++` 是让 clangd 用 Clang 自家驱动去探系统头和 ABI，交叉编译场景下特别有用（原始编译器是 `arm-none-eabi-g++`、clangd 探不到 sysroot 时换成 `clang++` 配 `--query-driver` 就能解决）。`CompilationDatabase` 上面讲过，指 compile_commands 所在目录。

```yaml
Index:
  Background: Build
  StandardLibrary: Yes
```

`Index` 段管 clangd 的索引。`Background: Build` 是开后台索引（首次打开项目慢就是它在干活），索引落盘在 `~/.cache/clangd/index/` 下，下次打开同一项目复用，不用从头来。`StandardLibrary: Yes` 把标准库符号纳入索引，您敲 `std::` 才能补全出 `vector`、`cout` 这些。这两项默认就是开的，写出来是为了显式说明。

```yaml
InlayHints:
  Enabled: Yes
  ParameterNames: Yes
  DeducedTypes: Yes
  Designators: Yes
  BlockEnd: Yes
```

`InlayHints` 是 clangd 18+ 的行内提示，灰色虚文字直接显示在代码行内。`ParameterNames: Yes` 在函数调用处显示参数名 `greet(/*name=*/"WSL")`，省得来回切到声明看参数叫啥。`DeducedTypes: Yes` 显示 `auto` 推导出来的类型 `auto /*= int*/ sum`。`Designators: Yes` 在结构体聚合初始化时显示字段名 `Point{/*.x=*/1, /*.y=*/2}`。`BlockEnd: Yes` 在大段 `}` 后面显示它属于哪个函数/命名空间，几千行函数尾部那个 `}` 不再是迷。这一组是 vscode clangd 扩展默认不开的，开了之后代码可读性提升一个台阶。

```yaml
Diagnostics:
  ClangTidy:
    Add: [modernize-*, bugprone-*, performance-*, readability-*]
    Remove: [modernize-use-trailing-return-type, readability-magic-numbers]
  UnusedIncludes: Strict
  MissingIncludes: Strict
  Suppress: [unused-includes]
```

`Diagnostics` 段管红线/黄线。`ClangTidy.Add/Remove` 让 clangd 直接在编辑器里跑 clang-tidy 检查，不用手动开终端。`modernize-*` 一开，您写 `NULL` 它提示用 `nullptr`、写 `for (int i = 0; i < v.size(); ++i)` 它提示换成范围 for。`Remove` 把噪音 check 关掉——`modernize-use-trailing-return-type` 强制要求 `auto foo() -> int` 这种写法，社区吵了好多年，多数项目不要。`UnusedIncludes: Strict` 和 `MissingIncludes: Strict` 开启 clangd 内置的 include-cleaner，标记「include 了但没用上」和「用上了但没 include」两种问题。**刚上手的项目这两项先关掉**，老项目一开会满屏黄波浪线，会让人想直接卸 clangd。`Suppress` 屏蔽具体诊断 code，比改 check 更精准。

```yaml
Hover:
  ShowAKA: Yes
```

`Hover` 段管鼠标悬停提示。`ShowAKA: Yes` 让 typedef/using 别名悬停时同时显示原始类型，`size_type` 悬停能看到底下是 `std::size_t`。

### clangd 扩展的 settings.json 关键项

`.clangd` 文件管的是 clangd 这个程序的行为，vscode clangd 扩展还有一组自己的设置在 `settings.json` 里。下面这份是和 `.clangd` 配套的关键项（仓库 `code/examples/vol7/wsl-clangd/.vscode/settings.json` 里有完整版）：

```json
{
    "C_Cpp.intelliSenseEngine": "disabled",
    "clangd.arguments": [
        "--background-index",
        "--clang-tidy",
        "--header-insertion=iwyu",
        "--all-scopes-completion",
        "--function-arg-placeholders",
        "--pch-storage=disk",
        "--inlay-hints",
        "--j=4"
    ],
    "clangd.onConfigChanged": "restart"
}
```

`C_Cpp.intelliSenseEngine: disabled` 是篇 5 那一步的核心——把 C/C++ 扩展的代码理解关掉，让 clangd 独占。`clangd.arguments` 是 clangd 启动时的命令行参数。`--background-index` 显式开后台索引、`--clang-tidy` 开 clang-tidy 集成（配合 `.clangd` 的 `Diagnostics.ClangTidy` 和 `.clang-tidy` 文件）、`--header-insertion=iwyu` 接受补全时自动补 `#include`、`--all-scopes-completion` 让补全跨越当前 namespace（您在某个命名空间里也能补全局符号）、`--function-arg-placeholders` 函数补全带参数占位符、`--pch-storage=disk` PCH 落盘省内存、`--inlay-hints` 启用行内提示（clangd 18+）、`--j=4` 后台并行度。

`clangd.onConfigChanged: restart` 这条很关键：您改了 `.clangd` 之后，clangd 自动重启加载新配置。不开这个的话，改 `.clangd` 得手动 `Ctrl+Shift+P` 跑 `clangd: Restart language server` 才生效。

### Background Index：大项目第一次开慢是正常的

打开一个几万行的项目，clangd 启动后会盯着状态栏转圈几分钟甚至十几分钟。这是后台索引在跑：它在解析所有源文件、抽取符号和引用关系、写到 `~/.cache/clangd/index/` 落盘。第一次跑完之后，索引复用，第二次打开就快了。

验证它确实在干活，看 clangd 的输出面板（`View → Output → clangd`），能看到这样的日志：

```text
I[15:32:11.456] Indexing xxx.cpp
I[15:32:11.612] Indexed preamble symbols: 1240
I[15:32:11.738] Background: 1450 indexed, 0 dirty
```

如果项目特别大（比如 Chromium 这种），索引吃内存几个 G，您机器扛不住可以关后台索引，`Background: Skip` 或 `--background-index=0`。但代价是跨文件跳转和补全变慢，因为没建跨文件索引。多数项目开着没问题。

### clang-tidy 集成

clangd 内置的 clang-tidy 集成让静态检查直接进编辑器，不用切终端。它的工作方式是这样的：

工程根放一个 `.clang-tidy` 文件（YAML 格式），写要开哪些 check：

```yaml
Checks: >
    -*,
    modernize-*,
    bugprone-*,
    performance-*,
    readability-*,
    -modernize-use-trailing-return-type,
    -readability-magic-numbers,
    -readability-identifier-length
WarningsAsErrors: ''
HeaderFilterRegex: '.*'
FormatStyle: file
```

`Checks` 第一项 `-*` 关掉所有默认 check，后面再 `modernize-*` 这种 glob 逐组开。`-` 前缀是关。`HeaderFilterRegex` 决定 clang-tidy 检查哪些头文件——`.*` 是所有，对第三方库噪音多就改成自己工程的头文件正则。

clangd 启动时会自动读这个文件。`settings.json` 里 `--clang-tidy` 开了之后，您每改一行代码、clangd 都会顺手跑相关的 clang-tidy check，问题直接画成黄线/红线在编辑器里。

笔者实测了一段代码触发 `readability-identifier-length`：

```text
$ cat tidy_demo.cpp
#include <cstdint>
int main() {
    int big = 1000000000;
    long narrowed = big;
    int* p = nullptr;   // ← 名字太短,3 字符以下被 check 拦
    return 0;
}

$ clang-tidy -p build tidy_demo.cpp
... tidy_demo.cpp:5:10: warning: variable name 'p' is too short,
    expected at least 3 characters [readability-identifier-length]
    5 |     int* p = nullptr;
      |          ^
```

同样的诊断在 vscode 里就是 `p` 那个变量名下面一条黄波浪线，鼠标悬停显示 `[readability-identifier-length]`。clangd 集成版不用您开终端，写完代码问题直接出现。

### include-cleaner

clangd 内置的 include-cleaner（不依赖外部 clang-tidy）专门治 include 的两个毛病：include 了但没用上、用上了但没 include。开关在 `.clangd` 的 `Diagnostics` 段：

```yaml
Diagnostics:
  UnusedIncludes: Strict   # None = 关, Strict = 严格开
  MissingIncludes: Strict
```

笔者建议：**新项目一开始就开**，include 关系从源头干净；**接手老项目先 `None`**，老代码 include 历史包袱重，一开 Strict 满屏黄线会让人失去判断力。先理顺代码再开。

include-cleaner 还支持 IWYU pragma，写在头文件里给工具下指令：

```cpp
#include <vector>  // IWYU pragma: export
#include "detail_helpers.h"  // IWYU pragma: keep  ← 即便没用上也别警告
```

`export` 是「我这个头替使用者 include 了 `<vector>`，使用者不用再 include」；`keep` 是「这条 include 别给我标记成 unused」。大型库里这两种 pragma 用得多，避免 include-cleaner 误报。

### clangd 还是 C/C++ 扩展（跟起步卷对齐）

到这里您可能问：C/C++ 扩展是不是该卸了？不行。和 [起步卷篇 5](/getting-started/05-vscode-clangd) 的口径一致：

- clangd 管「看懂代码」——补全、跳转、报错、悬停、行内提示、clang-tidy。准。
- C/C++ 扩展留「调试」——断点、单步、看变量、调用栈。它带的 `cppdbg` 调试器是 vscode 上调 gdb/lldb 最成熟的方案。

所以 `C_Cpp.intelliSenseEngine: disabled` 关的是 C/C++ 扩展的代码理解，扩展本身不卸。两个分工不打架。下面调试试的就是 C/C++ 扩展的 `cppdbg`。

## 调试配置：launch.json

工程能编、clangd 能跳转之后，最后一个环节是调试：打断点、单步、看变量。本篇这一段把原稿戛然而止在「切换到调试栏点击」那句话的地方补完。

vscode 调 C++ 走 `.vscode/launch.json`。这里给一份完整可用的配置（仓库 `code/examples/vol7/wsl-clangd/.vscode/launch.json` 里也是它），用 C/C++ 扩展的 `cppdbg` + gdb：

```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "(gdb) Launch greeter",
            "type": "cppdbg",
            "request": "launch",
            "program": "${workspaceFolder}/build/greeter",
            "args": [],
            "stopAtEntry": false,
            "cwd": "${workspaceFolder}",
            "environment": [],
            "externalConsole": false,
            "MIMode": "gdb",
            "miDebuggerPath": "/usr/bin/gdb",
            "setupCommands": [
                {
                    "description": "Enable pretty-printing for gdb",
                    "text": "-enable-pretty-printing",
                    "ignoreFailures": true
                }
            ],
            "preLaunchTask": "build"
        }
    ]
}
```

逐字段说。`type: cppdbg` 是 C/C++ 扩展提供的调试器类型，靠 gdb 的 MI 协议驱动 gdb。`program` 是要调的可执行文件全路径，`${workspaceFolder}` 是 vscode 当前打开的工程根目录。`MIMode: gdb` 配 `miDebuggerPath: /usr/bin/gdb` 告诉它走 WSL 里的 gdb。`preLaunchTask: build` 是按下 F5 之前先跑一个叫 `build` 的 task（下面 tasks.json 里定义），build 失败就不启动调试，省得调一个旧版本的二进制。

`setupCommands` 里的 `-enable-pretty-printing` 是关键。不开它，您断点上看一个 `std::vector<int> v{1,2,3,4,5}`，变量面板显示的是一堆原始成员（`_M_start`、`_M_finish`、`_M_end_of_storage` 这种 libstdc++ 内部指针），完全看不出 vector 里是 `{1,2,3,4,5}`。开了之后 gdb 用 Python pretty-printer 把它格式化成可读形式。下面是笔者本机的真实 gdb 输出对比：

```text
(gdb) print nums        # nums 是 std::vector<int>{1,2,3,4,5}

没开 pretty-printing:   $1 = {_M_impl = {_M_start = 0x555..., _M_finish = ..., _M_end_of_storage = ...}}
开了 pretty-printing:   $1 = std::vector of length 5, capacity 5 = {1, 2, 3, 4, 5}
```

vscode 里把 `setupCommands` 配上之后，变量面板显示的就是后者那种可读形式。这一步新手最容易漏：能调但变量看不懂，断点打了等于没打。

::: tip CodeLLDB 备选
如果您偏好 lldb，装 CodeLLDB 扩展（`vadimcn.vscode-lldb`）+ WSL 里 `sudo apt install lldb`，launch.json 改用 `"type": "lldb"`。CodeLLDB 不走 MI 协议、直接驱动 lldb，启动更快、对 C++ 类型显示更友好（不用配 pretty-printing，自带）。但本教程统一用 gdb，下面例子都基于 gdb。
:::

配好之后，在 `main.cpp` 第 14 行（`for (int x : nums)` 那行）左侧边栏点一下打个红点断点，按 `F5`。vscode 先跑 `build` task 重新编一次，编完启动 gdb 加载 `build/greeter`，跑到断点处停住。左侧「运行和调试」面板能看到调用栈、变量、断点、监视。变量面板里 `nums` 展开是 `std::vector of length 5, capacity 5 = {1, 2, 3, 4, 5}`，`sum` 是当前的累计值。按 `F10` 单步步过、`F11` 单步步入、`F5` 继续运行。

## tasks.json 构建任务

launch.json 里那个 `preLaunchTask: build` 需要一个对应的 task。task 在 `.vscode/tasks.json` 里定义：

```json
{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "build",
            "type": "shell",
            "command": "cmake",
            "args": [
                "--build",
                "${workspaceFolder}/build",
                "--config",
                "Debug",
                "--parallel"
            ],
            "options": {
                "cwd": "${workspaceFolder}"
            },
            "group": {
                "kind": "build",
                "isDefault": true
            },
            "problemMatcher": ["$gcc"]
        },
        {
            "label": "configure",
            "type": "shell",
            "command": "cmake",
            "args": [
                "-S", "${workspaceFolder}",
                "-B", "${workspaceFolder}/build",
                "-G", "Ninja",
                "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
            ],
            "options": { "cwd": "${workspaceFolder}" },
            "problemMatcher": []
        },
        {
            "label": "rebuild",
            "dependsOn": ["configure", "build"],
            "dependsOrder": "sequence",
            "group": "build",
            "problemMatcher": []
        }
    ]
}
```

三个 task 分工。`build` 跑增量构建（`cmake --build build`，底层 Ninja），它是默认 build task（`isDefault: true`），所以 `Ctrl+Shift+B` 直接触发它。`configure` 第一次或改了 `CMakeLists.txt` 之后跑，重新 configure 一次刷新 `compile_commands.json`。`rebuild` 用 `dependsOrder: sequence` 顺序跑 configure 再跑 build，一把梭。

`problemMatcher: ["$gcc"]` 这一项让 vscode 解析编译器输出，把报错/warning 转成「问题」面板里的可点击条目，点一下跳到对应行。这是 vscode 内置的 `$gcc` 模式，匹配 gcc/clang 的报错格式。

launch.json 按 F5 触发的链是：跑 `build` task → build 成功 → 启动 gdb 加载 `build/greeter` → 跑到断点停。整个调试循环就这么闭环了，不用每次手动切终端敲 `cmake --build`。

## 到这里

WSL2 + vscode + clangd + cppdbg 这一套配齐之后，您手上的 C++ 工程化环境跟一个资深 Linux 开发者用的几乎没差别：补全准、跳转快、报错严、调试能看 vector。后面读卷七 ch00 的 CMake 系列（target 心智模型、CMakePresets.json）、卷六的内存安全（AddressSanitizer + valgrind），命令都是直接在 WSL 终端里敲，跟文章里的输出对得上。

配套的所有配置文件（`.clangd`、`.clang-tidy`、`.vscode/settings.json`、`launch.json`、`tasks.json`）都存在仓库 `code/examples/vol7/wsl-clangd/` 下，clone 下来直接能跑。CMake 工程最小可复现，`cmake -B build -G Ninja && cmake --build build` 出 `build/greeter`，按 F5 进调试。
