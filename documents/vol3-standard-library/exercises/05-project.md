---
title: "卷 3 Project：CSV 报表流水线"
description: "标准库深入卷的综合项目：命令行 CSV 报表工具 csvreport——string_view 切字段、from_chars 解析、format 出对齐表格、nth_element 与 partial_sort 做统计、expected 错误链与 sanitizer 质量门，最后挑战内存受限的外部排序（408 外排序 + CS61B k 路归并改编）。任务分四层，难度 L1~L5。"
chapter: 3
order: 5
tags:
  - host
  - intermediate
  - cpp-modern
  - 工程实践
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17, 20, 23]
reading_time_minutes: 4
prerequisites:
  - "卷 3 全部章节（容器、迭代器与算法、字符串、I/O、时间与数值、错误处理）"
related:
  - "卷 3 课后练习（Homework）"
  - "卷 3 Lab：标准库解剖台"
---

# 卷 3 Project：CSV 报表流水线

## 项目定位

把本卷的家当全部用进一个真实的小工具：`csvreport`——一个命令行 CSV 报表程序。输入是「名字,分数,年份」的 CSV 文件，`string_view` 切字段、`from_chars` 解析、`std::format` 出对齐表格、`nth_element`/`partial_sort` 做统计、`expected` + `error_code` 管错误，最后用内存受限的外部排序挑战收尾。任务分四层，一层一层往上盖；卡住了看[参考实现](06-project-solutions.md)，它按层组织，可以只读你卡住的那层。

数据文件自己造，参考用这几行（注意故意混入坏行）：

```text
alice,90.5,2022
bob,88,2021
carol,90.5,2022
dave,95,2020
erin,76.5,2021
frank,88,2022
grace,91,2020
heidi,84,2021
ivan,notnum,2021
```

## 任务分层

### 核心任务（L2）：能跑起来的报表 {#pj-core}

**L1 热身**：先搭骨架——`struct Row { string name; double score; int year; }`、`parse_row` 与 `report` 两个空函数声明，外加一段 `sizeof` 侦察（Row / string / `vector<Row>` / `span<Row>` 各占多少字节）和 `__cplusplus` 版本打印，只求 `-std=c++23 -Wall -Wextra` 编译零警告、跑得起来。

实现 CSV 读取：`argv[1]` 传文件路径，`is_open()` 检查后 `while (std::getline(in, line))` 逐行读；每行用 `string_view::find(',')` 切三段，`from_chars` 解析分数（double）与年份（int）——**ec 和 ptr 都要检查**，坏行计数跳过。用 `rows.reserve(64)` 预撑容量，`std::format` 打印 `解析 N 行, 坏行 M` 和一张对齐表格（名字左对齐 8、分数 `>6.1f`、年份 `>6`）。

**验收标准**：编译零警告；对上面的数据文件输出「解析 8 行, 坏行 1」加完整表格；贴编译命令与运行输出。

[参考实现 →](06-project-solutions.md#pj-core)

### 进阶任务（L3）：统计层 {#pj-avg}

加四项统计：`avg`（平均分——`accumulate` 初始值写什么？想想第 44 篇的截断坑，结果应是 87.94 而不是 87）、`median`（`nth_element`，8 个取第 4 位）、前 3 名（`partial_sort` 降序）、按年份分组（`std::map<int, vector<double>>` 自动有序，每组人数与平均分）。再加一个 `find`：按名字查分数——先 `ranges::sort` 按名字投影排好，`ranges::lower_bound` 带投影二分定位 `"frank"`。

**验收标准**：贴出四项统计输出；一句话说明你的总分声明为什么能躲开整数除法坑。

[参考实现 →](06-project-solutions.md#pj-avg)

### 再进阶任务（L4）：把门装上 {#pj-gates}

三件事。① 错误链改造：解析函数改成 `std::expected<Row, std::error_code>`，按 [第 66 篇](../error-utils/66-error-code.md) 的四步搭一套自定义 `CsvErrc`（`kBadFormat`/`kScoreOutOfRange`/`kYearOutOfRange` + category 单例 + `make_error_code`），链上再加一个 `check_range` 检查分数 ∈ [0,100]、年份 ∈ [2000,2100]；坏行逐行打印 `message` 与 category。② 编译加 `-Wall -Wextra -Wconversion -Werror` 做到**零警告**（`-Wconversion` 会逼你把每个窄化转换显式 `static_cast`）。③ 质量门：`-fsanitize=address,undefined` 构建分别跑坏行数据集与正常数据集，**零报告**。迭代器失效审查：全程只 `reserve` + `push_back`、不持有任何迭代器（参考实现会讲为什么这么定）。

**验收标准**：贴三个坏行各自的失败信息（含 category）、零警告编译命令、两次 sanitizer 运行的退出码与「零报告」确认。

[参考实现 →](06-project-solutions.md#pj-gates)

### 终极挑战（L5）：内存受限的外部排序 {#pj-l5}

**目标**：模拟「数据 30000 个、内存一次只装得下 5000 个」的外排序——改编自 **408 数据结构的外部排序/多路归并** 与 **CS61B k-way merge**（本卷 L5＝「用本卷知识可解的最难问题」，档位口径见[练习总览](index.md)）。

1. 用 `mt19937(42)` + `uniform_int_distribution` 生成 30000 个整数的文本文件（`to_chars` 写、一行一个）。
2. **分趟**：一趟读最多 5000 个数、`std::sort` 排好、写成一个有序 run 文件；重复直到读完，记录 run 数。
3. **k 路归并**：每个 run 打开一个 `ifstream`，各读一个数装进 `std::priority_queue<Item, vector<Item>, std::greater<Item>>`（`Item{value, run号}`，重载 `operator>` 让 greater 成最小堆）；每次 pop 堆顶写进输出，再从对应 run 补读一个，直到堆空。
4. **验证**：读回输出文件，`std::is_sorted` 必须为 true；`steady_clock` 计时。

**验收标准**：贴输出（run 数、输出行数、`is_sorted 验证: true`、耗时）；说清「内存只能装 5000 个」时，为什么直接 `std::sort` 整份数据不成立、k 路归并为什么能做到 O(N log k) 趟内排序。

[参考实现 →](06-project-solutions.md#pj-l5)

## 提交物清单

项目目录（`src/`、`include/`、Makefile 可选）+ 各层终端记录 + 200 字以内小结：说说这个项目里哪一处让你对「本卷的知识点是一体的」体会最深。
