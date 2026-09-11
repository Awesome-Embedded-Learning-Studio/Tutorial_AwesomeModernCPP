---
title: "没有堆的世界:先把内存发对"
description: "无堆内核的第一块真代码为什么是内存:先想清楚发内存的办法与位图的记账直觉,再把位图、定长块池、Make/Destroy 门面逐篇写出来,防线写进编译期,最后搬上 -nostdlib 目标连撞三道门"
chapter: 1
order: 0
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - 内存管理
  - expected
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
---

# 没有堆的世界:先把内存发对

> 状态:陆续上线

## 概述

无堆、无异常、无 RTTI 的内核里,任务控制块和消息住哪?这是冒烟留下的第一个真问题。这一站咱们先想清楚:发内存有哪些办法、为什么选定长块池、位图这种记账方式好在哪里;然后逐篇动手——位图、池、类型化门面,每篇一个新机制、一道测试门,防线用负向编译测试写进编译期;最后把池子搬上 `-nostdlib` 目标,连撞三道门。文末留下一个"加 `constinit` 也编不过"的悬念,到下一主题开头用一对方括号了结。

## 站内文章

<ChapterNav variant="sub">
  <ChapterLink href="01-why-pool-and-bitmap">没有堆的世界:内存怎么发,位图是什么</ChapterLink>
  <ChapterLink href="02-write-the-bitmap">把位图写出来:定容量的 Bitmap</ChapterLink>
  <ChapterLink href="03-block-pool">定长块池:一块一个 bit 的分配器</ChapterLink>
  <ChapterLink href="04-make-destroy-and-guards">Make/Destroy:类型化的出生入死与编译期防线</ChapterLink>
</ChapterNav>

下一篇咱们撞 -nostdlib 的墙:memset 失踪、全局构造没人埋单、`__cxa_guard_*` 无处安放,三道门一道道过。陆续上线。
