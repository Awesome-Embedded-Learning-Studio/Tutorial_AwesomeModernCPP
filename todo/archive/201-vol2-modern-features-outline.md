---
id: "201"
title: "卷二：现代 C++ 特性（C++11-17）— 全部章节大纲与文章规划"
category: content
priority: P0
status: done
created: 2026-04-16
assignee: charliechen
depends_on: ["200"]
blocks: ["202", "203", "204", "205"]
estimated_effort: epic
---

# 卷二：现代 C++ 特性（C++11-17）— 全部章节大纲与文章规划

## 总览

- **卷名**：vol2-modern-features
- **难度范围**：intermediate
- **预计文章数**：35-40 篇
- **前置知识**：卷一（C++ 基础入门）
- **C++ 标准覆盖**：C++11, C++14, C++17（核心重点）
- **目录位置**：`documents/vol2-modern-features/`
- **优先级说明**：P0 最高优先级 — 这是区分 "现代C++" 和 "旧C++" 的关键卷

## 章节大纲

### ch00：移动语义与右值引用

- **难度**：intermediate
- **预计篇数**：5
- **核心知识点**：右值引用、移动构造/赋值、RVO/NRVO、完美转发

#### 文章列表

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 | 代码示例 |
|------|--------|------|---------|---------|---------|
| 00-01 | 01-rvalue-reference.md | 右值引用：从拷贝到移动 | lvalue/rvalue/xvalue 区分、T&&、右值引用绑定规则 | 区分值类别 | rvalue.cpp |
| 00-02 | 02-move-semantics.md | 移动构造与移动赋值 | 移动构造函数、移动赋值运算符、std::move 原理 | 实现移动语义类 | movable.cpp |
| 00-03 | 03-rvo-nrvo.md | RVO 与 NRVO | 返回值优化、命名返回值优化、C++17 保证消除 | 分析编译器优化 | rvo_demo.cpp |
| 00-04 | 04-perfect-forwarding.md | 完美转发 | 引用折叠、std::forward、万能引用、forwarding reference | 实现完美转发包装器 | forward.cpp |
| 00-05 | 05-move-in-practice.md | 移动语义实战 | 移动语义在 STL 中的体现、swap 惯用法、性能对比 | benchmark 移动 vs 拷贝 | move_practice.cpp |

### ch01：智能指针与 RAII

- **难度**：intermediate → advanced
- **预计篇数**：6
- **核心知识点**：unique_ptr、shared_ptr、weak_ptr、RAII、自定义删除器

#### 文章列表

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 | 代码示例 |
|------|--------|------|---------|---------|---------|
| 01-01 | 01-raii-deep-dive.md | RAII 深入理解 | RAII 原则、资源获取即初始化、作用域守卫 | 设计 RAII 包装器 | raii.cpp |
| 01-02 | 02-unique-ptr.md | unique_ptr 详解 | 独占所有权、移动语义、make_unique、自定义删除器 | 资源管理实践 | unique_ptr.cpp |
| 01-03 | 03-shared-ptr.md | shared_ptr 详解 | 共享所有权、引用计数、控制块、make_shared | 共享资源管理 | shared_ptr.cpp |
| 01-04 | 04-weak-ptr.md | weak_ptr 与循环引用 | 弱引用、lock()、打破循环、观察者模式 | 解决循环引用 | weak_ptr.cpp |
| 01-05 | 05-custom-deleter.md | 自定义删除器 | 删除器类型、FILE* 管理、SDL 资源、C API 封装 | 封装 C API | custom_deleter.cpp |
| 01-06 | 06-scope-guard.md | scope_guard 与 defer | 通用作用域守卫、defer 模式、异常安全 | 实现 scope_guard | scope_guard.cpp |

### ch02：constexpr 与编译期计算

- **难度**：intermediate
- **预计篇数**：4
- **核心知识点**：constexpr 变量/函数/构造函数、consteval、constinit

#### 文章列表

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 | 代码示例 |
|------|--------|------|---------|---------|---------|
| 02-01 | 01-constexpr-basics.md | constexpr 基础 | constexpr 变量、constexpr 函数、编译期求值 | 编译期计算练习 | constexpr.cpp |
| 02-02 | 02-constexpr-ctor.md | constexpr 构造函数与字面类型 | 字面类型(literal type)、constexpr 构造函数、编译期对象 | 编译期类型设计 | literal_type.cpp |
| 02-03 | 03-consteval-constinit.md | consteval 与 constinit (C++20) | consteval 立即函数、constinit、编译期保证 | 区分 constexpr/consteval/constinit | consteval.cpp |
| 02-04 | 04-compile-time-practice.md | 编译期计算实战 | 编译期字符串处理、编译期查表、模板+constexpr 配合 | 实现编译期工具 | compile_time.cpp |

### ch03：Lambda 与函数式编程

- **难度**：intermediate
- **预计篇数**：5
- **核心知识点**：lambda 语法、捕获、泛型 lambda、std::function、函数对象

