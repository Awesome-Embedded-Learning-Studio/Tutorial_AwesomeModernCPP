---
title: "卷 10 Project：safelist —— 类型安全的数值列表"
description: "卷 10 综合项目：做一个命令行数值列表工具 safelist——Number 窄化检测守构造、ranges 管道做统计与排序、健壮输入与 sanitizer/格式门守质量，终极挑战把只拦窄化的类型升级成四则运算全查溢出的 SafeInt。任务分四层，难度 L1~L5。"
chapter: 10
order: 5
tags: [host, advanced, cpp-modern, 类型安全, 泛型, Ranges, 工程实践]
difficulty: advanced
platform: host
cpp_standard: [17, 20, 23]
reading_time_minutes: 4
prerequisites: []
related: []
---

# 卷 10 Project：safelist —— 类型安全的数值列表

## 项目定位

把本卷的家当全部用进一个真实的小程序：`safelist`——一个命令行数值列表工具。`Number<T>` 用窄化检测守住每一次构造，`vector` 存数据，ranges 管道做统计与排序，健壮输入、sanitizer 与格式门守质量，终极挑战把「只拦窄化」的类型升级成「四则运算也拦溢出」的 `SafeInt`。任务分四层，一层一层往上盖；卡住了看[参考实现](./06-project-solutions.md)，它按层组织，可以只读你卡住的那层。

环境与笔记一致：WSL Arch，g++ 16.1.1（clang++ 22.1.8 交叉验证），`-std=c++20` 起步。项目不要求 CMake，单文件 `safelist.cpp` 配一条 g++ 命令即可，重点在类型设计与质量门。

## 任务分层

### 核心任务（L2）：能跑起来的数值列表 {#pj-core}

**L1 热身**：先把 `number` concept、`Number<T>` 类声明和 `would_narrow` 的骨架搭起来——不实现完整逻辑，只求 `g++ -c -std=c++20 -Wall -Wextra` 零警告通过。

实现命令 `add`、`list`、`quit`。数据结构：`Number<long long>`（窄化检测构造，`get()` 取值）放进 `std::vector`。`add` 的参数先用 `std::strtold` 解析成 `long double`，再走 `narrow_convert<long long>` 落库——这样「带小数」「超出 long long 范围」的输入在构造那一刻就被拦。`list` 按序打印索引和值。

**验收标准**：`add 100`、`add 2000000000000000000` 成功；`add 3.5` 被窄化检测拦下；`add abc` 报用法错误；`list` 输出正确；`quit` 正常退出。贴出编译命令和一次会话的完整输出。

[参考实现 →](./06-project-solutions.md#pj-core)

### 进阶任务（L3）：统计与排序 {#pj-opt}

加四个命令：`sum`（总和）、`avg`（平均——**整数除法**，想想为什么这样设计并验证你的数字对不对）、`max`（最大值及位置）、`sort`（用 `std::ranges::sort` 按值降序重排后打印）。

**验收标准**：贴出 `sum`/`avg`/`max`/`sort` 的输出；一句话说明你的 `avg` 为什么用整数除法、换成浮点会踩什么坑。

[参考实现 →](./06-project-solutions.md#pj-opt)

### 再进阶任务（L4）：把门装上 {#pj-gates}

四件事。①**健壮性**：`add` 检查输入是不是合法数字（`strtold` 的 endptr 校验）、越界和带小数由窄化检测拦；`calc` 的参数用 `strtoll` + errno 校验。分别用「缺参数」「`abc`」「$3.5$」「$1e300$」测试。②**编译门**：`-Wall -Wextra -Wconversion -Werror` 做到**零警告**（`-Wconversion` 会逼你把每个隐式转换显式化，这条最难）。③**质量门**：`-fsanitize=address,undefined` 构建跑一遍完整会话（含 `1e300` 用例）**零报告**。④**格式门**：`.clang-format`（`BasedOnStyle: LLVM` + `IndentWidth: 4` + `BreakBeforeBraces: Allman` + `ColumnLimit: 100`）下 `clang-format --dry-run --Werror` 退出码 0，格式门之后再严格编译一遍确认没被格式化改坏。附带一个**汇编审计**：`-O2 -S` 看 `do_sum` 的函数体，确认求和循环里没有任何对 `Number::get()` 的函数调用（内联证据）。

**验收标准**：贴出四个健壮性测试的输出、零警告的编译命令、sanitizer 会话零报告、格式门退出码 0、以及 `do_sum` 的汇编片段。

[参考实现 →](./06-project-solutions.md#pj-gates)

### 终极挑战（L5）：SafeInt —— 四则运算全查溢出 {#pj-l5}

挑战任务（受 Bjarne Stroustrup CppCon 2025 演讲的 safe_int 思路启发、按竞赛挑战强化。早期阶段 L5＝「用该阶段知识可解的最难问题」，档位口径见[练习总览](./index.md)）。`Number<T>` 只拦窄化、不拦算术——`LLONG_MAX + 1` 的 signed 溢出是 UB。写一个完整版 `SafeInt`（`long long`）：`+`/`-`/`*` 全部用 `__builtin_*_overflow` 查，`/` 查除零与 `LLONG_MIN / -1`；暴露成 `calc <add|sub|mul|div> <a> <b>` 命令，任何越界抛 `std::overflow_error` 并被命令层捕获打印。再加一个 `chk` 命令跑五条边界自检（LLONG_MAX+1、LLONG_MIN-1、LLONG_MAX×2、LLONG_MIN/-1、42/0），全部 PASS。全套在 sanitizer 构建下零报告。

**验收标准**：贴出 `calc` 的三个越界用例、`chk` 的 5/5 PASS 输出；说明 signed 溢出为什么不能像 unsigned 那样「回绕后再比大小」来查。

[参考实现 →](./06-project-solutions.md#pj-l5)

## 提交物清单

`safelist.cpp` + `.clang-format` + 各层终端记录 + 200 字以内小结：说说这个项目里哪一处让你对「让类型系统守门」体会最深。
