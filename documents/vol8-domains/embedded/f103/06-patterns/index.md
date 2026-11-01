---
title: "模式:对象池、侵入式容器、中断安全"
description: "日志组件一条线的完整实录:零开销设计、Formatter 分发选型、栈账与体积实测、map 审判,第七篇收用法面——两种写法与两个观看面怎么用"
chapter: 6
order: 0
tags:
  - stm32f1
  - intermediate
  - 对象池
  - 侵入式容器
difficulty: intermediate
platform: stm32f1
---

# 模式:对象池、侵入式容器、中断安全

## 概述

这一站目前落地的是日志组件一条线:前六篇从 source_location 撞墙、Formatter 分发选型、栈账实测、两行旗子砍体积,一路走到 map 审判的完整实录;第七篇收用法面,日志的两种写法与两个观看面怎么用。对象池、侵入式容器、中断安全这些惯用法篇目,等素材成熟再补。

## 章节导航

<ChapterNav variant="sub">
  <ChapterLink href="01-logger-zero-overhead">零开销日志组件完整踩坑记录:从 source_location 撞墙到反汇编验收</ChapterLink>
  <ChapterLink href="02-dispatch-overload-to-formatter">从重载集到 Formatter 特化:一次嵌入式日志分发机制的选型讨论</ChapterLink>
  <ChapterLink href="03-logger-stack-budget">日志组件的栈账实测:函数局部的 LineBuffer 到底会不会打穿栈</ChapterLink>
  <ChapterLink href="04-two-flags-size-cut">两行编译旗子砍掉 64% 固件:-ffunction-sections 的函数粒度回收实录</ChapterLink>
  <ChapterLink href="05-map-is-the-verdict">终审在 map:『裁剪级别连字符串都不进固件』的完整证据链</ChapterLink>
  <ChapterLink href="06-zero-overhead-price-list">零开销的价格表:18200 字节日志固件的解剖</ChapterLink>
  <ChapterLink href="07-logger-dual-syntax-usage">把日志用起来:两种写法与两个观看面</ChapterLink>
</ChapterNav>
