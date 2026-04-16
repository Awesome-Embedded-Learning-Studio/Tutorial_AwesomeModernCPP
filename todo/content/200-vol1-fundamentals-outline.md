---
id: "200"
title: "卷一：C++ 基础入门 — 全部章节大纲与文章规划"
category: content
priority: P1
status: pending
created: 2026-04-16
assignee: charliechen
depends_on: ["300"]
blocks: ["201", "202", "203", "204", "205", "206", "207"]
estimated_effort: epic
---

# 卷一：C++ 基础入门 — 全部章节大纲与文章规划

## 总览

- **卷名**：vol1-fundamentals
- **难度范围**：beginner
- **预计文章数**：50-60 篇
- **前置知识**：无（零基础）
- **C++ 标准覆盖**：C++98 基础 + 现代C++预告
- **目录位置**：`documents/vol1-fundamentals/`

## 章节大纲

### ch00：环境搭建与第一个程序

- **难度**：beginner
- **预计篇数**：4
- **核心知识点**：开发环境搭建、编译器基础、CMake 项目、第一个程序

#### 文章列表

| 编号 | 文件名 | 标题 | 难度 | 核心内容 | 练习重点 | 代码示例 |
|------|--------|------|------|---------|---------|---------|
| 00-01 | 00-preface.md | 前言：为什么学 C++ | beginner | 课程定位、C++ 的应用领域、学习路线图 | 无 | 无 |
| 00-02 | 01-setup-linux.md | Linux 环境搭建 | beginner | GCC/Clang 安装、CMake 安装、VS Code 配置 | 安装并验证工具链 | CMakeLists.txt + hello.cpp |
| 00-03 | 02-setup-windows.md | Windows 环境搭建 | beginner | MSVC/Visual Studio 安装、vcpkg 配置 | 安装并验证工具链 | CMakeLists.txt + hello.cpp |
| 00-04 | 03-first-program.md | 第一个 C++ 程序 | beginner | main 函数、iostream、编译运行流程、常见错误 | 修改程序输出不同内容 | hello.cpp, calc.cpp |

### ch01：类型与值类别

- **难度**：beginner
- **预计篇数**：4
- **核心知识点**：基本类型、类型转换、const、值类别

#### 文章列表

| 编号 | 文件名 | 标题 | 难度 | 核心内容 | 练习重点 | 代码示例 |
|------|--------|------|------|---------|---------|---------|
| 01-01 | 01-basic-types.md | 基本数据类型 | beginner | 整数/浮点/字符/布尔、sizeof、limits | 计算不同类型的大小和范围 | type_sizes.cpp |
| 01-02 | 02-type-conversion.md | 类型转换 | beginner | 隐式转换、显式转换、static_cast、数值精度问题 | 类型转换陷阱练习 | conversion.cpp |
| 01-03 | 03-const-basics.md | const 初探 | beginner | const 变量、const 指针、constexpr 预告 | const 正确使用 | const_demo.cpp |
| 01-04 | 04-value-categories.md | 值类别简介 | beginner | lvalue/rvalue 概念、引用初步 | 区分左右值 | values.cpp |

### ch02：控制流

- **难度**：beginner
- **预计篇数**：3
- **核心知识点**：条件、循环、range-for

#### 文章列表

| 编号 | 文件名 | 标题 | 难度 | 核心内容 | 练习重点 | 代码示例 |
|------|--------|------|------|---------|---------|---------|
| 02-01 | 01-conditionals.md | 条件语句 | beginner | if/else、switch、三元运算符、初始化器(C++17) | 分支逻辑练习 | conditional.cpp |
| 02-02 | 02-loops.md | 循环语句 | beginner | for/while/do-while、break/continue、嵌套循环 | 模式打印练习 | loops.cpp |
| 02-03 | 03-range-for.md | range-for 循环 | beginner | range-for 语法、数组/容器遍历、auto 与 range-for | 遍历练习 | range_for.cpp |

### ch03：函数

- **难度**：beginner
- **预计篇数**：4
- **核心知识点**：函数定义、参数传递、重载、inline

