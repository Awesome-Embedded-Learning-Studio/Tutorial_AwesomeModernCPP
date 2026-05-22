---
id: 039
title: "模板编程卷二：现代模板技术（C++17 核心特性）"
category: content
priority: P2
status: pending
created: 2026-05-22
assignee: charliechen
depends_on:
  - "038"
blocks: []
estimated_effort: epic
---

# 模板编程卷二：现代模板技术（C++17 核心特性）

## 目标
掌握现代 C++ 模板编程的核心工具，大幅提升代码表达力和编译期计算能力。覆盖 type traits、SFINAE、if constexpr、变参模板、折叠表达式、完美转发、constexpr、CTAD，最终通过综合项目实现类型安全的 `any` 类型。

## 验收标准
- [ ] T2.1 类型萃取深度解析完成
- [ ] T2.2 SFINAE 与 enable_if 完成
- [ ] T2.3 if constexpr 完成
- [ ] T2.4 可变参数模板完成
- [ ] T2.5 折叠表达式完成
- [ ] T2.6 完美转发完成
- [ ] T2.7 constexpr 与编译期计算完成
- [ ] T2.8 CTAD 完成
- [ ] 卷二综合项目：类型安全的 `any` 完成
- [ ] 每节包含 SFINAE/constexpr 版本的对比示例
- [ ] 标准库溯源至少覆盖 3 个（iterator_traits、tuple、enable_if_t）

## 实施说明

### T2.1 类型萃取（Type Traits）深度解析
- [ ] `<type_traits>` 全景概览（三类：检查、转换、修改）
- [ ] 类型检查 traits：`is_integral`、`is_pointer`、`is_class`、`is_same`
- [ ] 类型转换 traits：`remove_reference`、`remove_const`、`decay`、`common_type`
- [ ] 类型修改 traits：`conditional`、`enable_if`、`void_t`
- [ ] `invoke_result` 与 callable traits
- [ ] **实战：** 实现 `is_iterable`（检测是否有 `begin()`/`end()`）、`is_smart_pointer`（检测 `unique_ptr`/`shared_ptr`）
- [ ] **标准库深潜：** `std::iterator_traits` 完整实现剖析（含指针偏特化）

### T2.2 SFINAE 与替换失败并非错误
- [ ] SFINAE 原理详解：模板参数替换 vs 实例化的区别
- [ ] `std::enable_if` 的三种写法（模板参数、函数参数、返回类型）
- [ ] 函数重载决议中的 SFINAE 交互
- [ ] `void_t` 技巧与检测成员惯用法（detect idiom）
- [ ] **实战：** 条件成员函数的实现（仅当 `T` 可序列化时提供 `serialize()`）
- [ ] **调试技巧：** 使用 `static_assert` 改善 SFINAE 的错误信息
- [ ] **常见陷阱：** 硬错误（hard error）vs SFINAE 友好的失败；为什么有些替换失败不触发 SFINAE

### T2.3 if constexpr：编译期分支
- [ ] `if constexpr` 语法与语义（C++17）
- [ ] `if constexpr` vs 传统 SFINAE 的优劣对比
- [ ] 编译期递归的简化（替代模板特化递归终止）
- [ ] `if constexpr` 的限制（不能脱离 `if` 上下文、extern 模板交互）
- [ ] **实战：** 实现 `print` 函数支持任意类型（容器、tuple、基本类型的统一输出）
- [ ] **性能分析：** 零运行时开销的验证（Godbolt 汇编对比）

### T2.4 可变参数模板（Variadic Templates）
- [ ] 参数包（Parameter Pack）详解：`typename... Types`、`Types... args`
- [ ] 包展开（Pack Expansion）的四种方式：函数参数、模板参数、基类列表、初始化列表
- [ ] `sizeof...` 运算符
- [ ] 递归展开 vs 折叠表达式的演进关系
- [ ] **实战：** 实现 `printf` 风格的类型安全日志函数（编译期格式检查）
- [ ] **标准库深潜：** `std::tuple` 的构造原理（递归继承实现）

