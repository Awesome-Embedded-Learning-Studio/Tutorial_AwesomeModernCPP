# C++ 模板编程全面指南 - 内容表

> **定位：** 一套媲美《C++ Templates: The Complete Guide》的全面模板编程指南
>
> **覆盖标准：** C++11/14 → C++17 → C++20/23
>
> **目标受众：** 混合层次（兼顾初学者和有经验的开发者）
>
> **核心特色：** 标准库源码解析 + 通用泛型编程理论 + 编译期元编程 + 嵌入式/MCU实战

---

## 卷一：模板基础（C++11/14 核心机制）

**定位：** 从零开始建立完整的模板基础知识体系，适合初学者和需要巩固基础的开发者。

### T1.1 函数模板
- 模板参数推导规则
- 尾随返回类型与返回类型推导
- 模板重载与 specialization
- **实战：** 实现通用的 `min/max/clamp` 函数族
- **嵌入式贴士：** 避免代码膨胀的技巧

### T1.2 类模板
- 类模板声明与定义
- 模板参数的默认值
- 成员函数模板
- 虚函数与模板的限制
- **实战：** 实现一个固定容量的 `ring_buffer<T, N>`
- **调试技巧：** 理解模板实例化错误信息

### T1.3 模板特化与偏特化
- 全特化 vs 偏特化
- 类模板偏特化的匹配规则
- 函数模板的重载替代特化
- **实战：** 为指针类型特化 `traits` 类
- **标准库溯源：** `std::iterator_traits` 的指针特化

### T1.4 非类型模板参数
- 整数、指针、引用类型的非类型参数
- auto 作为非类型模板参数类型（C++17）
- **实战：** 编译期数组大小、位掩码生成器
- **嵌入式应用：** 寄存器地址的编译期封装

### T1.5 模板参数依赖与名字查找
- 依赖名称（Dependent Names）
- 两阶段查找（Two-Phase Lookup）
- typename 和 template 关键字的必要性
- ADL（Argument-Dependent Lookup）详解
- **实战：** 正确编写泛型迭代器代码
- **常见陷阱：** 为什么 `t.clear()` 有时不工作？

### T1.6 模板友元与 Barton-Nackman Trick
- 友元注入机制
- Barton-Nackman 模式（CRTP 前身）
- 运算符重载的模板技巧
- **实战：** 实现可比较的 `Point<T>` 类型

### T1.7 模板别名与 Using 声明
- typedef vs using
- 别名模板（Alias Templates）
- **标准库溯源：** `std::conditional_t`、`std::enable_if_t`
- **实战：** 简化复杂模板类型的声明

### T1.8 模板与继承
- 奇异递归模板模式（CRTP）详解
- 静态多态 vs 动态多态
- 混入（Mixin）模式
- **实战：** 使用 CRTP 实现单例基类、计数器基类
- **性能分析：** CRTP vs 虚函数的汇编对比

### 卷一综合项目：实现一个轻量级的 `fixed_vector<T, N>`
- 完整的迭代器支持（含 const_iterator）
- 与 `std::vector` 兼容的接口
- 编译期边界检查（可选）

---

## 卷二：现代模板技术（C++17 特性）

**定位：** 掌握现代C++模板编程的核心工具，大幅提升代码表达力和编译期计算能力。

### T2.1 类型萃取（Type Traits）深度解析
- `<type_traits>` 全景概览
- 类型检查、转换、修改三大类别
- 常用 traits 详解：`is_same`、`remove_reference`、`decay`、`invoke_result`
- **实战：** 实现 `is_iterable`、`is_smart_pointer` 自定义 traits
- **标准库深潜：** `std::iterator_traits` 完整实现

### T2.2 SFINAE 与替换失败并非错误
- SFINAE 原理详解
- `std::enable_if` 的多种写法
- 函数重载决议中的 SFINAE
- **实战：** 条件成员函数的实现
- **调试技巧：** 使用 static_assert 改善错误信息
- **常见陷阱：** 硬错误 vs SFINAE

### T2.3 if constexpr：编译期分支
- if constexpr 语法与语义
- vs 传统 SFINAE 的优劣
- 编译期递归的简化
- **实战：** 实现 `print` 函数支持任意类型
- **性能分析：** 零运行时开销的验证