#### 文章列表

| 编号 | 文件名 | 标题 | 难度 | 核心内容 | 练习重点 | 代码示例 |
|------|--------|------|------|---------|---------|---------|
| 03-01 | 01-function-basics.md | 函数基础 | beginner | 定义/声明、返回值、参数、作用域 | 编写工具函数 | functions.cpp |
| 03-02 | 02-pass-by-value-ref.md | 参数传递方式 | beginner | 值传递、引用传递、const 引用、何时用哪种 | 交换函数、效率对比 | passing.cpp |
| 03-03 | 03-overloading-default.md | 重载与默认参数 | beginner | 函数重载规则、默认参数、重载决议 | 设计重载函数族 | overload.cpp |
| 03-04 | 04-inline-constexpr.md | inline 与 constexpr 函数 | beginner | inline 语义、constexpr 函数、编译期计算预告 | constexpr 函数编写 | inline_constexpr.cpp |

### ch04：指针与引用

- **难度**：beginner
- **预计篇数**：4
- **核心知识点**：指针基础、引用、指针运算

#### 文章列表

| 编号 | 文件名 | 标题 | 难度 | 核心内容 | 练习重点 | 代码示例 |
|------|--------|------|------|---------|---------|---------|
| 04-01 | 01-pointer-basics.md | 指针基础 | beginner | 取地址/解引用、指针类型、空指针 | 指针操作练习 | pointers.cpp |
| 04-02 | 02-pointer-arithmetic.md | 指针运算与数组 | beginner | 指针算术、指针与数组、指针与字符串 | 数组遍历的多种方式 | ptr_arith.cpp |
| 04-03 | 03-references.md | 引用 | beginner | 引用语法、引用 vs 指针、const 引用 | 引用传参实践 | references.cpp |
| 04-04 | 04-smart-ptr-preview.md | 智能指针预告 | beginner | 为什么需要智能指针、unique_ptr 简介预告 | 无（仅概念引入） | unique_ptr_intro.cpp |

### ch05：数组与字符串

- **难度**：beginner
- **预计篇数**：3
- **核心知识点**：C数组、std::array、std::string

#### 文章列表

| 编号 | 文件名 | 标题 | 难度 | 核心内容 | 练习重点 | 代码示例 |
|------|--------|------|------|---------|---------|---------|
| 05-01 | 01-c-arrays.md | C 风格数组 | beginner | 数组声明/初始化、多维数组、数组退化 | 矩阵运算 | arrays.cpp |
| 05-02 | 02-std-array.md | std::array | beginner | std::array 用法、与 C 数组对比、遍历 | 改写C数组为 std::array | std_array.cpp |
| 05-03 | 03-std-string.md | std::string | beginner | string 构造/拼接/查找/子串、与 C 字符串互转 | 字符串处理工具 | string_demo.cpp |

### ch06：类与面向对象

- **难度**：beginner → intermediate
- **预计篇数**：6
- **核心知识点**：类定义、构造/析构、访问控制、友元、static

#### 文章列表

| 编号 | 文件名 | 标题 | 难度 | 核心内容 | 练习重点 | 代码示例 |
|------|--------|------|------|---------|---------|---------|
| 06-01 | 01-class-basics.md | 类的定义 | beginner | class/struct、成员变量/函数、访问控制 | 设计简单类 | point.cpp |
| 06-02 | 02-constructors.md | 构造函数 | beginner | 默认/参数/拷贝构造、初始化列表、委托构造 | 实现多种构造函数 | constructors.cpp |
| 06-03 | 03-destructors.md | 析构函数与资源管理 | beginner | 析构函数调用时机、RAII 引入、Rule of Three 预告 | 资源管理类 | destructor.cpp |
| 06-04 | 04-static-members.md | static 成员 | beginner | static 变量/函数、类内初始化、单例模式预告 | 实现 ID 生成器 | static_demo.cpp |
| 06-05 | 05-friends.md | 友元 | beginner | friend 函数/类、友元的合理使用 | 运算符重载前置 | friend_demo.cpp |
| 06-06 | 06-this-and-cascading.md | this 指针与链式调用 | beginner | this 指针、链式调用模式、const 成员函数 | 实现 Builder 模式 | this_demo.cpp |

