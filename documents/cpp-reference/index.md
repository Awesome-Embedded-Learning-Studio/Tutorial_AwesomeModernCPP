---
title: "C++ 特性参考卡"
description: "C++ 核心特性的速查参考卡集合，基于 cppreference 内容改编"
chapter: 99
order: 1
tags:
  - host
  - cpp-modern
  - 入门
difficulty: beginner
---

# C++ 特性参考卡

精炼的结构化速查页，覆盖 Modern C++ 核心特性。每张参考卡 1 分钟可扫完：核心 API 签名、最小可编译示例、嵌入式适用性、编译器支持。

> 遇到某个特性想快速查语法？来这里。想系统学习？去看对应卷的教程文章。

## 分类索引

### 内存管理

- [std::unique_ptr](memory/01-unique-ptr.md) — 独占所有权智能指针
- [std::shared_ptr](memory/02-shared-ptr.md) — 共享所有权智能指针
- [std::optional](memory/03-optional.md) — 可选值包装

### 容器与视图

- [std::span](containers/01-span.md) — 连续序列的非拥有视图
- [std::string_view](containers/02-string-view.md) — 字符串的非拥有视图
- [std::variant](containers/03-variant.md) — 类型安全的联合体

### 核心语言特性

- [constexpr / consteval / constinit](core-language/01-constexpr.md) — 编译期计算
- [lambda 表达式](core-language/02-lambda.md) — 匿名函数对象
- [auto / decltype](core-language/03-auto-decltype.md) — 类型推导

### 模板与元编程

- [Concepts](templates/01-concepts.md) — 编译期模板参数约束

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
