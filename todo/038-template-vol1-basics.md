---
id: 038
title: "模板编程卷一：模板基础（C++11/14 核心机制）"
category: content
priority: P2
status: pending
created: 2026-05-22
assignee: charliechen
depends_on:
  - "030"
blocks: []
estimated_effort: epic
---

# 模板编程卷一：模板基础（C++11/14 核心机制）

## 目标
从零开始建立完整的模板基础知识体系。覆盖函数模板、类模板、特化与偏特化、非类型参数、名称查找、友元注入、别名、继承与 CRTP，最终通过综合项目 `fixed_vector<T, N>` 巩固全部知识点。

## 验收标准
- [ ] T1.0 模板导论完成：为什么需要模板、模板的学习路径
- [ ] T1.1 函数模板完成
- [ ] T1.2 类模板完成
- [ ] T1.3 模板特化与偏特化完成
- [ ] T1.4 非类型模板参数完成
- [ ] T1.5 模板参数依赖与名字查找完成
- [ ] T1.6 模板友元与 Barton-Nackman Trick 完成
- [ ] T1.7 模板别名与 Using 声明完成
- [ ] T1.8 模板与继承（CRTP）完成
- [ ] 卷一综合项目：`fixed_vector<T, N>` 完成
- [ ] 每节包含完整代码示例与 static_assert 验证
- [ ] 嵌入式贴士至少出现 3 处（代码膨胀、编译错误调试、寄存器封装）

## 实施说明

### T1.0 模板导论
- 什么是模板，为什么需要模板（泛型编程 vs 宏 vs 手写多版本）
- 模板的学习策略：编译器思维、错误信息解读、渐进式深入
- 本章知识地图与后续卷的关系

### T1.1 函数模板
- [ ] 模板参数推导规则（值类别、数组到指针衰减、函数到函数指针）
- [ ] 尾随返回类型（`auto foo(T t) -> decltype(...)`）与返回类型推导
- [ ] 模板重载与 specialization 的区别
- [ ] **实战：** 实现通用 `min/max/clamp` 函数族
- [ ] **嵌入式贴士：** 避免代码膨胀的技巧（共享非模板基类、显式实例化）

### T1.2 类模板
- [ ] 类模板声明与定义（类内 vs 类外定义成员函数）
- [ ] 模板参数的默认值
- [ ] 成员函数模板（与类模板参数独立）
- [ ] 虚函数与模板的限制（为何虚函数不能是模板）
- [ ] **实战：** 实现固定容量的 `ring_buffer<T, N>`
- [ ] **调试技巧：** 理解模板实例化错误信息（常见模式与快速定位方法）

### T1.3 模板特化与偏特化
- [ ] 全特化 vs 偏特化的语法与语义差异
- [ ] 类模板偏特化的匹配规则（部分匹配、递归偏特化）
- [ ] 函数模板的重载替代特化（为什么函数模板不推荐偏特化）
- [ ] **实战：** 为指针类型特化 `traits` 类
- [ ] **标准库溯源：** `std::iterator_traits` 的指针特化实现

### T1.4 非类型模板参数（NTTP）
- [ ] 整数、指针、引用类型的非类型参数
- [ ] `auto` 作为非类型模板参数类型（C++17）
- [ ] **实战：** 编译期数组大小参数化、位掩码生成器
- [ ] **嵌入式应用：** 寄存器地址的编译期封装（`Register<0x40010800>`）

### T1.5 模板参数依赖与名字查找
- [ ] 依赖名称（Dependent Names）与非依赖名称
- [ ] 两阶段查找（Two-Phase Lookup）的原理与实际行为
- [ ] `typename` 和 `template` 关键字消歧义的必要性
- [ ] ADL（Argument-Dependent Lookup）详解及其与模板的交互
- [ ] **实战：** 正确编写泛型迭代器代码（避免名字查找陷阱）
- [ ] **常见陷阱：** 为什么 `t.clear()` 有时不工作（基类模板中的名称查找）

### T1.6 模板友元与 Barton-Nackman Trick
- [ ] 友元注入机制（友元定义在类模板内部）
- [ ] Barton-Nackman 模式（CRTP 前身，通过友元注入运算符）
- [ ] 运算符重载的模板技巧（对称运算符、隐式转换）
- [ ] **实战：** 实现可比较的 `Point<T>` 类型（`==`、`<`、`<=` 等全套运算符）

### T1.7 模板别名与 Using 声明
- [ ] `typedef` vs `using` 的差异与偏好
- [ ] 别名模板（Alias Templates）的能力与限制
- [ ] **标准库溯源：** `std::conditional_t`、`std::enable_if_t` 的别名模板实现
- [ ] **实战：** 简化复杂模板类型的声明（嵌套模板、函数指针类型）

### T1.8 模板与继承
- [ ] CRTP（Curiously Recurring Template Pattern）详解
- [ ] 静态多态 vs 动态多态的性能对比与适用场景
- [ ] 混入（Mixin）模式：通过模板继承注入功能
- [ ] **实战：** 使用 CRTP 实现单例基类、计数器基类
- [ ] **性能分析：** CRTP vs 虚函数的汇编对比（Godbolt 验证零开销）

### 卷一综合项目：`fixed_vector<T, N>`
- [ ] 完整的迭代器支持（含 `const_iterator`、`reverse_iterator`）
- [ ] 与 `std::vector` 兼容的接口（`push_back`、`pop_back`、`size`、`capacity`）
- [ ] 编译期边界检查（可选，通过模板参数控制）
- [ ] 综合运用：类模板、非类型参数、特化、CRTP、友元

## 涉及文件
- documents/vol4-advanced/vol1-basics-cpp11-14/index.md
- documents/vol4-advanced/vol1-basics-cpp11-14/01-template-introduction.md
- documents/vol4-advanced/vol1-basics-cpp11-14/02-function-templates.md
- documents/vol4-advanced/vol1-basics-cpp11-14/03-class-templates.md
- documents/vol4-advanced/vol1-basics-cpp11-14/04-specialization.md
- documents/vol4-advanced/vol1-basics-cpp11-14/05-nttp.md
- documents/vol4-advanced/vol1-basics-cpp11-14/06-name-lookup.md
- documents/vol4-advanced/vol1-basics-cpp11-14/07-friends-barton-nackman.md
- documents/vol4-advanced/vol1-basics-cpp11-14/08-aliases.md
- documents/vol4-advanced/vol1-basics-cpp11-14/09-inheritance-crtp.md
- documents/vol4-advanced/vol1-basics-cpp11-14/10-project-fixed-vector.md

## 参考资料
- 《C++ Templates: The Complete Guide》(2nd Edition) Part I & II
- cppreference.com Templates 章节
- Godbolt (godbolt.org) 编译器浏览器