### ch07：运算符重载

- **难度**：intermediate
- **预计篇数**：3
- **核心知识点**：算术/比较/流运算符、下标运算符

#### 文章列表

| 编号 | 文件名 | 标题 | 难度 | 核心内容 | 练习重点 | 代码示例 |
|------|--------|------|------|---------|---------|---------|
| 07-01 | 01-arithmetic-comparison.md | 算术与比较运算符 | intermediate | +, -, *, /, ==, <, > 重载、对称性 | 实现 Fraction 类 | fraction.cpp |
| 07-02 | 02-io-subscript.md | 流与下标运算符 | intermediate | <<, >> 重载、operator[]、const 版本 | 实现可打印容器 | io_overload.cpp |
| 07-03 | 03-call-and-conversion.md | 函数调用与类型转换 | intermediate | operator()、operator bool()、explicit | 实现简单函数对象 | callable.cpp |

### ch08：继承与多态

- **难度**：intermediate
- **预计篇数**：5
- **核心知识点**：继承、虚函数、抽象类、多态

#### 文章列表

| 编号 | 文件名 | 标题 | 难度 | 核心内容 | 练习重点 | 代码示例 |
|------|--------|------|------|---------|---------|---------|
| 08-01 | 01-single-inheritance.md | 单继承 | intermediate | 基类/派生类、构造顺序、切片问题 | 设计继承层次 | inheritance.cpp |
| 08-02 | 02-virtual-functions.md | 虚函数与多态 | intermediate | virtual、override、vtable 原理、动态绑定 | 实现多态接口 | polymorphism.cpp |
| 08-03 | 03-abstract-classes.md | 抽象类与接口 | intermediate | 纯虚函数、抽象类设计、接口隔离 | 设计接口层次 | abstract.cpp |
| 08-04 | 04-multiple-inheritance.md | 多继承与虚继承 | intermediate | 多继承、菱形问题、虚继承 | 理解虚继承 | multi_inherit.cpp |
| 08-05 | 05-oop-in-practice.md | OOP 实战 | intermediate | 综合练习：图形绘制系统、继承 vs 组合 | 图形类设计 | shapes.cpp |

### ch09：模板初步

- **难度**：intermediate
- **预计篇数**：3
- **核心知识点**：函数模板、类模板、基本特化

#### 文章列表

| 编号 | 文件名 | 标题 | 难度 | 核心内容 | 练习重点 | 代码示例 |
|------|--------|------|------|---------|---------|---------|
| 09-01 | 01-function-templates.md | 函数模板 | intermediate | template<typename T>、实例化、类型推导 | 泛型函数编写 | func_template.cpp |
| 09-02 | 02-class-templates.md | 类模板 | intermediate | 类模板定义、成员函数、模板参数 | 实现泛型栈 | stack.hpp |
| 09-03 | 03-specialization-basics.md | 模板特化初步 | intermediate | 全特化、偏特化概念、何时特化 | 特化练习 | specialize.cpp |

### ch10：异常处理

- **难度**：intermediate
- **预计篇数**：3
- **核心知识点**：try/catch、异常安全、RAII

#### 文章列表

| 编号 | 文件名 | 标题 | 难度 | 核心内容 | 练习重点 | 代码示例 |
|------|--------|------|------|---------|---------|---------|
| 10-01 | 01-try-catch.md | 异常基础 | intermediate | try/catch/throw、标准异常层次、异常类型 | 异常捕获与处理 | exceptions.cpp |
| 10-02 | 02-exception-safety.md | 异常安全 | intermediate | 异常安全等级、RAII 守卫、noexcept | 异常安全的资源管理 | safety.cpp |
| 10-03 | 03-error-handling-comparison.md | 错误处理方式对比 | intermediate | 异常 vs 错误码 vs optional/expected 预告 | 选择合适的错误处理方式 | error_cmp.cpp |

