---
chapter: 0
difficulty: intermediate
order: 0
platform: stm32f1
reading_time_minutes: 3
tags:
- cpp-modern
- intermediate
- stm32f1
title: 目录
description: ''
---
# 目录

这里是《面向嵌入式教程学习的现代C++教程》目录，点击我直接跳转到对应章节即可

## Chapter 0 - 前言与基础准备

- [前言](../../vol1-fundamentals/00-preface.md)
- [嵌入式的资源与实时约束](./01-resource-and-realtime-constraints.md)
- [急速C语言速通复习](../../vol1-fundamentals/02-c-language-crash-course.md)
- [C++98入门：命名空间、引用与作用域解析](../../vol1-fundamentals/03A-cpp98-namespace-reference.md)
- [C++98函数接口：重载与默认参数](../../vol1-fundamentals/03B-cpp98-function-overload-default-args.md)
- [C++98面向对象：类与对象深度剖析](../../vol1-fundamentals/03C-cpp98-classes-and-objects.md)
- [C++98面向对象：继承与多态](../../vol1-fundamentals/03D-cpp98-inheritance-polymorphism.md)
- [C++98运算符重载](../../vol1-fundamentals/03E-cpp98-operator-overloading.md)
- [C++98进阶：类型转换、动态内存与异常处理](../../vol1-fundamentals/03F-cpp98-casts-memory-exceptions.md)
- [何时用 C++、用哪些 C++ 特性（折中与禁用项）](../../vol1-fundamentals/04-when-to-use-cpp.md)
- [语言选择原则：性能 vs 可维护性的真实取舍](../../vol1-fundamentals/05-language-choice-performance-vs-maintainability.md)
- [C++一定导致代码膨胀嘛？](../../vol6-performance/06-evaluating-performance-and-size.md)

## Chapter 1 - 构建工具链

- [随意聊下交叉编译和CMake简单指南](../../vol7-engineering/01-cross-compilation-and-cmake.md)
- [常见编译器选项指南](../../vol7-engineering/02-compiler-options.md)
- [链接器与链接器脚本](../../vol7-engineering/03-linker-and-linker-scripts.md)

## Chapter 2 - 零开销抽象

- [零开销抽象](./01-zero-overhead-abstraction.md)
- [内联与编译器优化](../../vol6-performance/02-inline-and-compiler-optimization.md)
- [CRTP VS 运行时多态，你们知道吗？](./04-crtp-vs-runtime-polymorphism.md)

## Chapter 3 - 内存与对象管理

- [初始化列表](../../vol3-standard-library/11-initializer-lists.md)
- [空基类优化（EBO）](../../vol4-advanced/03-empty-base-optimization.md)
- [对象大小，平凡类型](../../vol3-standard-library/12-object-size-and-trivial-types.md)

## Chapter 4 - 编译期计算

- [if constexpr](../../vol4-advanced/vol3-metaprogramming-cpp20-23/index.md)

## Chapter 5 - 内存管理策略

- [动态分配问题](./01-dynamic-allocation-issues.md)
- [静态存储与栈上分配策略](./02-static-and-stack-allocation.md)
- [对象池模式](./03-object-pool-pattern.md)
- [禁用 heap 或限制 heap 时的替代策略：放置new（Placement New）的使用](./04-placement-new.md)
- [固定池分配](./05-fixed-pool-allocation.md)

## Chapter 7 - 容器与数据结构

- [array](../../vol3-standard-library/02-array.md)
- [span](../../vol3-standard-library/08-span.md)
- [循环缓冲区](./03-circular-buffer.md)
- [侵入式容器设计](./04-intrusive-containers.md)
- [ETL](./05-etl.md)
- [自定义的分配器](../../vol3-standard-library/13-custom-allocators.md)

## Chapter 8 - 类型安全与工具类型

- [类型安全的寄存器访问](./02-type-safe-register-access.md)

## Chapter 10 - 并发与原子操作

- [atomic](../../vol5-concurrency/ch03-atomic-memory-model/01-atomic-operations.md)
- [memory_order](../../vol5-concurrency/ch03-atomic-memory-model/02-memory-ordering.md)
- [无锁数据结构设计](../../vol5-concurrency/ch04-concurrent-data-structures/03-lock-free-basics.md)
- [mutex与RAII守卫](../../vol5-concurrency/ch02-mutex-condition-sync/01-mutex-and-raii-guards.md)
- [中断安全的代码编写](./05-interrupt-safe-coding.md)
- [临界区保护技术](./05-interrupt-safe-coding.md)

## Chapter 11 - 现代C++特性速览

- [三路比较运算符](../../vol4-advanced/05-spaceship-operator.md)

## Chapter 12 - 模板基础

- [模板基础（C++11-14）](../../vol4-advanced/vol1-basics-cpp11-14/index.md)
- [现代模板技术（C++17）](../../vol4-advanced/vol2-modern-cpp17/index.md)
- [元编程精要（C++20-23）](../../vol4-advanced/vol3-metaprogramming-cpp20-23/index.md)
- [泛型设计模式实战](../../vol4-advanced/vol4-generics-patterns/index.md)
