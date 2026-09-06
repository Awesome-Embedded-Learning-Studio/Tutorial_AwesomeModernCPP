---
title: "UART:中断驱动、环形缓冲、expected"
description: "中断驱动的串口收发,环形缓冲扛住速率差,命令解析的错误路径交给 expected"
chapter: 3
order: 0
tags:
  - stm32f1
  - intermediate
  - 循环缓冲区
  - expected
difficulty: intermediate
platform: stm32f1
---

# UART:中断驱动、环形缓冲、expected

> 状态：规划中

## 概述

串口是嵌入式里最常用的对话窗口。这一站把收发做成中断驱动,用环形缓冲扛住收发速率差,命令解析的错误路径交给 expected——解析失败带着原因返回,而不是悄悄吞掉。

## 章节导航

> 内容编写中，敬请期待。
