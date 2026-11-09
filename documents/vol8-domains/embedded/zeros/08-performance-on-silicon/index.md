---
title: "真硅秒表:切换到底多贵"
description: "整章上真硅:时钟树爬上 72MHz、DWT 周期计数器当秒表,量出第一刀切换;.ramfunc 搬 RAM 被四格实验矩阵实测证伪并回滚成宏开关,总账 489 到 333 周期;拔掉心跳的变异性测试挖出两个潜伏 bug;测量方法论本身成了交付物——探针以 patch 随库,忘了 apply 就响亮报错"
chapter: 8
order: 0
tags:
  - stm32f1
  - advanced
  - 嵌入式
  - 优化
difficulty: advanced
platform: stm32f1
cpp_standard: [23]
---

# 真硅秒表:切换到底多贵

> 咱们写完一篇发一篇,本章三篇全部上线了。

::: tip 需要真正的开发板
本章的周期数只在真正的开发板上验过(Blue Pill + ST-Link/OpenOCD),笔者的实测记录是仓库 documents 里的那本实验册。没有板子的读者照样读,每个数字都标了出处和口径,只是您暂时复现不了。仿真器是给不出周期数的,这件事咱们在正文里也实证了一遍。
:::

## 概述

有了 CI 的几道检查,咱们才配诚实地回答那个从头悬着的问题:一次上下文切换的成本,到底多贵?本章咱们整章上了真硅。头一篇咱们手动爬了时钟树(HSE 8MHz 到 PLL×9),顺带当场对质了仿真器里那个假 RCC,再把 DWT 的周期计数器装成了秒表,量出了第一刀切换的报表。第二篇讲的是一次反转,“教科书说 RAM 快”的说法被四格实验矩阵实测证伪了,.ramfunc 回滚成了宏开关,全章的合计从 489 个周期走到了 333,咱们还学会了写一本诚实的实验册。第三篇咱们拔掉心跳做变异性测试,挖出了潜伏已久的 bug:任务返回后的 Trap 原地霸占了 CPU,deadline 的无符号回绕,则把近似永远变成了立刻就醒。三篇走完了,测量的方法论本身也成了交付的一部分:探针以 patch 的形式随库,忘了 apply 就会响亮地报错。

## 站内文章

<ChapterNav variant="sub">
  <ChapterLink href="01-clock-tree-dwt-first-cut">把秒表焊进硅片:时钟树爬上 72MHz,量第一刀切换</ChapterLink>
  <ChapterLink href="02-ramfunc-rollback-experiment">教科书输给预取缓冲器:.ramfunc 反转与一本诚实的实验册</ChapterLink>
  <ChapterLink href="03-notick-variants-and-probe-patch">拔掉心跳做实验:NO_TICK、僵尸任务与以 patch 随库的探针</ChapterLink>
</ChapterNav>

周期量的成本算完了,咱们把眼界再抬一层:单板的能跑只是第一步,四个考场的全跑通才是主张。下一站咱们把同一个 RTOS 搬上了 QEMU 的第二块板,把栈深按证据定了下来,而睡着的心跳,咱们让它自己慢下来,最后咱们挂牌 v0.1.0。
