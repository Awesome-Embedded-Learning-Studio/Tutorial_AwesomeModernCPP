---
title: "卷六：性能优化"
description: "从测量方法论到 CPU 微架构,从按瓶颈部位优化到 C++ 抽象的性能成本"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
---

# 卷六：性能优化

性能优化是 C++ 工程能力里最容易「自信地犯错」的一块——微架构的复杂度远远跑在人的直觉前面。本卷的脊柱是一条:**先正确(正确性地基)→ 先测量(Benchmark 方法论锚点)→ 按瓶颈部位归因与优化(TMA 四桶)→ 落到 C++ 抽象的性能成本**。每个主题都走「C++ 代码切入 → 下沉硬件/方法论 → 回到 C++ 怎么改」的环路。

一句话总命题贯穿全卷:**efficiency(算法复杂度)≠ performance(硬件上的真实表现)。** 别只看 big-O,要看数据在硬件上怎么流。

> 本卷已按八章结构稳定下来(2026-07-03:历史散篇 02-inline / avx 删、06-evaluating 属嵌入式已移、sanitizer 三篇归位 ch00-03/04/05)。

## 章节导航

<ChapterNav variant="sub">
  <ChapterLink href="ch00-performance-mindset">ch00 · 性能思维与正确性前置</ChapterLink>
  <ChapterLink href="ch01-benchmark-methodology">ch01 · Benchmark 方法论【全卷锚点】</ChapterLink>
  <ChapterLink href="ch02-cpu-microarchitecture">ch02 · CPU 微架构与存储层次</ChapterLink>
  <ChapterLink href="ch03-attribution-methodology">ch03 · 归因方法论:从测量到瓶颈</ChapterLink>
  <ChapterLink href="ch04-tuning-by-bottleneck">ch04 · 按瓶颈部位优化【技术主体】</ChapterLink>
  <ChapterLink href="ch05-multicore-performance">ch05 · 多核性能(承接 vol5)</ChapterLink>
  <ChapterLink href="ch06-cpp-abstraction-cost">ch06 · C++ 抽象的性能成本</ChapterLink>
  <ChapterLink href="ch07-compiler-and-size">ch07 · 编译器优化边界与体积评估</ChapterLink>
</ChapterNav>
