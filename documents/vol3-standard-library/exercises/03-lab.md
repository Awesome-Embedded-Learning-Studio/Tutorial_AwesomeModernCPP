---
title: "卷 3 Lab：标准库解剖台"
description: "标准库深入卷的动手实验：把一条成绩日志从字节流一路加工成报表——六个步骤依次解剖容器大小与扩容、迭代器失效、charconv 解析、string 的 SSO 与悬垂、文件系统遍历、expected 错误链，最后附一道二分答案求第 k 小距离对的 L5 挑战。"
chapter: 3
order: 3
tags:
  - host
  - intermediate
  - cpp-modern
  - 实战
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17, 20, 23]
reading_time_minutes: 7
prerequisites:
  - "卷 3 全部章节（容器、迭代器与算法、字符串、I/O、时间与数值、错误处理）"
related:
  - "卷 3 课后练习（Homework）"
  - "卷 3 Project：CSV 报表流水线"
---

# 卷 3 Lab：标准库解剖台

## 实验目标

本卷的知识点像一台精密仪器：容器、迭代器、算法、字符串、I/O、时间、错误处理，每一件都值得单独解剖。这个 Lab 把它们拧成一条流水线——**把一条成绩日志从字节流一路加工成报表**：先用 `sizeof` 摸清容器的骨架，再亲手验证迭代器失效规矩，然后用 `charconv` 把文本解析成数，接着剖开 `string` 的 SSO 与悬垂，再让文件系统替你把数据搬出来，最后用 `expected` 把错误管起来。做完你会对「标准库每个组件为什么这么设计」有肌肉记忆。

所有实验在 `/tmp` 下独立目录做（每次登录 WSL 后 `/tmp` 会清空，重新做一遍也无妨）。每步有验收标准；卡住先回题面每步标注的章节链接读教材，再不行看[实验参考](04-lab-solutions.md)。

## 步骤 1：容器侦察 {#lab-1}

难度 **L1** · 涉及[array：编译期固定大小的聚合容器](../containers/02-array.md)、[vector 深入：三指针、扩容与迭代器失效](../containers/03-vector-deep-dive.md)、[string 深入：SSO、COW 与 resize_and_overwrite](../containers/04-string-memory-deep-dive.md)、[span：非拥有的连续视图](../containers/08-span.md)

**目标**：把本机主流容器的体积和扩容节奏一次摸清。

1. 写程序打印 `array<int,4>`、`vector<int>`、`list<int>`、`string`、`span<int>`、`span<int,4>` 的 `sizeof`。
2. 空 `vector<int>` 连续 push 16 个元素，capacity 变化时打印一行；空 `std::string` 连续 `push_back('x')` 40 个字符，capacity 变化时打印一行。

**验收标准**：贴输出；一句话说清「vector 的 24 字节」和「string 容量一开始就是 15、第 16 个字符才出堆」分别是什么机制。

