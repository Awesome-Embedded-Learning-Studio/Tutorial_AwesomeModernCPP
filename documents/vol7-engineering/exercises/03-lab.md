---
title: "卷 7 Lab：发布流水线"
description: "软件工程实践卷的动手实验：把一个 wc 风格的文本统计器 analyzer 从「一段源码」一路推到「双编译器八组合零警告发布」——六步走完 CMake 工程、警告门、target 语义、段与符号、调试实战、C++20 模块化，最后附一道矩阵构建攻坚的 L5 挑战。每步有目标、步骤与验收标准，实验参考独立成文件。"
chapter: 7
order: 3
tags: [host, intermediate, cpp-modern, CMake, 调试]
difficulty: intermediate
platform: host
cpp_standard: [17, 20]
reading_time_minutes: 7
prerequisites:
  - "卷 7 全部章节（第 7.1~7.8 章）"
related:
  - "卷 7 Homework"
  - "卷 7 Project：fcopy++ 工程化文件拷贝器"
---

# 卷 7 Lab：发布流水线

## 实验目标

本卷的知识点是一条流水线：CMake 工程 → 警告门 → target 语义 → 段与符号 → 调试 → 模块化 → 多组合发布。单独看每样都懂，串起来才是一个能交付的工程。这个 Lab 让你陪一个 `analyzer`（统计文本的 bytes/lines/words，`wc` 的迷你版）走完整条流水线——每一步都真开终端、真构建、真看输出，验收标准不满足就不算过。

所有实验在 `/tmp` 下独立目录做，编译基线 `g++ -std=c++17 -Wall -Wextra`。每步有验收标准；卡住先回题面每步标注的章节链接读教材，再不行看[实验参考](./04-lab-solutions.md)。

## 步骤 1：最小 CMake 工程 {#lab-1}

难度 **L1** · 涉及[交叉编译与 CMake](../01-cross-compilation-and-cmake.md)、[WSL 开发 C++](../cpp-development-on-wsl.md)

**目标**：把三文件程序（`main.cpp` + `analyzer_core.h` + `analyzer_core.cpp`）装进最小 CMake 工程，Debug 配置构建跑通。

1. `analyzer_core` 提供 `struct Counts { bytes, lines, words }` 和 `Counts count_text(const std::string&)`：按空白切词、数换行、数字节。
2. `main.cpp` 用 `std::ifstream` + `istreambuf_iterator` 把文件整体读成 `std::string`，调用 `count_text` 打印三计数。
3. `CMakeLists.txt`：`cmake_minimum_required` + `project` + `CMAKE_CXX_STANDARD 17` + `add_executable`；`cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug` 配置、构建。
4. 自备 `sample.txt`（三行：`hello world` / `modern c++ vol7` / `bye`），先手算 bytes/lines/words，再真跑对答案。

**验收标准**：贴出配置与构建输出、`analyzer sample.txt` 的输出（应和手算一致）；说清 `-S`/`-B`/`-G` 三个参数各管什么。

