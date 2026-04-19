---
title: "卷二：现代 C++ 特性"
description: "C++11-17 核心特性深入讲解"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
---

# 卷二：现代 C++ 特性

> 状态：部分内容已有（待重写）

## 概述

本卷覆盖 C++11/14/17 核心现代特性。

## 现有文章（待重写为通用内容）

### 移动语义

- [移动语义](02-move-semantics.md)
- [RVO 与 NRVO](03-rvo-nrvo.md)

### 智能指针与 RAII

- [RAII 在外设管理中的应用](01-raii-in-peripheral-management.md)
- [unique_ptr](02-unique-ptr.md)
- [shared_ptr](03-shared-ptr.md)
- [智能指针嵌入式权衡](04-smart-ptr-embedded-tradeoffs.md)
- [侵入式指针与引用计数](05-intrusive-ptr-and-ref-counting.md)
- [自定义删除器](06-custom-deleter.md)
- [引用计数](07-reference-counting.md)
- [scope_guard](08-scope-guard.md)

### constexpr 与编译期

- [constexpr](03-constexpr.md)
- [constexpr 与设计技术](01-constexpr-and-design-techniques.md)
- [consteval 与 constinit](02-consteval-constinit.md)
- [编译期计算实战](03-compile-time-in-practice.md)

### Lambda 与函数式

- [Lambda 基础](01-lambda-basics.md)
- [Lambda 捕获与性能](02-lambda-capture-and-performance.md)
- [std::function vs 函数指针](03-std-function-vs-function-ptr.md)
- [零开销回调](04-zero-overhead-callbacks.md)
- [std::invoke 与可调用对象](05-std-invoke-and-callables.md)
- [函数式错误处理](06-functional-error-handling.md)
- [Ranges 基础与视图](07-ranges-basics-and-views.md)
- [Ranges 管道实战](08-ranges-pipeline-in-practice.md)

### 类型安全

- [enum class](01-enum-class.md)
- [variant](03-variant.md)
- [optional](04-optional.md)
- [expected](05-expected.md)
- [类型别名与 using](06-type-aliases-and-using.md)

### 其他特性

- [auto 与 decltype](01-auto-and-decltype.md)
- [结构化绑定](02-structured-bindings.md)
- [range-for 优化](03-range-based-for-optimization.md)
- [属性](04-attributes.md)
- [指定初始化器](06-designated-initializers.md)
- [用户自定义字面量](07-user-defined-literals.md)
- [字面量运算符与自定义单位](07-literal-operators-and-custom-units.md)
- [string_view](cpp17-string-view.md)
