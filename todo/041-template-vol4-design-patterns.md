---
id: 041
title: "模板编程卷四：泛型设计模式实战（架构级应用）"
category: content
priority: P2
status: pending
created: 2026-05-22
assignee: charliechen
depends_on:
  - "040"
blocks: []
estimated_effort: epic
---

# 模板编程卷四：泛型设计模式实战（架构级应用）

## 目标
将模板技术应用于实际架构设计，掌握业界验证的设计模式和架构范式。覆盖 Policy-Based Design、Type Erasure、NVI、工厂/访问者/单例/观察者模式的模板实现、Mixin 组合、Tag Dispatching、DSL 构建，最终通过综合项目实现完整的嵌入式事件系统。

## 验收标准
- [ ] T4.1 Policy-Based Design 完成
- [ ] T4.2 类型擦除（Type Erasure）完成
- [ ] T4.3 模板方法模式与 NVI 完成
- [ ] T4.4 工厂模式模板实现完成
- [ ] T4.5 访问者模式模板实现完成
- [ ] T4.6 单例模式线程安全实现完成
- [ ] T4.7 观察者模式模板实现完成
- [ ] T4.8 Mixin 与组合式设计完成
- [ ] T4.9 Tag Dispatching 与类型分派完成
- [ ] T4.10 模板与 DSL 完成
- [ ] 卷四综合项目：嵌入式事件系统完成
- [ ] 每个模式提供虚函数 vs 模板的性能对比
- [ ] 嵌入式应用案例至少 5 处

## 实施说明

### T4.1 Policy-Based Design（策略设计）
- [ ] Policy Class 的定义与设计原则（正交性、可组合性）
- [ ] vs 传统策略模式（运行时多态）的优劣
- [ ] **经典案例：** `std::allocator` 作为 policy 的设计分析
- [ ] **实战：** 设计可配置的智能指针（删除策略：数组 vs 单对象、所有权策略：独占 vs 共享）
- [ ] **嵌入式应用：** 可插拔的内存管理策略（静态池 / arena / 栈分配器作为策略注入）

### T4.2 类型擦除（Type Erasure）
- [ ] 类型擦除原理：隐藏具体类型，保留统一接口
- [ ] Small Buffer Optimization（SBO）策略与实现
- [ ] **标准库深潜：** `std::function` 完整实现剖析（SBO + 虚函数表 + 类型擦除）
- [ ] **标准库深潜：** `std::any` 实现剖析（RTTI 的使用与 `any_cast` 的安全性）
- [ ] **实战：** 实现 `function<R(Args...)>`（支持函数指针、lambda、仿函数、成员函数）
- [ ] **性能权衡：** 虚函数 vs 类型擦除 vs 纯模板的性能对比（调用开销 + 二进制大小）

### T4.3 模板方法模式与 NVI
- [ ] 非虚拟接口（NVI）模式：public 非虚函数调用 private 虚函数
- [ ] 编译期模板方法：CRTP 替代虚函数的"模板方法"
- [ ] **实战：** CRTP 实现的算法框架（基类定义骨架，派生类提供策略钩子）
- [ ] **性能分析：** vs 虚函数调用的汇编对比（Godbolt 验证零开销）

### T4.4 工厂模式的模板实现
- [ ] 抽象工厂的编译期版本（基于 typelist 的工厂）
- [ ] 对象构造的泛型解决方案（`make_unique` / `make_shared` 的通用化）
- [ ] **实战：** 实现 `generic_factory<T>`（支持注册创建函数、按 key 查找创建）
- [ ] **嵌入式应用：** 驱动程序的编译期注册（基于模板 + constexpr 注册表）

### T4.5 访问者模式的模板实现
- [ ] 传统访问者模式的局限（双重分发的复杂性、扩展困难）
- [ ] `std::variant` + `std::visit` 的现代方案（编译期 visitor）
- [ ] 泛型 lambda 作为 visitor（`overloaded` 工具实现）
- [ ] **实战：** 实现编译期访问者模式（基于 variant 的表达式树求值器）
- [ ] **嵌入式应用：** 命令模式与事件分发（variant-based 的命令处理框架）

### T4.6 单例模式的线程安全实现
- [ ] Meyer's Singleton（C++11 保证线程安全的局部静态变量）
- [ ] 模板单例基类（CRTP 实现：`class T : public singleton<T>`）
- [ ] **实战：** `singleton<T>` 的线程安全实现（含禁用拷贝/移动、显式 delete）
- [ ] **注意：** 静态初始化顺序问题（Static Initialization Order Fiasco）与解决策略
- [ ] **嵌入式注意：** 全局构造函数的开销与 `constinit` 的替代