[实验参考 →](04-lab-solutions.md#lab-1)

## 步骤 2：迭代器失效对照表 {#lab-2}

难度 **L2** · 涉及[vector 深入：三指针、扩容与迭代器失效](../containers/03-vector-deep-dive.md)、[map 与 set 深入：红黑树、异构查找与节点句柄](../containers/06-map-set-deep-dive.md)、[unordered_map 与 unordered_set 深入：哈希表、桶与自定义 hash](../containers/07-unordered-map-set-deep-dive.md)、[容器选择指南：按操作、内存与失效规则挑对容器](../containers/01-container-selection-guide.md)

**目标**：亲手验证四种「改容器后旧句柄还能不能用」的情形。

1. `vector` 不 reserve：拿 `&v[1]` 后狂 push 触发扩容，旧指针还等于 `&v[1]` 吗？
2. `vector` reserve(100)：同样操作，旧指针呢？
3. `map`：拿 key=1 的引用后插 1000 个新元素，引用还是 `alpha` 吗？
4. `unordered_map`：拿引用后 `reserve(1000)` 再插 200 个元素（必然 rehash），引用还在吗？

**验收标准**：贴出四个 `0/1/alpha`；对照教材失效表说清每行属于哪条规则。

[实验参考 →](04-lab-solutions.md#lab-2)

## 步骤 3：文本解析流水线 {#lab-3}

难度 **L3** · 涉及[string_view：非拥有的只读字符串视图](../strings/50-string-view.md)、[charconv：零开销的数字与字符串互转](../strings/51-charconv.md)、[算法总览（下）：排序、分区与堆](../iterators-algorithms/43-algorithm-overview-part2.md)、[numeric：累加、填充、内积与相邻差](../iterators-algorithms/44-numeric-algorithms.md)、[format：C++20 的类型安全格式化](../strings/52-format.md)

**目标**：把 6 行「名字 分数」日志（其中一行是坏行 `bad_line`）解析成结构体，输出四项统计。

1. `string_view::find(' ')` 切两段；`from_chars` 解析分数，**ec 和 ptr 都检查**；坏行计数跳过。
2. 平均分：`accumulate` 初始值用 $0.0$（`-std=c++23` 编译）。
3. 中位数：`nth_element`；前 2 名：`partial_sort` 降序。
4. 全表：`ranges::sort` 按名字投影排序，`std::format` 对齐输出。

**验收标准**：贴输出（应有「解析成功 5 行, 失败 1 行」、平均 88.10、中位数 90.5、前 2 名 dave 与 carol——注意 partial_sort 不保证并列者顺序）。

[实验参考 →](04-lab-solutions.md#lab-3)

## 步骤 4：string 内存解剖 {#lab-4}

难度 **L3** · 涉及[string 深入：SSO、COW 与 resize_and_overwrite](../containers/04-string-memory-deep-dive.md)、[string_view：非拥有的只读字符串视图](../strings/50-string-view.md)

**目标**：测出 SSO 阈值、用 `resize_and_overwrite` 接 C 风格 API、亲眼抓住悬垂 view。

1. 写 `bool inside(const std::string&)` 判断 `data()` 是否落在对象内，从长度 0 扫到 40，打印「首次出堆」的长度——SSO 阈值就是它减 1。
2. 模拟一个 C API `fake_read(char* buf, size_t n)`（往缓冲写 `"hello"`、返回实际字节数），分别用「`resize(64)` + 截回」老写法和 C++23 `resize_and_overwrite` 接它，对比结果。
3. 写 `std::string_view bad_return()` 返回函数内局部 `string` 的视图，普通构建跑一次；再用 `-fsanitize=address` 跑一次。

**验收标准**：贴 SSO 阈值、两个写法输出、以及 ASan 报告的关键行（`stack-use-after-return`）。

[实验参考 →](04-lab-solutions.md#lab-4)

## 步骤 5：文件系统报表与截断检测 {#lab-5}

难度 **L4** · 涉及[filesystem：C++17 跨平台文件系统操作](../io/57-filesystem.md)、[format：C++20 的类型安全格式化](../strings/52-format.md)、[error_code：错误码体系与自定义 category](../error-utils/66-error-code.md)

**目标**：遍历目录出报表，体验「entry 缓存」与「format_to_n 截断检测」，并把错误双路径走一遍。

1. 造目录树（两层子目录 + 3 个文件），`recursive_directory_iterator` 用 **entry 成员**统计文件数与总字节。
2. 对每个文件拼一行 `"{:<16} {:>6}"` 报表，但用 `format_to_n` 写进 8 字节定长缓冲：打印缓冲内容、`res.size`（完整长度）与「是否截断」。
3. 对不存在的路径调 `file_size`，分别走异常版（贴 `what()`）与 `error_code` 版（贴 `value`/`message`）。

**验收标准**：贴输出；说清「完整长度 23」是怎么算出来的，以及为什么遍历时要用 entry 成员。

[实验参考 →](04-lab-solutions.md#lab-5)

## 步骤 6：expected + error_code 错误链 {#lab-6}

难度 **L4** · 涉及[expected：值或错误，C++23 的错误处理新范式](../error-utils/64-expected.md)、[error_code：错误码体系与自定义 category](../error-utils/66-error-code.md)、[charconv：零开销的数字与字符串互转](../strings/51-charconv.md)

**目标**：从零搭一套自定义错误码体系，再用 `expected` 把解析链串起来。

1. 按四步搭 `MyErrc` 体系：定义枚举（0 留成功）→ 特化 `std::is_error_code_enum<MyErrc>` → 写 category 单例（`name`/`message`/`default_error_condition`，把 `kTimeout` 映射到 `std::errc::timed_out`）→ 写 `make_error_code(MyErrc)`。
2. 验证跨体系比较：`error_code{MyErrc::kTimeout, my_category()} == std::errc::timed_out` 应为 1。
3. 写 `expected<int, error_code> parse_value(string_view line)`：`"key=value"` 格式检查、`from_chars` 解析（失败给标准 `errc::invalid_argument`）、值域检查（>1000 给 `MyErrc::kTooBig`）。
4. 对 6 行输入（含 3 个坏行）跑 `parse_value(...).transform(翻倍).and_then(...)` 链，逐行打印失败原因与 category，汇总成功/失败数与总和。

**验收标准**：贴输出（3 条失败信息分别带 `generic` 与 `my-app` 两个 category，总和 $596$）。

[实验参考 →](04-lab-solutions.md#lab-6)

## 附加挑战（L5）：第 k 小距离对 {#lab-l5}

**目标**：**只用本卷的知识**（`std::sort`、`upper_bound`、`steady_clock`）解决「数组里第 k 小的元素对距离」——改编自 **LeetCode 719 K-th Smallest Pair Distance**（本卷 L5＝「用本卷知识可解的最难问题」，档位口径见[练习总览](index.md)）。n² 对距离不能枚举，正解是「排序 + 二分答案」：猜一个距离 d，数出「距离 ≤ d 的对数」（`count_pairs` 用 `upper_bound` 对每个 i 二分找 `a[i]+d` 的位置，O(n log n)），对数 ≥ k 就往左收、否则往右放，二分到底就是答案。

1. 写 `long long count_pairs(const vector<long long>&, long long d)` 与 `long long kth_smallest_distance(vector<long long> a, long long k)`。
2. **先验后跑**：n=200 随机数组（`mt19937(42)`，值域 0~100000），k=12345，把你算法的结果和暴力枚举全部 pair 距离排序后的结果对照，打印「一致? true」。
3. 大样本：n=10000、k=3000000，`steady_clock` 计时，贴结果与耗时。

**验收标准**：贴出小样本对照（一致 true）与大样本结果；说清为什么 `count_pairs` 用 `upper_bound` 而不是 `lower_bound`（≤ d 与 < d 的边界差在哪）。

[实验参考 →](04-lab-solutions.md#lab-l5)

## 提交物清单

一个目录装下全部源码、每步终端记录（`stepN.log`）、以及 200 字以内的小结——用你自己的话说清「标准库的每一个抽象都在为某个具体的坑兜底」这件事你在哪一步看得最真切。
