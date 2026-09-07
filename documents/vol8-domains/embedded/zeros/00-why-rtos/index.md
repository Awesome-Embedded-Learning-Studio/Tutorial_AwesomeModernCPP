---
title: "为什么是 RTOS:从超级循环到 ZerOS"
description: "前后台系统的三个结构性问题、整条线的概念对照表、为什么用 C++23,以及四个验证环境的分工"
chapter: 0
order: 0
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - 入门
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
---

# 为什么是 RTOS:从超级循环到 ZerOS

> 状态:陆续上线

## 概述

从 F103 线下来的您会写裸机、会 Renode,但没碰过内核。这一站先把前后台系统的三个结构性问题(响应时间没有保证、任务没有优先级、同步全靠手动)讲清楚,给出整条手搓线的概念对照表,每个概念都标了它将在 ZerOS 的哪个 commit 落地;然后讲清楚为什么这套内核在裸机上保留 C++23,以及 host、Renode、QEMU、真机四个验证环境各管什么。

## 站内文章

<ChapterNav variant="sub">
  <ChapterLink href="01-rtos-concept-map">从超级循环到 RTOS:为什么需要,怎么验证</ChapterLink>
</ChapterNav>

下一篇咱们讲工程搭建:checkout 仓库最早的两个 commit,把链接器禁令、向量表和构建系统立起来,在 Renode 串口窗里看到第一行输出。正在写,稍后上线。