#### 文章列表

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 | 代码示例 |
|------|--------|------|---------|---------|---------|
| 03-01 | 01-lambda-basics.md | Lambda 基础 | lambda 语法、捕获列表、参数、返回类型 | STL 算法配合 lambda | lambda.cpp |
| 03-02 | 02-lambda-capture.md | 捕获机制深入 | 值捕获/引用捕获、init 捕获(C++14)、*this 捕获(C++17)、捕获陷阱 | 安全捕获练习 | capture.cpp |
| 03-03 | 03-generic-lambda.md | 泛型 Lambda 与模板 Lambda | auto 参数(C++14)、模板 lambda(C++20)、递归 lambda | 泛型算法编写 | generic_lambda.cpp |
| 03-04 | 04-std-function.md | std::function 与类型擦除 | std::function、函数指针对比、性能考量、small buffer optimization | 回调系统设计 | function.cpp |
| 03-05 | 05-functional-patterns.md | 函数式编程模式 | 高阶函数、柯里化、组合、ranges 预告 | 函数式工具编写 | functional.cpp |

### ch04：类型安全

- **难度**：intermediate
- **预计篇数**：5
- **核心知识点**：enum class、variant、optional、any

#### 文章列表

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 | 代码示例 |
|------|--------|------|---------|---------|---------|
| 04-01 | 01-enum-class.md | enum class 与强类型枚举 | 作用域枚举、底层类型、位运算、switch 匹配 | 重构 C 枚举为 enum class | enum_class.cpp |
| 04-02 | 02-strong-types.md | 强类型 typedef | using vs typedef、phantom type 模式、类型安全单位系统 | 实现类型安全度量 | strong_type.cpp |
| 04-03 | 03-variant.md | std::variant | 访问者模式、std::visit、运行时多态替代 | 实现类型安全联合体 | variant.cpp |
| 04-04 | 04-optional.md | std::optional | 可选值语义、monadic 操作(C++23)、与指针对比 | 可选返回值设计 | optional.cpp |
| 04-05 | 05-any.md | std::any 与类型擦除 | any 用法、any_cast、性能考量、何时使用 | 类型擦除容器 | any_demo.cpp |

### ch05：结构化绑定与初始化

- **难度**：intermediate
- **预计篇数**：2
- **核心知识点**：结构化绑定、if/switch 初始化器

#### 文章列表

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 | 代码示例 |
|------|--------|------|---------|---------|---------|
| 05-01 | 01-structured-bindings.md | 结构化绑定 | 绑定 pair/tuple/数组/结构体、自定义绑定、限定符 | 多返回值处理 | bindings.cpp |
| 05-02 | 02-init-statements.md | if/switch 初始化器 | if constexpr 预告、作用域限制变量、锁守卫模式 | 缩小变量作用域 | init_stmt.cpp |

### ch06：auto 与 decltype

- **难度**：intermediate
- **预计篇数**：3
- **核心知识点**：auto 推导、decltype、CTAD

#### 文章列表

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 | 代码示例 |
|------|--------|------|---------|---------|---------|
| 06-01 | 01-auto-deep-dive.md | auto 推导深入 | auto 规则、auto&/auto&&/const auto&、auto 与初始化列表 | 类型推导练习 | auto_demo.cpp |
| 06-02 | 02-decltype.md | decltype 与返回类型推导 | decltype 规则、decltype(auto)、尾置返回类型、C++14 auto 返回 | 模板返回类型设计 | decltype_demo.cpp |
| 06-03 | 03-ctad.md | 类模板参数推导 (CTAD) | 推导指引(deduction guide)、标准库 CTAD、自定义推导指引 | 简化模板使用 | ctad.cpp |

### ch07：属性系统

- **难度**：intermediate
- **预计篇数**：2
- **核心知识点**：C++11-17 属性、C++20-23 新增属性

#### 文章列表

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 | 代码示例 |
|------|--------|------|---------|---------|---------|
| 07-01 | 01-standard-attributes.md | 标准属性详解 | [[nodiscard]], [[maybe_unused]], [[deprecated]], [[fallthrough]], [[noreturn]] | 代码质量提升 | attributes.cpp |
| 07-02 | 02-modern-attributes.md | C++20-23 属性 | [[likely]]/[[unlikely]], [[no_unique_address]], [[assume]](C++23) | 性能导向属性 | modern_attr.cpp |

### ch08：string_view 深入

- **难度**：intermediate
- **预计篇数**：3
- **核心知识点**：string_view 原理、性能、陷阱

#### 文章列表

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 | 代码示例 |
|------|--------|------|---------|---------|---------|
| 08-01 | 01-string-view-internals.md | string_view 内部原理 | 实现原理(指针+长度)、SSO 对比、构造来源 | 理解实现机制 | sv_internals.cpp |
| 08-02 | 02-string-view-performance.md | string_view 性能分析 | 基准测试、减少分配、函数参数、substr O(1) | 性能对比测试 | sv_perf.cpp |
| 08-03 | 03-string-view-pitfalls.md | string_view 陷阱与最佳实践 | 悬垂引用、null 终止、隐式转换、与 string 互操作 | 避免常见错误 | sv_pitfalls.cpp |

