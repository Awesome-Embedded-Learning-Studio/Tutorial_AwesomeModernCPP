---
title: "卷 2 Lab：零拷贝配置读取器"
description: "现代特性卷的动手实验：把值类别、移动语义、string_view、variant、自制 expected 串成一条装配线——五个步骤从 decltype 体检做到错误传播链，最后附一道零分配 INI 读取器的 L5 挑战。每步有目标、步骤与验收标准，实验参考独立成文件。"
chapter: 2
order: 3
tags: [host, intermediate, cpp-modern, 移动语义, 类型安全]
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17]
reading_time_minutes: 6
prerequisites:
  - "卷 2 全部章节（第 0~11 章）"
related:
  - "卷 2 Homework"
  - "卷 2 Project：logscan 日志巡检器"
---

# 卷 2 Lab：零拷贝配置读取器

## 实验目标

本卷的知识点像一条装配线：值类别、移动语义、`string_view`、`variant`、错误传播，单独看每样都懂，串起来才是一个真正能用的「零分配配置读取器」。这个 Lab 分五步装配，每步只引入一个新组件，最后一步是挑战——把前五步的零件组装成一台读文件、报行号的 INI 读取器。做完你会对「现代 C++ 的组合拳」有肌肉记忆：视图不拥有数据、variant 管类型、expected 管错误、RAII 管资源。

所有实验在 `/tmp` 下独立目录做，编译基线 `g++ -std=c++17 -Wall -Wextra`。每步有验收标准；卡住先回题面每步标注的章节链接读教材，再不行看[实验参考](./04-lab-solutions.md)。

## 步骤 1：值类别体检台 {#lab-1}

难度 **L1** · 涉及[右值引用：从拷贝到移动](../ch00-move-semantics/01-rvalue-reference.md)

**目标**：把「有名字的就是左值」「std::move 的产物是 xvalue」这些规则变成一张自己跑出来的体检表。

1. 写 `category<T>()` 模板（`is_lvalue_reference` / `is_rvalue_reference` 三分支）+ `PROBE` 宏，对 9 个表达式打印值类别：局部变量、命名右值引用、返回引用的函数调用、返回值的函数调用、`std::move(x)`、赋值表达式 `x = 5`、条件表达式 `x > 0 ? x : g`、字符串字面量、`"hello"[0]`。
2. 配 5 条 `static_assert` 钉死关键结论（局部变量左值、`move` 产物 xvalue、值返回 prvalue、引用返回左值、赋值表达式左值）。

**验收标准**：贴出输出；一句话说清「命名右值引用是左值」为什么是完美转发存在的理由。

