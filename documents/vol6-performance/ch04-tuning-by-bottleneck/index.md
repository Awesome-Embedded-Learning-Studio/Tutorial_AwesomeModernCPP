---
title: "按瓶颈部位优化"
description: "ch04 是 vol6 的技术主体,对齐 TMAM 四桶:Backend Memory(cache-friendly、AoS/SoA、prefetch)、Backend Core(循环优化、数据类型与算术、inline+去虚拟化、SIMD)、Bad Speculation(branchless)、Frontend(代码布局、PGO、BOLT),每篇都配本机实测"
---

# 按瓶颈部位优化

ch03 教我们用 USE / Roofline / TMAM / 火焰图把瓶颈归到流水线的某个桶。这一章是对症下药:对着四个桶,逐个讲怎么治。这是 vol6 的**技术主体**,也是整卷最长的一章。

四桶对应七篇:

- **Backend Memory**(04-01):cache-friendly、AoS→SoA、prefetch。单线程最大杠杆,AoS→SoA 实测快近 10 倍。
- **Backend Core**(04-02 / 04-03 / 04-04 / 04-05):循环优化、数据类型与算术、inline+去虚拟化、SIMD。除法是 5 倍于乘法的瓶颈、SIMD 实测 ~20×、虚函数 vs CRTP 2.5×。
- **Bad Speculation**(04-06):branchless 与 predication,核心是「别盲目无分支」。
- **Frontend**(04-07):代码布局、PGO、BOLT。

贯穿全章的精神只有一句:**现代编译器 + 硬件替你做了大半优化,你的工作不是「使劲手写 trick」,而是「测出真瓶颈、精准改、改完用同一套方法论验证」**。这一章里有好几个诚实的结果:switch 不总比 if-else 快、final 没自动去虚化、FP 归约不自动向量化、branchless 在 -O2 下和 if 一样快、PGO 对微基准无收益。它们都是这句话的注脚。性能优化是测量驱动的精准手术,不是堆 trick。

> 边界提醒:本章只讲「**P**——在硬件上跑时怎么改更快」。「**D**——为什么 vector/string 这么设计」「**U**——怎么用对容器」归 vol3;「EBO/SSO 机制」归 vol4;「怎么写无锁/同步原语」归 vol5。ch04-01 动笔前已与 vol3 切边,讲布局性能不重复造机制。

## 本章内容

<ChapterNav variant="sub">
  <ChapterLink href="04-01-backend-memory">后端内存瓶颈:cache-friendly、AoS/SoA 与 prefetch</ChapterLink>
  <ChapterLink href="04-02-loop-and-compute">循环与计算优化:code motion、消除内存引用与多累加器</ChapterLink>
  <ChapterLink href="04-03-types-and-arithmetic">数据类型与算术:整数/浮点、除法瓶颈与跳转表</ChapterLink>
  <ChapterLink href="04-04-inline-devirt-compiler">inline、去虚拟化与编译器优化全景</ChapterLink>
  <ChapterLink href="04-05-simd">SIMD 与向量化:自动向量化条件、intrinsics 与 CPU 分发</ChapterLink>
  <ChapterLink href="04-06-branch-branchless">分支:branchless、predication 与「别盲目无分支」</ChapterLink>
  <ChapterLink href="04-07-frontend-pgo">前端优化:代码布局、PGO 与 BOLT</ChapterLink>
</ChapterNav>