### T2.4 可变参数模板（Variadic Templates）
- 参数包（Parameter Pack）详解
- 包展开的四种方式
- 递归展开 vs 折叠表达式
- **实战：** 实现 `printf` 风格的类型安全日志函数
- **标准库深潜：** `std::tuple` 的构造原理

### T2.5 折叠表达式（Fold Expressions）
- 一元折叠、二元折叠
- 四种折叠模式：`, + && ||`
- **实战：** 实现 `all_of`、`any_of`、`for_each_arg`
- **性能对比：** 折叠表达式 vs 递归模板的编译时性能

### T2.6 完美转发（Perfect Forwarding）
- 万能引用（Universal Reference）vs 右值引用
- 引用折叠规则
- `std::forward` 实现原理
- **实战：** 实现 `make_unique`、泛型工厂函数
- **常见陷阱：** 转发引用的 auto&&、重载决议问题

### T2.7 constexpr 函数与编译期计算
- constexpr 的演进（C++11→14→17）
- constexpr 函数的限制与放宽
- constexpr lambda（C++17）
- **实战：** 编译期 CRC32、MD5 计算
- **嵌入式应用：** 编译期查找表生成

### T2.8 类模板参数推导（CTAD）
- 自动推导规则
- 推导指南（Deduction Guides）
- **实战：** 为自定义容器添加 CTAD 支持
- **注意事项：** 隐式转换陷阱

### 卷二综合项目：实现一个类型安全的 `any` 类型
- 支持任意类型的存储与检索
- 使用 SFINAE/constexpr 优化
- 与 `std::any` 的性能对比

---

## 卷三：元编程精要（C++20/23 约束与元编程）

**定位：** 掌握现代C++的约束机制和高级元编程技术，编写更安全、更易维护的泛型代码。

### T3.1 概念（Concepts）详解
- concept 声明与定义
- requires 表达式语法
- requires 子句
- **标准库概览：** `std::integral`、`sortable`、`range` 等核心概念
- **实战：** 定义 `Numeric`、`Addable`、`Hashable` 概念

### T3.2 使用 Concepts 约束模板
- concept 作为模板参数约束
- 缩写函数模板（Abbreviated Function Templates）
- concept 重载与优先级
- **vs SFINAE：** 为什么 Concepts 是更好的选择
- **实战：** 重写算法库使用 Concepts
- **嵌入式贴士：** 更清晰的编译错误信息

### T3.3 Requires 表达式深度解析
- requires 表达式的四种成分
- 简单要求、类型要求、复合要求、嵌套要求
- **实战：** 定义复杂的概念（如 `Range`、`Iterator`）
- **标准库溯源：** `std::ranges::range` 概念定义

### T3.4 模板元编程（TMP）核心技巧
- 类型列表（Type List）操作
- 编译期映射与查找
- 编译期算法：排序、搜索
- **传统 TMP vs constexpr：** 迁移指南
- **实战：** 实现 `type_list` 和对应的算法

### T3.5 编译期字符串处理
- 字符串作为非类型模板参数（C++20）
- 编译期字符串操作
- **实战：** 实现编译期正则表达式匹配
- **嵌入式应用：** 协议解析的编译期优化

### T3.6 反射元编程基础
- `std::is_aggregate`、`std::is_layout_compatible`
- 结构化绑定与聚合类型
- **实战：** 实现 `for_each_member` 遍历结构体成员
- **前瞻：** C++26 静态反射简介

### T3.7 模板实例化控制
- 显式实例化声明与定义
- extern template 减少编译时间
- 实例化点（POI）详解
- **嵌入式关键：** 控制代码膨胀的实用技巧
- **实战：** 为常用类型显式实例化模板

### T3.8 模板与异常安全
- 强异常保证（Strong Exception Guarantee）
- noexcept 在模板中的应用
- 条件 noexcept（C++17）
- **实战：** 实现异常安全的泛型容器

### 卷三综合项目：实现一个 mini-STL 算法库
- 使用 Concepts 约束所有算法
- 实现 `sort`、`find`、`transform` 等核心算法
- 与 `std::algorithm` 的性能对比