### ch09：文件系统库

- **难度**：intermediate
- **预计篇数**：3
- **核心知识点**：std::filesystem、路径操作、目录遍历

#### 文章列表

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 | 代码示例 |
|------|--------|------|---------|---------|---------|
| 09-01 | 01-filesystem-path.md | path 操作 | path 构造/分解/修改、跨平台路径处理 | 路径操作工具 | path_demo.cpp |
| 09-02 | 02-filesystem-ops.md | 文件与目录操作 | exists/copy/move/remove、权限、空间查询 | 文件管理工具 | fs_ops.cpp |
| 09-03 | 03-directory-iteration.md | 目录遍历与搜索 | directory_iterator、recursive_directory_iterator、文件过滤 | 实现文件搜索器 | dir_iter.cpp |

### ch10：错误处理的现代方式

- **难度**：intermediate → advanced
- **预计篇数**：4
- **核心知识点**：variant/optional/expected、monadic 操作

#### 文章列表

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 | 代码示例 |
|------|--------|------|---------|---------|---------|
| 10-01 | 01-error-handling-evolution.md | 错误处理的演进 | 错误码→异常→optional→expected 的演进历程 | 分析各方案优劣 | evolution.cpp |
| 10-02 | 02-optional-error.md | optional 用于错误处理 | optional<T> 表示"可能没有值"、and_then/or_else(C++23) | 设计 optional 返回函数 | opt_error.cpp |
| 10-03 | 03-expected-error.md | std::expected<T, E> (C++23) | expected 语义、and_then/transform/or_else、与 Result<T,E> 对比 | 实现错误传播链 | expected.cpp |
| 10-04 | 04-error-patterns.md | 错误处理模式总结 | 各方案选择指南、性能对比、最佳实践 | 综合错误处理设计 | patterns.cpp |

### ch11：用户自定义字面量

- **难度**：intermediate
- **预计篇数**：2
- **核心知识点**：原始/cooked 字面量、常见用例

#### 文章列表

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 | 代码示例 |
|------|--------|------|---------|---------|---------|
| 11-01 | 01-udl-basics.md | 用户自定义字面量基础 | operator""、原始/cooked 形式、标准库字面量 | 实现自定义字面量 | udl.cpp |
| 11-02 | 02-udl-practice.md | UDL 实战 | 单位系统（m, kg, s）、字符串处理、编译期计算配合 | 物理单位库 | units.cpp |

## 练习与项目

### 文章末尾练习
- 每篇文章末尾 3-5 道练习
- 类型：概念理解、代码分析、编程实现、性能对比
- 每道提供参考答案

### 中级实战项目
1. **内存池分配器**（ch00-ch01 后）：结合移动语义和智能指针，实现高效内存池
2. **事件系统**（ch03-ch04 后）：基于 lambda 和 variant 实现类型安全的事件系统

### 代码示例组织
- `code/vol2-modern-features/` 下按章节组织
- 每个示例都是可编译的 CMake 项目
- 包含 CMakeLists.txt 和必要的测试文件

## 与其他卷的交叉引用

- **← 卷一**：ch04 智能指针预告 → ch01 详细讲解
- **← 卷一**：ch03 constexpr 预告 → ch02 深入
- **→ 卷三**：string_view 深入 → 卷三 ch04 字符串深入
- **→ 卷四**：模板 lambda → 卷四 ch00 Concepts
- **→ 卷五**：lambda 捕获 → 卷五 ch01 线程原语
- **→ 卷六**：移动语义性能 → 卷六 ch01 CPU 缓存

## 现有内容映射

| 现有文章 | 重写去向 | 备注 |
|----------|---------|------|
| core-embedded-cpp/ch03/03-rvo-nrvo.md | ch00/03-rvo-nrvo.md | 脱离嵌入式上下文重写 |
| core-embedded-cpp/ch06/* (8篇) | ch01/* | RAII/智能指针系列通用化重写 |
| core-embedded-cpp/ch04/01-constexpr.md | ch02/* | constexpr 通用化 |
| core-embedded-cpp/ch09/* (8篇) | ch03/* | Lambda/函数式通用化 |
| core-embedded-cpp/ch08/01-enum-class.md | ch04/01 | 通用化 |
| core-embedded-cpp/ch08/03-variant.md | ch04/03 | 通用化 |
| core-embedded-cpp/ch08/04-optional.md | ch04/04 | 通用化 |
| core-embedded-cpp/ch08/05-expected.md | ch10/03 | 通用化 |
| core-embedded-cpp/ch11/02-structured-bindings.md | ch05/01 | 通用化 |
| core-embedded-cpp/ch11/01-auto-and-decltype.md | ch06/* | 拆分并深入 |
| core-embedded-cpp/ch11/04-attributes.md | ch07/* | 扩展 C++20-23 属性 |
| cpp-features/cpp17-string-view.md | ch08/* | 拆分为 3 篇深入讲解 |
| core-embedded-cpp/ch11/07-user-defined-literals.md | ch11/* | 扩展并增加实战 |
