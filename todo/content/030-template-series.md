---
id: 030
title: "模板编程四卷系列"
category: content
priority: P2
status: pending
created: 2026-04-15
assignee: charliechen
depends_on: ["architecture/002", "203"]
# NOTE: 本 TODO 已对齐到卷四 ch05 (vol4-advanced/ch05-template-metaprogramming/)
# 详细大纲见 todo/content/203-vol4-advanced-outline.md
blocks: []
estimated_effort: epic
---

# 模板编程四卷系列

## 目标
基于 drafts/Content-Table-TemplateGuide.md 规划，编写完整的模板编程四卷系列教程。从模板基础到高级元编程技术，再到嵌入式场景的实战应用，系统性地覆盖 C++ 模板的方方面面。每卷独立成章，循序渐进。

## 验收标准
- [ ] Vol1 模板基础完成：函数模板、类模板、非类型参数、特化与偏特化
- [ ] Vol2 现代模板完成：变参模板、折叠表达式、if constexpr、SFINAE、Concepts
- [ ] Vol3 模板元编程完成：类型萃取、编译期计算、CRTP、Policy-based design
- [ ] Vol4 模板实战完成：嵌入式场景应用、零开销抽象、代码生成技巧
- [ ] 每卷至少 8 个章节，每章配备完整代码示例
- [ ] 嵌入式场景示例覆盖 GPIO/UART/SPI/中断等常见场景
- [ ] 编译期输出验证（static_assert）贯穿全文
- [ ] 与 drafts/Content-Table-TemplateGuide.md 大纲对齐

## 实施说明
模板编程是 C++ 最强大也最复杂的功能之一。本系列采取由浅入深、理论结合实践的教学策略。

**Vol1：模板基础**

1. 函数模板 — 定义、实例化、类型推导、显式指定模板参数、重载决议。
2. 类模板 — 定义、成员函数、友元、静态成员、模板参数。
3. 非类型模板参数 — 整数参数、指针参数、C++20 的浮点数和类类型 NTTP、编译期常量。
4. 模板特化 — 全特化、偏特化的语法和应用场景（如 `vector<bool>` 的特化思想）。
5. 模板实例化模型 — 隐式实例化、显式实例化、包含模型、分离编译模型。
6. 名称查找与依赖类型 — 两阶段名称查找、`typename` 和 `template` 消歧义符。
7. 模板与继承 — 模板基类名称查找、混入（Mixin）模式。
8. 综合练习 — 实现一个泛型的环形缓冲区模板。

**Vol2：现代模板（C++11/14/17/20）**

1. 变参模板（Variadic Templates） — 参数包、包展开、递归终止、sizeof...运算符。
2. 折叠表达式（Fold Expressions） — 左折叠/右折叠、一元/二元折叠、常见应用模式。
3. if constexpr — 编译期条件分支、替代 SFINAE 的简洁方案、与模板递归配合。
4. SFINAE 与 enable_if — 替换失败不是错误的原则、std::enable_if、void_t 技巧、检测成员惯用法。
5. Concepts（C++20） — concept 定义、requires 子句、requires 表达式、标准库 concepts、约束的偏序。
6. auto 与模板 — auto 作为模板参数（C++17）、返回类型推导、decltype(auto)。
7. 类模板参数推导（CTAD） — 推导指引、标准库 CTAD 示例。
8. 综合练习 — 用 Concepts 重构 SFINAE 代码。

**Vol3：模板元编程**

1. 类型萃取（Type Traits） — 标准库 type_traits 分类、自定义 type trait、实现原理。
2. 编译期计算 — 模板递归计算、constexpr 函数替代、编译期字符串处理。
3. CRTP（Curiously Recurring Template Pattern） — 静态多态、代码复用、接口注入。
4. Policy-Based Design — 策略类设计、组合优于继承、编译期策略选择。
5. 表达式模板 — 延迟求值、避免临时对象（以嵌入式数学运算为例）。
6. 标签分发（Tag Dispatch） — iterator_category 风格的编译期分派。
7. 编译期数据结构 — 编译期链表（typelist）、编译期 map、编译期字符串。
8. 综合练习 — 实现一个编译期消息路由框架。

**Vol4：模板实战（嵌入式场景）**

1. 硬件寄存器抽象 — 使用模板实现类型安全的寄存器访问、位域操作、零开销抽象。
2. 引脚配置模板 — 编译期 GPIO 配置、端口/引号参数化、配置验证。
3. 中断处理模板 — 编译期中断向量表生成、类型安全的 ISR 注册。
4. DMA 描述符模板 — 编译期 DMA 传输描述、类型安全的缓冲区管理。
5. 通信协议模板 — 串行通信协议的模板化封装（SPI/I2C/UART）。
6. 状态机模板 — 基于模板的编译期状态机、表格驱动状态机。
7. 零开销抽象实战 — 性能对比：模板方案 vs C 方案 vs 虚函数方案。
8. 代码膨胀控制 — 模板实例化管理、显式实例化、避免不必要膨胀的策略。

## 涉及文件
- documents/general/templates/vol1-basics/index.md
- documents/general/templates/vol1-basics/01-function-templates.md
- documents/general/templates/vol1-basics/02-class-templates.md
- documents/general/templates/vol1-basics/03-nttp.md
- documents/general/templates/vol1-basics/04-specialization.md
- documents/general/templates/vol1-basics/05-instantiation.md
- documents/general/templates/vol1-basics/06-name-lookup.md
- documents/general/templates/vol1-basics/07-inheritance.md
- documents/general/templates/vol1-basics/08-exercise-ring-buffer.md
- documents/general/templates/vol2-modern/index.md
- documents/general/templates/vol2-modern/ (01-08 章节)
- documents/general/templates/vol3-metaprogramming/index.md
- documents/general/templates/vol3-metaprogramming/ (01-08 章节)
- documents/general/templates/vol4-practice/index.md
- documents/general/templates/vol4-practice/ (01-08 章节)
- drafts/Content-Table-TemplateGuide.md (参考大纲)

## 参考资料
- drafts/Content-Table-TemplateGuide.md — 项目内规划大纲
- 《C++ Templates: The Complete Guide》(2nd Edition) — David Vandevoorde et al.
- 《Modern C++ Design》— Andrei Alexandrescu
- 《C++ Template Metaprogramming》— Abrahams & Gurtovoy
- cppreference.com Templates 章节
-Meeting C++ 和 CppCon 模板相关演讲
