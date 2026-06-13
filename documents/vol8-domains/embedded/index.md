---
title: "嵌入式开发"
description: "现代 C++ 在嵌入式系统中的实战应用"
platform: stm32f1
tags:
  - cpp-modern
  - intermediate
  - stm32f1
---

# 嵌入式开发

> 现代 C++ 在资源受限的嵌入式系统里能做什么、不能做什么——从零开销抽象、内存管理到外设编程、中断并发，再到 STM32 实战与 RTOS。

## STM32F1 实战系列

这是一条从零开始、用现代 C++ 写 STM32 的完整路线，按 "环境 → LED → 按键 → 串口" 的顺序展开，每个外设都从 C 起步，一路重构到 C++23：

- [开发环境搭建](00-env-setup/index.md) — 工具链、项目结构、CMake、WSL2 USB 透传、GDB 调试
- [LED 点灯：从 C 到 C++ 的演进](01-led/index.md) — 从 HAL 寄存器到模板与 `if constexpr`
- [按键输入：消抖、状态机与类型安全](02-button/index.md) — 从轮询到 `variant`/`concepts`
- [UART 串口通信](03-uart/index.md) — 从协议到中断驱动、`std::expected` 错误处理

## 嵌入式专题文章

这些是从旧教程迁移来的专题文章，覆盖零开销抽象、内存管理、寄存器访问、中断安全等主题，可作为实战系列的横向补充：

<ChapterNav variant="sub">
  <ChapterLink href="01-zero-overhead-abstraction">嵌入式现代 C++ 教程——零开销抽象</ChapterLink>
  <ChapterLink href="01-resource-and-realtime-constraints">嵌入式的资源与实时约束</ChapterLink>
  <ChapterLink href="01-dynamic-allocation-issues">动态内存的代价：碎片化与不确定性</ChapterLink>
  <ChapterLink href="02-static-and-stack-allocation">嵌入式 C++ 教程——静态存储与栈上分配策略</ChapterLink>
  <ChapterLink href="03-object-pool-pattern">嵌入式 C++ 教程：对象池模式</ChapterLink>
  <ChapterLink href="04-crtp-vs-runtime-polymorphism">编译期多态 vs 运行时多态</ChapterLink>
  <ChapterLink href="04-placement-new">嵌入式 C++ 教程：placement new</ChapterLink>
  <ChapterLink href="05-fixed-pool-allocation">嵌入式 C++ 教程：Slab / Arena 实现与比较</ChapterLink>
  <ChapterLink href="05-etl">嵌入式 C++ 教程——ETL</ChapterLink>
  <ChapterLink href="05-interrupt-safe-coding">中断安全的代码编写</ChapterLink>
  <ChapterLink href="03-circular-buffer">循环缓冲区</ChapterLink>
  <ChapterLink href="04-intrusive-containers">侵入式容器设计</ChapterLink>
  <ChapterLink href="02-type-safe-register-access">类型安全的寄存器访问</ChapterLink>
  <ChapterLink href="core-embedded-cpp-index">目录</ChapterLink>
</ChapterNav>
