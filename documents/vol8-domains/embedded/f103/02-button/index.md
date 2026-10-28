---
title: "按键:消抖、状态机、variant"
description: "GPIO 输入读取与软件消抖,用状态机管理按键生命周期,用 variant 表达按键事件,让类型系统盯着每一个分支"
chapter: 2
order: 0
tags:
  - stm32f1
  - intermediate
  - 状态机
  - variant
difficulty: intermediate
platform: stm32f1
---

# 按键:消抖、状态机、variant

## 概述

这一站目前落地的是按键输入的第一篇:同一根 PA0 接进两条链,轮询消抖链用 Debouncer 与 ButtonFsm 报出稳态事件,EXTI 边沿链在中断里抓时间戳,Renode 注入抖动做对拍,两条链的取舍摆在明面。后续篇目等素材成熟再补。

## 章节导航

<ChapterNav variant="sub">
  <ChapterLink href="01-polling-debounce-vs-async-exti">一串抖动，两条链：轮询消抖与 EXTI 边沿的对拍</ChapterLink>
</ChapterNav>