---

## 卷四：泛型设计模式实战（架构级应用）

**定位：** 将模板技术应用于实际架构设计，掌握业界验证的设计模式和架构范式。

### T4.1 Policy-Based Design（策略设计）
- Policy Class 的定义与设计原则
- vs 传统策略模式的优劣
- **经典案例：** `std::allocator` 作为 policy
- **实战：** 设计可配置的智能指针（删除策略、所有权策略）
- **嵌入式应用：** 可插拔的内存管理策略

### T4.2 类型擦除（Type Erasure）
- 类型擦除原理
- Small Buffer Optimization（SBO）
- **标准库深潜：** `std::function` 完整实现剖析
- **标准库深潜：** `std::any` 实现剖析
- **实战：** 实现 `function<R(Args...)>`
- **性能权衡：** 虚函数 vs 类型擦除 vs 模板

### T4.3 模板方法模式与 NVI
- 非虚拟接口（NVI）模式
- 编译期模板方法
- **实战：** CRTP 实现的算法框架
- **性能分析：** vs 虚函数调用的汇编对比

### T4.4 工厂模式的模板实现
- 抽象工厂的编译期版本
- 对象构造的泛型解决方案
- **实战：** 实现 `generic_factory<T>`
- **嵌入式应用：** 驱动程序的编译期注册

### T4.5 访问者模式的模板实现
- 传统访问者模式的局限
- `std::variant` + `std::visit` 的现代方案
- **实战：** 实现编译期访问者模式
- **嵌入式应用：** 命令模式与事件分发

### T4.6 单例模式的线程安全实现
- Meyer's Singleton
- 模板单例基类（CRTP）
- **实战：** `singleton<T>` 的线程安全实现
- **注意：** 静态初始化顺序问题

### T4.7 观察者模式的模板实现
- 编译期类型安全的信号槽
- **实战：** 实现 `signal<R(Args...)>` 和 `slot`
- **嵌入式应用：** ISR 到任务的事件分发

### T4.8 混入（Mixin）与组合式设计
- CRTP 作为 Mixin 机制
- 参数化继承
- **实战：** 构建可组合的组件（日志、计数、锁）
- **设计原则：** Mixin vs 组合

### T4.9 Tag Dispatching 与类型分派
- Iterator Tags 详解
- 编译期算法选择
- **标准库溯源：** `std::advance` 的 tag dispatching
- **实战：** 实现优化的算法选择器

### T4.10 模板与 DSL（领域特定语言）
- 内嵌 DSL 的设计原则
- 运算符重载在 DSL 中的应用
- **实战：** 实现类型安全的单位系统（米/秒）
- **实战：** 实现状态机编译期 DSL

### 卷四综合项目：实现一个完整的嵌入式事件系统
- 编译期类型安全的事件分发
- 支持 ISR-safe 的队列
- Policy-Based 的内存管理策略
- 综合运用：CRTP、Type Erasure、Policy Design

---

## 附录：嵌入式模板开发指南

### A.1 代码膨胀控制
- extern template 实践
- 共享基类技术
- 模板 vs 运行时多态的权衡

### A.2 编译时间优化
- 头文件组织策略
- 预编译头（PCH）
- 模板实例化优化

### A.3 嵌入式友好的模板库选择
- ETL（Embedded Template Library）简介
- 自研模板库的最佳实践

---

## 参考资料与延伸阅读

### 经典书籍
- *C++ Templates: The Complete Guide* (2nd Edition) - David Vandevoorde, Nicolai M. Josuttis
- *Modern C++ Design* - Andrei Alexandrescu
- *Effective C++* / *Effective Modern C++* - Scott Meyers

### 在线资源
- cppreference.com - 模板相关章节
- C++ Standard Drafts - N4860 (C++20), N4950 (C++23)
- Fluent C++ (fluentcpp.com)
- Modernes C++ (modernescpp.com)

### 标准库实现参考
- libstdc++ (GCC)
- libc++ (Clang)
- MSVC STL

---

**文档版本：** v1.0
**最后更新：** 2026-03-17
