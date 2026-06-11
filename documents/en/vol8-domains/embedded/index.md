---
title: Embedded Development
description: Practical applications of modern C++ in embedded systems
platform: stm32f1
tags:
- cpp-modern
- intermediate
- stm32f1
translation:
  source: documents/vol8-domains/embedded/index.md
  source_hash: c9dc7bda40253cbd57d2c50d58741938ec30d505ad73a45c789c284f51f51ee1
  translated_at: '2026-05-26T12:23:33.146275+00:00'
  engine: anthropic
  token_count: 352
---
# Embedded Development

> Status: Planned

## Overview

This subdomain covers the application of modern C++ in embedded systems, including resource constraints, zero-overhead abstraction, memory management, peripheral programming, interrupt concurrency, STM32 hands-on projects, RTOS, and more.

An estimated 45 to 50 articles.

## Chapter Navigation

> Content is being written, stay tuned.

## Existing Content (To Be Rewritten)

This directory contains a large number of embedded-related articles and an STM32F1 hands-on series migrated from an older tutorial. These will be comprehensively rewritten in the future.

### Article List

<ChapterNav variant="sub">
  <ChapterLink href="01-zero-overhead-abstraction">嵌入式现代 C++ 教程——零开销抽象</ChapterLink>
  <ChapterLink href="01-resource-and-realtime-constraints">嵌入式的资源与实时约束</ChapterLink>
  <ChapterLink href="01-dynamic-allocation-issues">动态内存的代价：碎片化与不确定性</ChapterLink>
  <ChapterLink href="02-static-and-stack-allocation">嵌入式 C++ 教程——静态存储与栈上分配策略</ChapterLink>
  <ChapterLink href="03-object-pool-pattern">嵌入式 C++ 教程：对象池模式</ChapterLink>
  <ChapterLink href="04-crtp-vs-runtime-polymorphism">编译期多态 vs 运行时多态</ChapterLink>
  <ChapterLink href="04-empty-base-optimization">空基类优化（EBO）：C++ 的瘦身技巧</ChapterLink>
  <ChapterLink href="04-placement-new">嵌入式 C++ 教程：placement new</ChapterLink>
  <ChapterLink href="05-fixed-pool-allocation">嵌入式 C++ 教程：Slab / Arena 实现与比较</ChapterLink>
  <ChapterLink href="05-etl">嵌入式 C++ 教程——ETL</ChapterLink>
  <ChapterLink href="05-interrupt-safe-coding">中断安全的代码编写</ChapterLink>
  <ChapterLink href="06-array-vs-raw-arrays">嵌入式 C++ 教程——std::array vs C 数组</ChapterLink>
  <ChapterLink href="03-circular-buffer">循环缓冲区</ChapterLink>
  <ChapterLink href="04-intrusive-containers">侵入式容器设计</ChapterLink>
  <ChapterLink href="02-type-safe-register-access">类型安全的寄存器访问</ChapterLink>
  <ChapterLink href="core-embedded-cpp-index">目录</ChapterLink>
</ChapterNav>
