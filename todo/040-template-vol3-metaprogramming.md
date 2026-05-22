---
id: 040
title: "模板编程卷三：元编程精要（C++20/23 约束与元编程）"
category: content
priority: P2
status: pending
created: 2026-05-22
assignee: charliechen
depends_on:
  - "039"
blocks: []
estimated_effort: epic
---

# 模板编程卷三：元编程精要（C++20/23 约束与元编程）

## 目标
掌握现代 C++ 的约束机制和高级元编程技术。覆盖 Concepts、requires 表达式、TMP 核心技巧、编译期字符串、反射基础、实例化控制、异常安全，最终通过综合项目实现 mini-STL 算法库。

## 验收标准
- [ ] T3.1 Concepts 详解完成
- [ ] T3.2 使用 Concepts 约束模板完成
- [ ] T3.3 Requires 表达式深度解析完成
- [ ] T3.4 TMP 核心技巧完成
- [ ] T3.5 编译期字符串处理完成
- [ ] T3.6 反射元编程基础完成
- [ ] T3.7 模板实例化控制完成
- [ ] T3.8 模板与异常安全完成
- [ ] 卷三综合项目：mini-STL 算法库完成
- [ ] 所有算法使用 Concepts 约束而非 SFINAE
- [ ] 嵌入式贴士至少 3 处（代码膨胀控制、编译期优化、ISR 友好模式）

## 实施说明

### T3.1 概念（Concepts）详解
- [ ] `concept` 声明与定义语法
- [ ] `requires` 表达式语法（四种成分：简单要求、类型要求、复合要求、嵌套要求）
- [ ] `requires` 子句（约束模板、约束 auto）
- [ ] **标准库概览：** `std::integral`、`std::floating_point`、`std::invocable`、`std::ranges::range`、`std::sortable` 等核心概念
- [ ] **实战：** 定义 `Numeric`（支持算术运算）、`Addable`（支持 `+`）、`Hashable`（支持 `std::hash`）概念

### T3.2 使用 Concepts 约束模板
- [ ] concept 作为模板参数约束（`template<std::integral T>`）
- [ ] 缩写函数模板（Abbreviated Function Templates：`void foo(std::integral auto x)`）
- [ ] concept 重载与约束偏序（更严格的约束优先匹配）
- [ ] **vs SFINAE：** 为什么 Concepts 是更好的选择（可读性、错误信息、编译速度）
- [ ] **实战：** 重写 `std::sort` / `std::find` 算法使用 Concepts 约束
- [ ] **嵌入式贴士：** 更清晰的编译错误信息（减少模板错误信息迷宫）

### T3.3 Requires 表达式深度解析
- [ ] 简单要求（Simple Requirement）：表达式合法性检查
- [ ] 类型要求（Type Requirement）：`typename` 合法性检查
- [ ] 复合要求（Compound Requirement）：`{expr} -> concept` 语法与 `noexcept` 检查
- [ ] 嵌套要求（Nested Requirement）：`requires concept_name` 子句
- [ ] **实战：** 定义复杂概念（如 `RandomAccessIterator`、`SortableContainer`）
- [ ] **标准库溯源：** `std::ranges::range` 和 `std::ranges::random_access_range` 概念定义剖析

### T3.4 模板元编程（TMP）核心技巧
- [ ] 类型列表（Type List）操作：`push_front`、`push_back`、`at`、`remove`、`unique`
- [ ] 编译期映射与查找（`type_map`、`type_set`）
- [ ] 编译期算法：排序（按大小/对齐）、搜索（按条件）
- [ ] **传统 TMP vs constexpr：** 迁移指南（何时用模板递归、何时用 constexpr 函数）
- [ ] **实战：** 实现 `type_list<Ts...>` 和对应的编译期算法（`for_each`、`transform`、`filter`）

### T3.5 编译期字符串处理
- [ ] 字符串作为非类型模板参数（C++20 `fixed_string` 惯用法）
- [ ] 编译期字符串操作：拼接、截取、比较、查找
- [ ] **实战：** 实现编译期正则表达式匹配（简化版，支持 `*` 和 `?` 通配符）
- [ ] **嵌入式应用：** 协议解析的编译期优化（AT 命令匹配、寄存器名称映射）

### T3.6 反射元编程基础
- [ ] `std::is_aggregate`、`std::is_layout_compatible` 的使用
- [ ] 结构化绑定与聚合类型的关系
- [ ] **实战：** 实现 `for_each_member` 遍历结构体成员（基于结构化绑定 + 聚合检测）
- [ ] **前瞻：** C++26 静态反射（Reflection）提案简介（`^T`、`std::meta::info`）

### T3.7 模板实例化控制
- [ ] 显式实例化声明（`extern template`）与定义（`template class`）
- [ ] `extern template` 减少编译时间的原理与最佳实践
- [ ] 实例化点（Point of Instantiation, POI）详解
- [ ] **嵌入式关键：** 控制代码膨胀的实用技巧（共享非模板基类、显式实例化常用类型、Pimpl + 模板）
- [ ] **实战：** 为 `ring_buffer<uint8_t, 64>` 等常用类型显式实例化，对比编译产物大小

### T3.8 模板与异常安全
- [ ] 强异常保证（Strong Exception Guarantee）在模板中的重要性
- [ ] `noexcept` 在模板中的应用（`noexcept(noexcept(expr))` 模式）
- [ ] 条件 `noexcept`（C++17）：基于类型特征的 noexcept 规范
- [ ] `std::is_nothrow_constructible`、`std::is_nothrow_assignable` 等类型检查
- [ ] **实战：** 实现异常安全的泛型容器（`swap` 保证不抛出、移动优于拷贝的策略）

### 卷三综合项目：mini-STL 算法库
- [ ] 使用 Concepts 约束所有算法参数
- [ ] 实现 `sort`（快速排序 + 插入排序混合）、`find`（线性搜索）、`transform`（元素映射）
- [ ] 实现 `accumulate`、`count_if`、`remove_if`（稳定删除）
- [ ] 与 `std::algorithm` 的性能对比（编译时间 + 运行时间）
- [ ] 嵌入式裁剪版本：无异常、无 RTTI、固定大小容器优先

## 涉及文件
- documents/vol4-advanced/ch05-template-metaprogramming/vol3-metaprogramming/index.md
- documents/vol4-advanced/ch05-template-metaprogramming/vol3-metaprogramming/01-concepts.md
- documents/vol4-advanced/ch05-template-metaprogramming/vol3-metaprogramming/02-constraining-templates.md
- documents/vol4-advanced/ch05-template-metaprogramming/vol3-metaprogramming/03-requires-expressions.md
- documents/vol4-advanced/ch05-template-metaprogramming/vol3-metaprogramming/04-tmp-techniques.md
- documents/vol4-advanced/ch05-template-metaprogramming/vol3-metaprogramming/05-compile-time-strings.md
- documents/vol4-advanced/ch05-template-metaprogramming/vol3-metaprogramming/06-reflection-basics.md
- documents/vol4-advanced/ch05-template-metaprogramming/vol3-metaprogramming/07-instantiation-control.md
- documents/vol4-advanced/ch05-template-metaprogramming/vol3-metaprogramming/08-exception-safety.md
- documents/vol4-advanced/ch05-template-metaprogramming/vol3-metaprogramming/09-project-mini-stl.md

## 参考资料
- 《C++ Templates: The Complete Guide》(2nd Edition) Part V
- 《C++ Concurrency in Action》(2nd Edition) — 异常安全章节
- cppreference.com Concepts / Ranges 章节
- C++26 反射提案（P2996）
