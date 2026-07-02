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

> 本卷正在按这套骨架系统化重写,部分历史散篇(内联、AVX、体积评估、sanitizer 三篇)在归位中,暂列在下方导航。

## 章节导航

<ChapterNav variant="sub">
  <ChapterLink href="ch00-performance-mindset">ch00 · 性能思维与正确性前置</ChapterLink>
  <ChapterLink href="ch01-benchmark-methodology">ch01 · Benchmark 方法论【全卷锚点】</ChapterLink>
  <ChapterLink href="02-inline-and-compiler-optimization">内联与编译器优化</ChapterLink>
  <ChapterLink href="avx-avx2-deep-dive">AVX/AVX2 深度介绍</ChapterLink>
  <ChapterLink href="06-evaluating-performance-and-size">性能与体积评估</ChapterLink>
  <ChapterLink href="10-asan-family-and-memory-safety">ASan 工具家族与内存安全</ChapterLink>
  <ChapterLink href="11-memory-safety-asan-valgrind">Valgrind 与 ASan 对照:JIT 解释 vs 编译期插桩</ChapterLink>
  <ChapterLink href="12-sanitizer-toolchain-and-memory-safety">Sanitizer 工具链全景:从 -fsanitize 到内核 KASAN/KFENCE</ChapterLink>
</ChapterNav>