[实验参考 →](./04-lab-solutions.md#lab-1)

## 步骤 2：noexcept 与扩容策略 {#lab-2}

难度 **L2** · 涉及[移动构造与移动赋值](../ch00-move-semantics/02-move-semantics.md)、[移动语义实战：从 STL 到自定义类型](../ch00-move-semantics/05-move-in-practice.md)

**目标**：亲手复现「noexcept 移动 → vector 扩容用移动；非 noexcept → 退回拷贝」。

1. 写 `template <bool NoexceptMove> class TrackedBuffer`：拷贝构造与移动构造（`noexcept(NoexceptMove)`）都打日志计数，赋值删掉，静态计数器带 `reset()`。
2. 用 `NB = TrackedBuffer<true>` 和 `TB = TrackedBuffer<false>` 各做一轮：`reserve(1)` → `emplace_back` 占满 → 再 `emplace_back` 触发扩容，打印两轮的 `copy/move` 计数。

**验收标准**：贴出两轮输出（一轮只有 move、一轮只有 copy）；说清 `vector` 为什么「不敢」用可能抛异常的移动（异常安全的角度）。

[实验参考 →](./04-lab-solutions.md#lab-2)

## 步骤 3：零拷贝 KV 解析核心 {#lab-3}

难度 **L3** · 涉及[string_view 内部原理：非拥有字符串视图](../ch08-string-view/01-string-view-internals.md)、[std::optional：优雅表达「可能没有值」](../ch04-type-safety/04-optional.md)

**目标**：装配读取器的引擎——`parse_kv`，全程零堆分配。

1. 写 `parse_kv(string_view)`：找 `=`、拆键值、四个 while 做两侧 trim，空 key 返回 `nullopt`，返回 `optional<pair<string_view, string_view>>`。
2. 主循环消费 `"name = Alice ; age = 30 ; city = Beijing"`：按 `;` 分段、逐段解析打印 `key=[...] value=[...]`，用 `remove_prefix` 推进视图。

**验收标准**：贴出三段输出；说出这一整个解析过程发生过几次堆分配（除了 ostream 内部），以及为什么 `std::string` 版同样的逻辑每次 `substr` 都要分配。

[实验参考 →](./04-lab-solutions.md#lab-3)

## 步骤 4：类型安全的配置值 {#lab-4}

难度 **L3** · 涉及[std::variant：类型安全的联合体](../ch04-type-safety/03-variant.md)、[结构化绑定：一行解包多个值](../ch05-structured-bindings/01-structured-bindings.md)

**目标**：给步骤 3 的值装上类型——int、double、bool、字符串四选一，用 variant 表达。

1. 定义 `ConfigValue = variant<int, double, string_view, bool>`，写 `parse_value`：先精确匹配 `true/false`，再 `from_chars` 试 int、试 double，都不行落 `string_view`。
2. 四个条目（`port=8080`、`debug=true`、`ratio=0.75`、`host=example.com`）逐个打印 `index()`、`holds_alternative<string_view>`、`get_if<int>` 的结果，再用 `Overloaded` 访问者打印最终归类。

**验收标准**：贴出四个条目的全部输出；说清 `index()`、`get_if`、`visit` 三者的分工和各自适合什么场景。

[实验参考 →](./04-lab-solutions.md#lab-4)

## 步骤 5：错误传播链 {#lab-5}

难度 **L4** · 涉及[std::expected\<T, E>：类型安全的错误传播](../ch10-error-handling/03-expected-error.md)、[错误处理模式总结：选择指南与最佳实践](../ch10-error-handling/04-error-patterns.md)、[标准属性详解：让编译器成为你的代码审查员](../ch07-attributes/01-standard-attributes.md)

**目标**：给读取器装上报错系统——自制 C++17 简化版 `expected` + 枚举错误类型 + `[[nodiscard]]`。

1. 实现 `unexpected<E>` 与 `expected<T, E>`（构造/拷贝/析构 + `has_value`/`operator bool`/`operator*`/`error` + `and_then`）。注意 union 里放非平凡成员的编译坑：给 union 起名并补空构造/析构。
2. 错误类型用 `enum class ParseError` 并标 `[[nodiscard]]`（位置放对，`-Wall -Wextra` 零警告）；`parse_port` 与 `find_field` 两个可能失败的操作返回 `expected`。
3. 用 `and_then` 串成「找字段 → 解析端口」的链，对四个输入跑一遍：`"host=localhost;port=8080"`（成功）、缺字段、端口非数字、端口越界。

**验收标准**：贴出四条链的结果（一条成功三条失败且原因各异）；说清 `and_then` 里错误是怎么「穿透」到链尾的。

[实验参考 →](./04-lab-solutions.md#lab-5)

## 附加挑战（L5）：组装零分配 INI 读取器 {#lab-l5}

**目标**：把前五步的零件装成一台能读真文件的机器——RAII 文件句柄 + 整文件一次读入 + 逐行零拷贝解析 + 带行号的错误报告。本题口径：难度按「用本卷知识可解的最难问题」标定 L5（本卷 L5 口径见[练习总览](./index.md)）。

1. `unique_ptr<FILE, FcloseDeleter>` 管文件（ch01）；一次 `fread` 把整份文件读进一个 `std::string` 缓冲——**这是整个程序唯一的一次堆分配**，之后全部在 `string_view` 上零拷贝解析。
2. 逐行处理：去 `\r`、截掉 `#`/`;` 注释、trim 空白；空行与纯注释行跳过。
3. 每行走「`parse_kv` → `parse_value`」的 `expected` 链；错误类型 `LineError{行号, 消息}`，缺 `=`、空 key、空值都要报出行号和该行原文。
4. 值分类成 `variant<int, double, bool, string_view>`，`Overloaded` 访问者打印「键 = 值 (类型)」。
5. 自备一份含注释、合法行、两种错误行的 `sample.ini`，普通构建跑一遍；ASan/UBSan 构建（`-fsanitize=address,undefined`）再跑一遍，要求零报告。

**验收标准**：贴出 `sample.ini` 的内容、两次运行输出（错误行必须带行号和原文）；说清「唯一一次堆分配」发生在哪一行代码、为什么解析路径可以做到零分配。

[实验参考 →](./04-lab-solutions.md#lab-l5)

## 提交物清单

一个目录装下全部源码（`lab1.cpp` ~ `lab5.cpp`、`lab5l.cpp`、`sample.ini`）、每步终端记录，以及 200 字以内的小结——用你自己的话说清「view 不拥有、variant 管类型、expected 管错误」这三句话分别在你哪一步体会得最深。