### T4.7 观察者模式的模板实现
- [ ] 编译期类型安全的信号槽（无 Qt 依赖）
- [ ] **实战：** 实现 `signal<R(Args...)>` 和 `slot`（支持连接、断开、emit）
- [ ] **嵌入式应用：** ISR 到任务的事件分发（信号槽作为中断安全的观察者机制）
- [ ] **性能考量：** 信号槽 vs 裸回调 vs 虚函数的开销对比

### T4.8 混入（Mixin）与组合式设计
- [ ] CRTP 作为 Mixin 机制（向基类注入行为）
- [ ] 参数化继承（多重继承的线性化）
- [ ] **实战：** 构建可组合的组件（日志 Mixin + 计数 Mixin + 锁 Mixin 的自由组合）
- [ ] **设计原则：** Mixin vs 组合的选择指南（何时用继承混入、何时用成员组合）

### T4.9 Tag Dispatching 与类型分派
- [ ] Iterator Tags 详解（`input_iterator_tag` → `forward_iterator_tag` → ... → `random_access_iterator_tag`）
- [ ] 编译期算法选择（基于 tag 选择最优实现）
- [ ] **标准库溯源：** `std::advance` 的 tag dispatching 实现剖析
- [ ] **实战：** 实现优化的算法选择器（串行 vs 并行、内存映射 vs IO 端口）
- [ ] Tag Dispatching vs Concepts vs `if constexpr` 的选择指南

### T4.10 模板与 DSL（领域特定语言）
- [ ] 内嵌 DSL 的设计原则（流畅接口、运算符重载、表达式模板）
- [ ] 运算符重载在 DSL 中的应用（`+`、`|`、`>>` 等运算符的语义重载）
- [ ] **实战：** 实现类型安全的单位系统（`meter` / `second` / `meter_per_second`，编译期维度检查）
- [ ] **实战：** 实现状态机编译期 DSL（`state_machine` + `transition_table` + `guard/action`）

### 卷四综合项目：嵌入式事件系统
- [ ] 编译期类型安全的事件分发（基于 variant + visitor）
- [ ] 支持 ISR-safe 的队列（lock-free SPSC 环形缓冲区）
- [ ] Policy-Based 的内存管理策略（静态池 / arena 可配置）
- [ ] 综合运用：CRTP（事件处理器基类）、Type Erasure（事件存储）、Policy Design（内存策略）
- [ ] 性能验证：中断延迟测量 + 内存占用分析

## 涉及文件
- documents/vol4-advanced/ch05-template-metaprogramming/vol4-design-patterns/index.md
- documents/vol4-advanced/ch05-template-metaprogramming/vol4-design-patterns/01-policy-based-design.md
- documents/vol4-advanced/ch05-template-metaprogramming/vol4-design-patterns/02-type-erasure.md
- documents/vol4-advanced/ch05-template-metaprogramming/vol4-design-patterns/03-nvi-template-method.md
- documents/vol4-advanced/ch05-template-metaprogramming/vol4-design-patterns/04-factory-pattern.md
- documents/vol4-advanced/ch05-template-metaprogramming/vol4-design-patterns/05-visitor-pattern.md
- documents/vol4-advanced/ch05-template-metaprogramming/vol4-design-patterns/06-singleton-pattern.md
- documents/vol4-advanced/ch05-template-metaprogramming/vol4-design-patterns/07-observer-pattern.md
- documents/vol4-advanced/ch05-template-metaprogramming/vol4-design-patterns/08-mixin-composition.md
- documents/vol4-advanced/ch05-template-metaprogramming/vol4-design-patterns/09-tag-dispatching.md
- documents/vol4-advanced/ch05-template-metaprogramming/vol4-design-patterns/10-dsl.md
- documents/vol4-advanced/ch05-template-metaprogramming/vol4-design-patterns/11-project-event-system.md

## 参考资料
- 《Modern C++ Design》— Andrei Alexandrescu（Policy-Based Design 开山之作）
- 《C++ Templates: The Complete Guide》(2nd Edition) Chapter 19-23
- 《Game Programming Patterns》— Robert Nystrom（状态机/观察者/命令模式）
- 《Hands-On Design Patterns with C++》— Fedor G. Pikus
