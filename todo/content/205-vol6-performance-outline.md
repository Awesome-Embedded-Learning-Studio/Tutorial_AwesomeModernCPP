---
id: "205"
title: "卷六：性能优化 — 全部章节大纲与文章规划"
category: content
priority: P2
status: pending
created: 2026-04-16
assignee: charliechen
depends_on: ["200", "201", "202"]
blocks: []
estimated_effort: large
---

# 卷六：性能优化 — 全部章节大纲与文章规划

## 总览

- **卷名**：vol6-performance
- **难度范围**：advanced
- **预计文章数**：18-22 篇
- **前置知识**：卷一 + 卷二 + 卷三
- **C++ 标准覆盖**：C++11-23
- **目录位置**：`documents/vol6-performance/`

## 章节大纲

### ch00：性能思维

- **预计篇数**：2

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 00-01 | 01-perf-philosophy.md | 性能哲学 | 度量先行、过早优化陷阱、性能预算、Big O 实际影响 | 性能分析思维 |
| 00-02 | 02-measurement-methodology.md | 度量方法论 | 微基准 vs 宏基准、统计方法、回归检测 | 设计 benchmark |

### ch01：CPU 缓存优化

- **预计篇数**：4

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 01-01 | 01-cache-hierarchy.md | 缓存层级 | L1/L2/L3、缓存行(64B)、TLB、页表 | 缓存感知编程 |
| 01-02 | 02-data-locality.md | 数据局部性 | 时间/空间局部性、AoS vs SoA、结构体分裂 | 数据布局重构 |
| 01-03 | 03-false-sharing.md | 伪共享 | 缓存行争用、alignas 避免伪共享、NUMA 简介 | 多线程缓存优化 |
| 01-04 | 04-prefetching.md | 预取与缓存友好的算法 | 软件预取、缓存感知算法、分块(tiling) | 矩阵乘法优化 |

### ch02：SIMD/AVX 向量化

- **预计篇数**：3

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 02-01 | 01-simd-concepts.md | SIMD 概念 | SIMD 寄存器、SSE/AVX/AVX2/AVX-512、自动向量化条件 | 向量化条件分析 |
| 02-02 | 02-intrinsics.md | Intrinsics 编程 | __m128/__m256、算术/比较/洗牌操作 | SIMD 向量运算 |
| 02-03 | 03-autovectorization.md | 自动向量化 | 编译器提示、restrict、循环优化、-march=native | 引导编译器向量化 |

### ch03：性能分析工具

- **预计篇数**：3

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 03-01 | 01-perf.md | Linux perf | perf record/report/stat、火焰图、硬件计数器 | 性能热点分析 |
| 03-02 | 02-valgrind.md | Valgrind 工具套件 | memcheck/callgrind/cachegrind/massif | 内存与缓存分析 |
| 03-03 | 03-vtune.md | Intel VTune | 热点分析、微架构探索、线程分析 | 深度性能分析 |

### ch04：汇编阅读

- **预计篇数**：3

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 04-01 | 01-x86-basics.md | x86_64 基础 | 寄存器、指令格式、调用约定、栈帧 | 阅读 GCC 输出 |
| 04-02 | 02-reading-compiler-output.md | 阅读编译器输出 | -S -O2 输出、优化变换识别、内联分析 | 分析优化效果 |
| 04-03 | 03-arm-assembly.md | ARM 汇编简介 | AArch64 基础、与 x86 对比、移动/嵌入式场景 | 交叉编译分析 |

### ch05：优化模式

- **预计篇数**：3

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 05-01 | 01-data-layout.md | 数据布局优化 | SoA/AoS/AoSoA、对齐填充、热/冷数据分离 | 结构体优化 |
| 05-02 | 02-branch-optimization.md | 分支优化 | 分支预测、likely/unlikely、查找表替代、bimodal | 减少分支 |
| 05-03 | 03-loop-optimization.md | 循环优化 | 展开交换、向量化条件、循环不变量外提 | 循环性能提升 |

### ch06：内存优化

- **预计篇数**：2

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 06-01 | 01-alignment-pool.md | 对齐与池分配 | alignas、内存池设计、arena allocator | 自定义分配器 |
| 06-02 | 02-memory-compression.md | 内存压缩与紧凑存储 | 位域、差分编码、紧凑指针、字符串压缩 | 减少内存占用 |

### ch07：基准测试

- **预计篇数**：2

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 07-01 | 01-google-benchmark.md | Google Benchmark | 框架使用、参数化、统计报告、do_not_optimize | 编写 benchmark |
| 07-02 | 02-nanobench.md | nanobench 与微基准 | 轻量级基准、cycles 测量、正确测量方法 | 微观性能测量 |

## 练习与项目

### 实战项目
1. **矩阵乘法优化**：从朴素实现到分块+SIMD
2. **字符串处理优化**：缓存友好的字符串搜索
3. **自定义分配器性能测试**：对比 malloc/pool/arena

## 现有内容映射

| 现有文章 | 重写去向 | 备注 |
|----------|---------|------|
| documents/parallel-computing/01-avx-vectorization.md | ch02 SIMD | 扩展为 3 篇 |
| core-embedded-cpp/ch00/06-evaluating-performance.md | ch00 | 通用化重写 |
| core-embedded-cpp/ch02/02-inline-and-compiler-optimization.md | ch04+ch05 | 融入 |
