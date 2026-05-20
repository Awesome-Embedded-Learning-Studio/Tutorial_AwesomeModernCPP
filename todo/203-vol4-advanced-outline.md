---
id: "203"
title: "卷四：高级主题（C++20-26）— 全部章节大纲与文章规划"
category: content
priority: P1
status: pending
created: 2026-04-16
assignee: charliechen
depends_on: ["200", "201", "202"]
blocks: []
estimated_effort: epic
---

# 卷四：高级主题（C++20-26）— 全部章节大纲与文章规划

## 总览

- **卷名**：vol4-advanced
- **难度范围**：advanced
- **预计文章数**：50-60 篇
- **前置知识**：卷一 + 卷二 + 卷三
- **C++ 标准覆盖**：C++20, C++23, C++26
- **目录位置**：`documents/vol4-advanced/`

## 章节大纲

### ch00：Concepts 与约束

- **难度**：advanced
- **预计篇数**：5

#### 文章列表

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 00-01 | 01-concept-basics.md | concept 定义与使用 | concept 语法、requires 子句、requires 表达式 | 定义自定义 concept |
| 00-02 | 02-standard-concepts.md | 标准库 concepts | <concepts> 头文件、分类概念、比较概念 | 使用标准 concepts |
| 00-03 | 03-constraining-templates.md | 约束模板 | constrained auto、约束的偏序、重载决议 | 约束模板实战 |
| 00-04 | 04-concepts-vs-sfinae.md | Concepts vs SFINAE | 对比分析、迁移策略、错误信息质量 | 重构 SFINE 代码 |
| 00-05 | 05-concepts-practice.md | Concepts 实战 | 接口设计、算法约束、库设计中的 concepts | 设计 concept 层次 |

### ch01：Ranges 与视图

- **难度**：advanced
- **预计篇数**：5

#### 文章列表

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 01-01 | 01-ranges-basics.md | Ranges 基础 | range 概念、view 概念、惰性求值 | 基本管道操作 |
| 01-02 | 02-views.md | 视图(views) | take/drop/filter/transform/join/split 等 | 数据管道构建 |
| 01-03 | 03-range-adaptors.md | 范围适配器 | 管道操作符|、组合视图、性能分析 | 复杂管道设计 |
| 01-04 | 04-custom-views.md | 自定义 view | view_interface、sentinel、迭代器设计 | 实现自定义视图 |
| 01-05 | 05-ranges-practice.md | Ranges 实战 | 数据处理管道、ETL、算法组合 | 实用数据处理 |

### ch02：协程

- **难度**：advanced
- **预计篇数**：6

#### 文章列表

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 02-01 | 01-coroutine-mechanics.md | 协程机制 | 协程帧、promise_type、co_await/yield/return 机制 | 理解编译器变换 |
| 02-02 | 02-awaitable.md | Awaitable 与 Awaiter | operator co_await、Awaiter 接口、自定义等待 | 实现自定义 Awaiter |
| 02-03 | 03-promise-type.md | promise_type 设计 | get_return_object/initial_suspend/final_suspend | 设计返回类型 |
| 02-04 | 04-coroutine-scheduler.md | 协程调度器 | 调度器设计、任务队列、定时器集成 | 实现简单调度器 |
| 02-05 | 05-generator.md | std::generator (C++23) | 协程生成器、惰性序列、管道组合 | 实现数据管道 |
| 02-06 | 06-coroutine-practice.md | 协程实战 | 异步 I/O 集成、生产者-消费者模式、性能考量 | 完整协程应用 |

### ch03：模块

- **难度**：advanced
- **预计篇数**：3

#### 文章列表

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 03-01 | 01-module-basics.md | 模块基础 | module/import/export、模块分区、头文件单元 | 创建模块项目 |
| 03-02 | 02-module-build.md | 模块与构建系统 | BMI 文件、CMake 支持、编译器兼容性 | CMake 模块项目 |
| 03-03 | 03-module-migration.md | 从头文件迁移 | 迁移策略、混合使用、全局模块片段 | 渐进式迁移 |

### ch04：三路比较

- **难度**：intermediate → advanced
- **预计篇数**：2

#### 文章列表

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 04-01 | 01-spaceship-operator.md | <=> 运算符 | strong_ordering/weak_ordering/partial_ordering、默认比较 | 实现自定义比较 |
| 04-02 | 02-automatic-comparisons.md | 自动生成比较 | =default 实现、推导规则、与运算符重载配合 | 简化类设计 |

### ch05：模板元编程四卷系列

- **难度**：advanced
- **预计篇数**：12-16
- **核心**：按原 TODO 030 计划执行

#### 文章列表（概要，详见 TODO 030）

| 子卷 | 文件名 | 主题 | 预计篇数 | 对应 TODO 030 内容 |
|------|--------|------|---------|-------------------|
| Vol1 | vol1-cpp11-14-basics/ | C++11-14 模板基础 | 3-4 | 函数模板→类模板→特化→名称查找 |
| Vol2 | vol2-cpp17-modern/ | C++17 现代模板 | 3-4 | 变参模板→折叠→SFINAE→Concepts |
| Vol3 | vol3-cpp20-23-metaprog/ | C++20-23 元编程 | 3-4 | 类型萃取→CRTP→Policy→编译期数据结构 |
| Vol4 | vol4-generic-patterns/ | 泛型模式实战 | 3-4 | 硬件抽象→通信协议→状态机→代码膨胀控制 |