[实验参考 →](./04-lab-solutions.md#lab-1)

## 步骤 2：警告全歼 {#lab-2}

难度 **L2** · 涉及[编译器选项](../02-compiler-options.md)

**目标**：亲手经历「警告 → Werror 拦截 → 修复归零」的全流程，把 `-Wall -Wextra -Wpedantic -Wshadow -Werror` 变成肌肉记忆。

1. 写 `main_bad.cpp`，埋三颗雷：内层变量遮蔽外层（`-Wshadow`）、`unsigned` 与 `int` 混比（`-Wsign-compare`）、定义了从不使用的变量（`-Wunused-variable`）。
2. 先用 `-Wall -Wextra -Wpedantic -Wshadow` 编译，逐条认清警告；再加 `-Werror` 编译，贴出「警告变错误」的报错。
3. 修到零警告：遮蔽改名、比较显式转换、删死变量；`-Werror` 全套旗标下编译运行。

**验收标准**：贴出三次编译的输出（警告版 / 拦截版 / 修复版）；说清 `-Werror` 在 CI 里的定位，以及为什么「先修完再上 Werror」是新手最省的路线。

[实验参考 →](./04-lab-solutions.md#lab-2)

## 步骤 3：target 语义 {#lab-3}

难度 **L2** · 涉及[交叉编译与 CMake](../01-cross-compilation-and-cmake.md)

**目标**：把工程拆成「静态库 + 可执行」两个 target，验证 `PUBLIC`/`PRIVATE` 的传播规则。

1. `analyzer_core` 建成静态库：include 目录挂 `PUBLIC`，同时挂 `PUBLIC` 和 `PRIVATE` 各一个编译定义（如 `CORE_PUBLIC_FLAG` / `CORE_PRIVATE_FLAG`）。
2. `app` 里的 `#ifdef` 检测两个宏：哪个可见、哪个不可见？
3. 构建运行，`nm` 看一眼静态库里的符号。

**验收标准**：贴出构建与运行输出（两行宏检测结果都要）；一句话说清 `PUBLIC` 与 `PRIVATE` 沿依赖图传播的差别，并回答「app 为什么能 include 到库的头文件」。

[实验参考 →](./04-lab-solutions.md#lab-3)

## 步骤 4：段与符号侦探 {#lab-4}

难度 **L3** · 涉及[链接器与链接脚本](../03-linker-and-linker-scripts.md)、[编译器选项](../02-compiler-options.md)

**目标**：用 `nm`/`size`/`objdump -h` 解剖步骤 3 的产物，再补两个「体积真相」实验。

1. `nm -C` 静态库：找到 `count_text` 的符号类型；`size` 和 `objdump -h` 看可执行文件的 `.text/.data/.bss/.rodata` 各多大、在哪个地址。
2. 大数组实验：`static char big_zero[4*1024*1024];` 与 `static char big_data[4*1024*1024] = {1};` 放同一个程序，`ls -l` + `size`——为什么 4MB 数组几乎不占文件空间、另一个占满 4MB？
3. 垃圾回收实验：两个函数分放两文件、只调用其一；对比「普通构建」与「`-ffunction-sections -fdata-sections -Wl,--gc-sections`」的 `size`，用 `nm` 证明死函数消失。

**验收标准**：贴出三组输出；说清 `.bss` 为什么不在镜像里占空间、gc-sections 为什么需要「编译端分区 + 链接端回收」配合。

[实验参考 →](./04-lab-solutions.md#lab-4)

## 步骤 5：调试实战 {#lab-5}

难度 **L4** · 涉及[MSVC 调试原理](../msvc-debugging-internals.md)、[编译器选项](../02-compiler-options.md)

**目标**：把调试三能力（观测/控制/映射）在 gdb 里各做一遍，再用 ASan 抓一个真实的栈溢出。

1. `-g -O0` 构建 analyzer：`break an::count_text`、`run sample.txt`、`bt` 看调用栈、`print text.size()` 观测参数。
2. 对比 `-O2` 无 `-g` 构建：`readelf -S` 数 debug 段、`ls -l` 看体积差。
3. 埋雷：写一个 `char token[4]; strcpy(token, line)` 的 `buggy.cpp`，用 `-fsanitize=address` 构建、输入 5 字节触发，贴出 ASan 报告——它点名了哪个变量、哪一行、退出码多少。

**验收标准**：贴出 gdb 会话、两次构建的 debug 段与体积对比、完整 ASan 报告；对照教材说出 gdb 的 `bt`/`print`/`break` 分别对应调试三能力里的哪一个。

[实验参考 →](./04-lab-solutions.md#lab-5)

## 步骤 6：模块登场 {#lab-6}

难度 **L4** · 涉及[VS2026 使用 C++ 模块](../cpp-modules-on-vs2026.md)、[交叉编译与 CMake](../01-cross-compilation-and-cmake.md)

**目标**：把 `count_text` 搬进 C++20 模块 `analyze`，用 CMake 4 的 `FILE_SET CXX_MODULES` 构建，并用「触碰实验」拿到增量编译证据。

1. 写 `analyze.cppm`：`export module analyze;` + `export struct Counts` + `export Counts count_text(...)`。**接口只许用核心语言类型**（`const char*` + `unsigned long long`）——直接 `#include <string>` 会怎样？先试、贴报错，再改（坑的解释见实验参考）。
2. `main.cpp` 改成 `import analyze;`，文件读入仍用标准库（消费者侧 include 头文件没问题）。
3. CMake：`CMAKE_CXX_STANDARD 20` + `target_sources(... FILE_SET CXX_MODULES FILES analyze.cppm)` + `-fmodules`。构建后做三次触碰：`touch main.cpp`、`touch analyze.cppm`、零改动，每次用 `ninja -C build -v` 贴出实际重编清单。

**验收标准**：贴出构建日志、运行输出、三次触碰的重编清单；说清「接口模块变了 → 谁跟着重编」的证据链，以及 BMI 缓存在哪一步省了编译。

[实验参考 →](./04-lab-solutions.md#lab-6)

## 附加挑战（L5）：矩阵发布攻坚 {#lab-l5}

**目标**：把 analyzer 推到「可发布」状态——2 编译器 × 2 配置 × 2 生成器共 8 个组合全部零警告构建通过，外加一次「埋雷必拦」的门禁演示。本题口径：难度按「复杂构建场景攻坚」标定 L5（本卷 L5 口径见[练习总览](./index.md)）。

1. 把步骤 3 的工程加上 `INTERFACE` 警告库（`-Wall -Wextra -Wpedantic -Wshadow -Werror`），全部 target 链接继承。
2. 写循环脚本跑矩阵：`{g++, clang++} × {Debug, Release} × {Ninja, "Unix Makefiles"}`，每个组合独立构建目录（都在 `/tmp`），用 `size` 取 text 体积，输出一张对比表。
3. 埋雷验收：复制一份工程，在 `main.cpp` 里加一个未用变量，构建必须失败、贴出 `-Werror` 的拦截报错。

**验收标准**：贴出 8 行矩阵表（全部 build OK）+ 运行输出；贴出埋雷构建的失败报错；一句话说清「换编译器为什么必须换构建目录」。

[实验参考 →](./04-lab-solutions.md#lab-l5)

## 提交物清单

一个目录装下全部源码（每步一份 `CMakeLists.txt` 与源码）、每步终端记录（`stepN.log`）、矩阵表，以及 200 字以内的小结——用你自己的话说清「一条流水线为什么每道闸都缺不得」你在哪一步体会最深。
