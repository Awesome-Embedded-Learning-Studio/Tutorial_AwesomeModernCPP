---
title: "卷 2 Project：logscan 日志巡检器"
description: "现代特性卷的综合项目：做一个命令行日志巡检器 logscan——递归扫描 .log 文件、元数据查询、日志轮转、零拷贝行解析与级别统计。任务分四层（核心/进阶/再进阶/终极），难度 L1~L5，参考实现分文件逐段讲解、附真实运行输出。"
chapter: 2
order: 5
tags: [host, intermediate, cpp-modern, 移动语义, 类型安全, 工程实践]
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17]
reading_time_minutes: 4
prerequisites:
  - "卷 2 全部章节（第 0~11 章）"
related:
  - "卷 2 Homework"
  - "卷 2 Lab：零拷贝配置读取器"
---

# 卷 2 Project：logscan 日志巡检器

## 项目定位

把本卷的家当全部用进一个真实的小程序：`logscan`——命令行日志巡检器。文件系统库扫目录、`path` 做路径处理、`string_view` 做零拷贝行解析、`variant`/`enum class` 管级别、自制 `expected` 报错、`unique_ptr` 管资源、移动语义管结果搬运。四个命令：`scan`（递归找 .log）、`info`（大小/修改时间/行数）、`rotate`（超阈值轮转）、`analyze`（按行统计 INFO/WARN/ERROR）。任务分四层，一层一层往上盖；卡住了看[参考实现](./06-project-solutions.md)，它按层组织，可以只读你卡住的那层。

## 任务分层

### 核心任务（L2）：能跑起来的扫描器 {#pj-core}

**L1 热身**：先把 `logscan.hpp` 的 `Summary` 结构体和四个命令函数声明搭起来——不实现逻辑，只求 `g++ -std=c++17 -Wall -Wextra -c` 零警告通过。

实现命令 `scan <dir>`：用 `recursive_directory_iterator` + `skip_permission_denied` 递归收集目录树下所有扩展名为 `.log` 的普通文件，按路径排序输出「路径 + 字节数」。配一个 `main` 里的命令循环（`scan/info/rotate/analyze/quit`，`quit` 退出）。

**验收标准**：`-Wall -Wextra` 编译零警告；在自建的测试目录树（含子目录、非 .log 文件）上跑 `scan`，输出全部 .log 文件并排序。贴出编译命令和 `scan` 的完整输出。

[参考实现 →](./06-project-solutions.md#pj-core)

### 进阶任务（L3）：info 与 rotate {#pj-avg}

加两个命令。`info <file>`：打印文件大小（字节）、最后修改时间（C++17 下 `file_time_type` 到 `time_t` 的转换）、总行数（`getline` 计数）；文件不存在要报错。`rotate <file> <max_kb> <backups>`：文件 ≥ 阈值时把 `x.N.log` 依次后移一位、当前日志改名为 `.1`、新建空文件；低于阈值要打印「no rotation」和当前大小。

**验收标准**：贴出 `info` 的输出（大小/时间/行数三行）；`rotate` 做两轮演示——一轮触发轮转（贴出 `rotate` 输出和轮转后 `scan` 结果），一轮不触发（贴出「below threshold」）。

[参考实现 →](./06-project-solutions.md#pj-avg)

### 再进阶任务（L4）：把门装上 {#pj-gates}

三件事。①`analyze <file>`：逐行解析 `"[LEVEL] message"` 格式——`string_view` 零拷贝拆出 `[...]` 里的级别令牌和后面的消息体，`[INFO]/[WARN]/[ERROR]` 计数；格式坏的行（不以 `[` 开头、缺 `]`、级别不认识）**按行号报错**并统计坏行数，第一条 ERROR 的消息要记录下来。②错误信息走自制 `expected`（错误类型 `LineError{行号, 消息}`）——从解析函数一路传到命令层，不用任何 `optional`+`bool` 的拼凑。③质量门：`-Wall -Wextra` 零警告；`-fsanitize=address,undefined` 构建跑完整会话零报告。

**验收标准**：贴出对一份含 `[TRACE]` 坏行日志的 `analyze` 输出（三类计数 + 坏行数 + 坏行原文 + 第一条 ERROR）；贴出 sanitizer 构建下同一会话的输出（零报告）。

[参考实现 →](./06-project-solutions.md#pj-gates)

### 终极挑战（L5）：编译期级别分派 {#pj-l5}

三件挑战，全部用本卷的知识完成（本卷 L5＝「用本卷知识可解的最难问题」，口径见[练习总览](./index.md)）。①**编译期哈希分派**：`constexpr` FNV-1a 哈希 + UDL `"INFO"_hash`，把 `analyze` 里的级别识别从 `if/else` 链换成 `switch (hash_string(token))` 的哈希分派（case 标签全是编译期常量）——顺带说清哈希分派的碰撞风险怎么兜底。②**编译期级别名表**：`constexpr const char* kLevelNames[]` 按 `LogLevel` 枚举值索引，`static_assert` 保证表长度与枚举数量一致，`analyze` 输出用它打印级别名。③**unique_ptr 持有结果**：统计结果 `Summary` 放在 `unique_ptr` 里构造、在命令函数内部就地更新并打印（不随函数返回——大结构体由单一所有者持有、不拷贝）；三个挑战全部在 sanitizer 构建下零报告。

**验收标准**：贴出挑战①的关键代码（`operator""_hash` + `level_from_token` 的 switch）与一次含坏行 `analyze` 的完整输出；说清 `static_assert` 防漏改、哈希碰撞怎么兜底、`unique_ptr` 持有的结果怎么就地更新。

[参考实现 →](./06-project-solutions.md#pj-l5)

## 提交物清单

项目目录（`include/` + `src/` + 可选 Makefile）+ 各层终端记录 + 200 字以内小结：说说这个项目里哪一处让你对「卷 2 的知识点是一体的」体会最深。
