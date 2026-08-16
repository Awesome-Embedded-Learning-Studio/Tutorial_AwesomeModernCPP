---
title: "卷 7 Project：fcopy++ 工程化文件拷贝器"
description: "软件工程实践卷的综合项目：把教材的 FileCopier 做成一个可交付的工程 fcopy++——CMake target 结构、进度/速度/ETA 反馈、CLI 选项、警告与 sanitizer 质量门、CTest 测试，最后挑战 C++20 模块化、八组合矩阵发布与增量构建证据。任务分四层，难度 L1~L5。"
chapter: 7
order: 5
tags: [host, intermediate, cpp-modern, CMake, 工程实践]
difficulty: intermediate
platform: host
cpp_standard: [17, 20]
reading_time_minutes: 4
prerequisites:
  - "卷 7 全部章节（第 7.1~7.8 章）"
related:
  - "卷 7 Homework"
  - "卷 7 Lab：发布流水线"
---

# 卷 7 Project：fcopy++ 工程化文件拷贝器

## 项目定位

把本卷的家当全部用进一个真实的小程序：`fcopy`——教材文件拷贝器的工程化版本。CMake 管工程结构（静态库 + 可执行 + INTERFACE 警告库）、编译器选项管质量（`-Wconversion` 级别的严格警告 + sanitizer）、拷贝核心管功能（分块读写 + 大小校验）、`chrono` 管测量（进度 / 速度 / ETA）、CTest 管验收。任务分四层，一层一层往上盖；卡住了看[参考实现](./06-project-solutions.md)，它按层组织，可以只读你卡住的那层。所有构建在 `/tmp` 独立目录做。

## 任务分层

### 核心任务（L2）：能跑起来的拷贝器 {#pj-core}

**L1 热身**：先把 `include/fcopypp/copier.hpp` 的 `CopyStats` 结构体和 `copy_file` 声明、以及 `src/copier.cpp` 里返回 `false` 的占位实现搭起来——不实现逻辑，只求 `g++ -std=c++17 -Wall -Wextra -c src/copier.cpp -Iinclude` 零警告通过。

实现 `copy_file(src, dst, chunk_size, stats_out)`：前置检查（存在性、目录拒绝、`file_size`）→ 二进制双流 → `vector<char>` 缓冲 → `while (in)` + `read`/`gcount`/`write` 主循环 → `flush`/`close` → 大小校验 → 把 `total/copied/elapsed_seconds` 填进 `CopyStats`。配 `main` 解析两个参数、调用、按返回码退出，全部放进 CMake 工程（`fcopypp_core` 静态库 + `fcopy` 可执行）。

**验收标准**：`cmake -S . -B build -G Ninja` 配置、构建全绿；拷贝 524288 字节文件 `cmp` 一致、程序按返回码报告成败。贴出配置、构建、拷贝验证的完整输出。（CTest 到 L4「把门装上」才引入，本层以手动运行验收。）

[参考实现 →](./06-project-solutions.md#pj-core)

### 进阶任务（L3）：进度反馈与 CLI {#pj-extra}

三件事。①**进度反馈**：实现 `progress` 模块的三个纯函数——`percent_of`（百分比）、`speed_bps`（字节/秒）、`eta_seconds`（剩余秒数，注意除零与「已完成」的边界）；`main` 里用 `\r` 渲染一行 `100% | 2858.3 MB/s | ETA 0s` 风格的动态进度，最后换行打印总耗时与平均速度。②**`--chunk-size N` 选项**：解析可变参数（格式 `fcopy <src> <dst> [--chunk-size N]`），不认识的选项要报错退出。③**错误路径全覆盖**：源不存在、目标目录不存在、源是目录，三种失败各自贴出真实输出与退出码。

**验收标准**：贴出 64MB 文件拷贝的最终进度行与总耗时行、三种错误路径输出；说清 `percent_of` 里 `total == 0` 的分支为什么必要（空文件）。

[参考实现 →](./06-project-solutions.md#pj-extra)

### 再进阶任务（L4）：把门装上 {#pj-gates}

三件事。①**警告门**：`INTERFACE` 库 `project_warnings` 挂 `-Wall -Wextra -Wpedantic -Wshadow -Wconversion`，`option(FCOPY_WARNINGS_AS_ERRORS ...)` 用生成器表达式按开关追加 `-Werror`——开启后全工程必须零警告（`-Wconversion` 会逼你把每个隐式转换显式化，这是本层最难的点）。②**测试门**：`option(FCOPY_BUILD_TESTS ...)` + `enable_testing()` + `add_test`，测试脚本覆盖：非整块大小逐字节一致（`cmp -s`）、空文件、源不存在非零退出、自定义块大小逐字节一致。③**sanitizer 门**：`-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"` 单独构建，同一套测试在 sanitizer 下**零报告**通过。

**验收标准**：贴出「`FCOPY_WARNINGS_AS_ERRORS=ON` 构建成功」（即零警告）的日志、`ctest` 的 1/1 通过、sanitizer 构建下 `ctest` 的 1/1 通过与零报告。

[参考实现 →](./06-project-solutions.md#pj-gates)

### 终极挑战（L5）：模块化 + 矩阵发布 + 增量证据 {#pj-l5}

三件挑战，全部用本卷的知识完成（本卷 L5＝「复杂构建场景攻坚」类，口径见[练习总览](./index.md)）。①**C++20 模块化**：把进度三函数搬进模块 `copy_stats`（接口只许用核心语言类型，坑见参考实现——直接引 `std::string` 出接口在本机 GCC 会撞出什么真实报错，先自己试出来），`main` 改 `import copy_stats;`，CMake 4 的 `FILE_SET CXX_MODULES` 构建，产物与 L3 行为一致。②**矩阵发布**：`{g++, clang++} × {Debug, Release} × {Ninja, "Unix Makefiles"}` 共 8 个组合、全部开 `FCOPY_WARNINGS_AS_ERRORS=ON`，每个组合独立构建目录（都在 `/tmp`），`size` 取 text 体积出对比表——8 格必须全绿。③**增量构建证据**：`touch src/main.cpp` 与 `touch src/copier.cpp` 各一次，用 `ninja -C build -v` 贴出实际重编清单，证明构建系统只重编了该重编的文件。

**验收标准**：贴出模块构建日志与运行输出；8 行矩阵表；两次触碰的重编清单；一句话说清「模块接口为什么限定核心语言类型」。

[参考实现 →](./06-project-solutions.md#pj-l5)

## 提交物清单

项目目录（`include/` + `src/` + `tests/` + 顶层 `CMakeLists.txt` + 模块变体目录）+ 各层终端记录 + 200 字以内小结：说说这个项目里哪一处让你对「卷 7 的知识点是一体的」体会最深。