### ch06：编译期编程进阶

- **难度**：advanced
- **预计篇数**：3

#### 文章列表

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 06-01 | 01-if-constexpr.md | if constexpr 深入 | 编译期分支、SFINAE 替代、模板递归终止 | 编译期算法 |
| 06-02 | 02-compile-time-reflection.md | 编译期反射(C++26) | 静态反射提案、meta::info、枚举反射 | 元编程前沿 |
| 06-03 | 03-compile-time-patterns.md | 编译期设计模式 | 编译期字符串、编译期正则、X-macro 替代 | 编译期工具 |

### ch07：C++23 新特性

- **难度**：advanced
- **预计篇数**：5

#### 文章列表

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 07-01 | 01-expected.md | std::expected | 值或错误、monadic 操作、与异常对比 | 错误处理实战 |
| 07-02 | 02-print.md | std::print | 类型安全格式化输出、性能、unicode 支持 | 格式化输出 |
| 07-03 | 03-deducing-this.md | deducing this | 显式对象参数、递归 lambda、CRTP 简化 | 现代面向对象 |
| 07-04 | 04-generator-multidim.md | std::generator 与 mdspan | 协程生成器、多维 span | 数据处理 |
| 07-05 | 05-cpp23-misc.md | C++23 其他特性 | 显式 bool、constexpr 增强、flat containers、stacktrace | 综合练习 |

### ch08：C++26 预览

- **难度**：advanced
- **预计篇数**：3-4

#### 文章列表

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 08-01 | 01-static-reflection.md | 静态反射 | 反射提案、meta::info、编译期类型遍历 | 反射实验 |
| 08-02 | 02-contracts.md | 契约编程 | precondition/postcondition/assert、语义、与 assert 对比 | 契约注解 |
| 08-03 | 03-senders.md | 发送器(Senders) | std::execution 提案、结构化并发、异步算法 | 异步编程前沿 |
| 08-04 | 04-cpp26-misc.md | C++26 其他提案 | 模式匹配、 Hazard Pointer、线性代数库 | 前沿跟踪 |

### ch09：设计模式全面体系

- **难度**：intermediate → advanced
- **预计篇数**：10-12
- **子模块**：classic-gof/、cpp-idioms/、concurrent-patterns/

#### classic-gof/：GoF 23 模式的现代 C++ 实现

| 编号 | 文件名 | 标题 | 核心内容 |
|------|--------|------|---------|
| gof-01 | 01-creational.md | 创建型模式 | Singleton(thread-safe)、Factory(variant)、Builder(fluent)、Prototype(clone) |
| gof-02 | 02-structural.md | 结构型模式 | Adapter(lambda)、Bridge(Pimpl)、Composite(recursive)、Decorator(类型擦除) |
| gof-03 | 03-behavioral-1.md | 行为型模式(上) | Strategy(lambda)、Observer(signal-slot)、Command(function)、State(state-machine) |
| gof-04 | 04-behavioral-2.md | 行为型模式(下) | Iterator(coroutine)、Visitor(variant+visit)、Template Method(CRTP)、Memento |

#### cpp-idioms/：C++ 惯用法

| 编号 | 文件名 | 标题 | 核心内容 |
|------|--------|------|---------|
| id-01 | 01-raii.md | RAII 深入 | 资源管理、scope guard、事务性编程 |
| id-02 | 02-crtp.md | CRTP | 静态多态、代码注入、接口约束 |
| id-03 | 03-pimpl.md | Pimpl | 编译防火墙、ABI 稳定性、实现隐藏 |
| id-04 | 04-type-erasure.md | Type Erasure | value semantics + polymorphism、std::function 原理、any 原理 |
| id-05 | 05-sfinae-concepts.md | SFINAE 与 Concepts | 模板约束演进、检测惯用法 |
| id-06 | 06-nvi-policy.md | NVI 与 Policy-Based Design | 非虚接口、策略模式、编译期组合 |

#### concurrent-patterns/：并发设计模式

| 编号 | 文件名 | 标题 | 核心内容 |
|------|--------|------|---------|
| cp-01 | 01-thread-safe-interfaces.md | 线程安全接口 | mutex guard、lock-free queue、线程安全容器设计 |
| cp-02 | 02-active-object.md | Active Object | 异步方法调用、future 返回、与 Actor 对比 |
| cp-03 | 03-producer-consumer.md | 生产者-消费者 | blocking queue、条件变量、无锁队列 |

## 练习与项目

### 文章末尾练习
- 每篇 3-5 道，注重设计决策和代码实现
- 包含"用现代 C++ 重写经典实现"练习

### 实战项目
1. **Concept 约束的泛型库**：使用 Concepts 设计类型安全的数学运算库
2. **协程调度器**：实现支持定时器和 I/O 的完整调度器
3. **设计模式库**：实现可复用的 modern C++ 设计模式库

## 现有内容映射

| 现有文章 | 重写去向 | 备注 |
|----------|---------|------|
| core-embedded-cpp/ch12/* (9篇) | ch05 模板元编程 | 融入 4 卷系列 |
| cpp-features/coroutines/* (3篇) | ch02 协程 | 扩展为 6 篇 |
| cpp-features/msvc-cpp-modules.md | ch03 模块 | 通用化扩展 |
| todo/content/030-template-series.md | ch05 模板元编程 | 对齐，更新文件路径 |
| core-embedded-cpp/ch08/07-literal-operators.md | ch09 设计模式 | 融入惯用法 |
