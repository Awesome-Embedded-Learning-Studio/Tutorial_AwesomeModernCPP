---
sidebar_order: 0
title: "并发世界观与第一件工具"
description: "从三原则与卷地图出发，用 ThreadSanitizer 抓住第一个数据竞争，再给 OS 线程的开销标个价"
---

# 并发世界观与第一件工具

咱们这一卷不从 `std::thread` 起手。多数教程一开场就领着读者开线程，咱们更愿意提前把思维方式立起来：为什么值得并发，出了问题长什么样，咱们开一条线程又得付多少代价。等三个问题都有了着落，后面的每一行并发代码才有判断力撑腰。写在卷首的三条原则——`先正确性再性能`、`先锁再无锁`、`先同步再任务`——往后每一章的取舍都照它们来，本章咱们就把它们逐条讲明白。

第一篇讲世界观与卷地图：Amdahl 定律用纯数学把并行的收益上限算得明明白白，整卷八章怎么走，一张地图就都看全了。第二篇是工具第一课：数据竞争是并发里的头号杀手，咱们拿 ThreadSanitizer 当第一件工具，既学会读它的报告，也认清它的边界。第三篇咱们给 OS 线程标价：创建、切换、内存足迹咱们逐项过目，您拿 perf stat 一跑，开销就摆在咱们眼前。

## 本章内容

<ChapterNav variant="sub">
  <ChapterLink href="01-why-concurrency">为什么并发：世界观与卷地图</ChapterLink>
  <ChapterLink href="02-data-race-and-tsan">数据竞争与 ThreadSanitizer 第一课</ChapterLink>
  <ChapterLink href="03-os-threads-and-cost">OS 线程与开销</ChapterLink>
</ChapterNav>