### T2.5 折叠表达式（Fold Expressions）
- [ ] 一元折叠：`(... op pack)` vs `(pack op ...)`
- [ ] 二元折叠：`(init op ... op pack)` vs `(pack op ... op init)`
- [ ] 四种折叠模式的实际应用：`,`（顺序执行）、`+`（求和）、`&&`（全部满足）、`||`（任一满足）
- [ ] 空包展开的规则与默认值
- [ ] **实战：** 实现 `all_of`、`any_of`、`for_each_arg`（折叠表达式版本）
- [ ] **性能对比：** 折叠表达式 vs 递归模板的编译时性能与代码生成

### T2.6 完美转发（Perfect Forwarding）
- [ ] 万能引用（Universal Reference / Forwarding Reference）vs 右值引用的区分
- [ ] 引用折叠规则（四种组合推导）
- [ ] `std::forward` 实现原理与正确使用方式
- [ ] `std::move` vs `std::forward` 的选择
- [ ] **实战：** 实现 `make_unique`、泛型工厂函数 `make<T>(args...)`
- [ ] **常见陷阱：** 转发引用的 `auto&&` 用法、重载决议中的完美转发拦截问题、按值传递 vs 完美转发的权衡

### T2.7 constexpr 函数与编译期计算
- [ ] `constexpr` 的演进：C++11（单 return）→ C++14（循环、局部变量）→ C++17（if、更多放宽）
- [ ] constexpr 函数的限制与放宽时间线
- [ ] `constexpr` lambda（C++17）
- [ ] `constexpr` if 与 `constexpr` 函数的配合
- [ ] **实战：** 编译期 CRC32 计算、编译期 MD5 计算
- [ ] **嵌入式应用：** 编译期查找表生成（LUT）替代运行时计算

### T2.8 类模板参数推导（CTAD）
- [ ] 自动推导规则（从构造函数参数推导）
- [ ] 推导指南（Deduction Guides）的语法与自定义
- [ ] 标准库 CTAD 示例：`std::pair`、`std::tuple`、`std::vector`
- [ ] **实战：** 为自定义容器 `ring_buffer` 添加 CTAD 支持
- [ ] **注意事项：** 隐式转换陷阱、CTAD 与 `explicit` 构造函数的交互

### 卷二综合项目：类型安全的 `any` 类型
- [ ] 支持任意类型的存储与检索（`any::any<T>()`、`any_cast<T>`）
- [ ] 使用 SFINAE / `if constexpr` 优化类型检查
- [ ] Small Buffer Optimization（SBO）策略
- [ ] 与 `std::any` 的接口兼容与性能对比

## 涉及文件
- documents/vol4-advanced/vol2-modern-cpp17/index.md
- documents/vol4-advanced/vol2-modern-cpp17/01-type-traits.md
- documents/vol4-advanced/vol2-modern-cpp17/02-sfinae.md
- documents/vol4-advanced/vol2-modern-cpp17/03-if-constexpr.md
- documents/vol4-advanced/vol2-modern-cpp17/04-variadic-templates.md
- documents/vol4-advanced/vol2-modern-cpp17/05-fold-expressions.md
- documents/vol4-advanced/vol2-modern-cpp17/06-perfect-forwarding.md
- documents/vol4-advanced/vol2-modern-cpp17/07-constexpr.md
- documents/vol4-advanced/vol2-modern-cpp17/08-ctad.md
- documents/vol4-advanced/vol2-modern-cpp17/09-project-any.md

## 参考资料
- 《C++ Templates: The Complete Guide》(2nd Edition) Part III & IV
- 《Effective Modern C++》— Scott Meyers（Item 1-8, 26-30）
- cppreference.com Type Traits / Fold Expressions 章节