### ch11：STL 初见

- **难度**：beginner → intermediate
- **预计篇数**：4
- **核心知识点**：vector、map、algorithm 快速上手

#### 文章列表

| 编号 | 文件名 | 标题 | 难度 | 核心内容 | 练习重点 | 代码示例 |
|------|--------|------|------|---------|---------|---------|
| 11-01 | 01-vector.md | std::vector 快速上手 | beginner | 增删改查、容量管理、遍历 | vector 实践 | vector_demo.cpp |
| 11-02 | 02-map-set.md | 关联容器快速上手 | beginner | map/set/unordered_map、查找/插入 | 词频统计 | map_demo.cpp |
| 11-03 | 03-algorithms-intro.md | 算法库初见 | beginner | sort/find/copy/transform、lambda 配合 | 数据处理练习 | algo_demo.cpp |
| 11-04 | 04-stl-patterns.md | STL 常用模式 | beginner | 容器选择指南、常见陷阱、性能基础 | 综合练习 | patterns.cpp |

### ch12：内存模型基础

- **难度**：intermediate
- **预计篇数**：3
- **核心知识点**：栈/堆/静态区、new/delete、内存对齐

#### 文章列表

| 编号 | 文件名 | 标题 | 难度 | 核心内容 | 练习重点 | 代码示例 |
|------|--------|------|------|---------|---------|---------|
| 12-01 | 01-memory-layout.md | 内存布局 | intermediate | 栈/堆/静态区/代码段、变量存储位置分析 | 分析变量位置 | layout.cpp |
| 12-02 | 02-new-delete.md | 动态内存管理 | intermediate | new/delete、new[]/delete[]、内存泄漏、RAII 再述 | 内存泄漏检测 | dynamic.cpp |
| 12-03 | 03-alignment-padding.md | 内存对齐与填充 | intermediate | 对齐规则、sizeof 计算、alignas/alignof | 结构体大小计算 | alignment.cpp |

## 练习与项目

### 文章末尾练习
- 每篇文章末尾 2-3 道小练习
- 练习类型：填空、改错、编写小程序、分析输出
- 每道练习提供参考答案

### 贯穿小项目
1. **学生管理系统**（ch01-ch06 后）：综合练习类型、函数、类、容器
2. **简易计算器**（ch07-ch08 后）：运算符重载、继承、多态
3. **文本处理工具**（ch09-ch12 后）：模板、异常、STL、文件操作

### 代码示例组织
- 简单示例内嵌在 Markdown 中
- 完整示例放 `code/vol1-fundamentals/` 下，按章节组织
- 每个 CMake 项目可独立编译运行

## 与其他卷的交叉引用

- **→ 卷二**：ch04 智能指针预告 → 卷二 ch01 详细讲解
- **→ 卷二**：ch03 constexpr 预告 → 卷二 ch02 深入
- **→ 卷三**：ch11 STL 初见 → 卷三 全部章节深入
- **→ 卷四**：ch09 模板初步 → 卷四 ch05 模板元编程 4 卷
- **→ 卷七**：ch00 环境搭建 → 卷七 ch00 CMake 深入

## 现有内容映射

| 现有文章 | 重写去向 | 备注 |
|----------|---------|------|
| core-embedded-cpp/ch00/00-preface.md | ch00/00-preface.md | 全面重写，去掉嵌入式限定 |
| core-embedded-cpp/ch00/01-resource-constraints.md | vol8-domains/embedded/ | 移至嵌入式领域 |
| core-embedded-cpp/ch00/02-c-language-crash-course.md | ch01 | 重写为类型系统 |
| core-embedded-cpp/ch00/03-cpp98-basics.md | ch06 | 重写为类基础 |
| core-embedded-cpp/ch00/04-when-to-use-cpp.md | ch00/00-preface.md | 融入前言 |
| core-embedded-cpp/ch00/05-language-choice.md | ch00/00-preface.md | 融入前言 |
| core-embedded-cpp/ch00/06-evaluating-performance.md | vol6-performance/ | 移至性能卷 |
