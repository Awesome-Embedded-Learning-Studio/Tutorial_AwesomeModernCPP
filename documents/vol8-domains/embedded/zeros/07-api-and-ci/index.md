---
title: "收口与门禁:demo 注册中心与 CI"
description: "342 行单体 example 拆成一幕一房,一行注册自动生成固件与 Renode 目标,任务创建收敛成三条路;CI 用四道检查把每站全绿自动化——host 单测、include 防火墙、体积天花板、Renode 冒烟;有了安全网,后面性能那几刀才有资格动"
chapter: 7
order: 0
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - CMake
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
---

# 收口与门禁:demo 注册中心与 CI

> 咱们写完一篇发一篇,本章两篇全部上线了。

## 概述

内核的能耐配齐了,example 也胖到了三百多行,八个 demo 是挤在同一间屋里的,不收拾的话咱们就没法教,也没法教到验收通过的地步。本站咱们把家收了,也把检查立了起来。头一件是把家收了:demo 拆成了注册中心,一行注册就能生成固件与 Renode 的目标,八个场景各自有了一幕一房。任务创建的写法收敛成了三条路,也就是手铺的 slot、arena 的切片与建造者链,它们各有各的适用场面。weak 与 strong 的中断处理器交给了链接器写条件,遗言也从可选变成了义务。第二件咱们立检查:CI 把四道关卡自动化了,host 单测、include 的防火墙(内核头不得认识板卡)、体积的天花板、Renode 冒烟里 grep 各 demo 的台词,每一站都跑绿从此是自动的事。仓库第一次有了安全网,后面性能的那几刀才有了资格动。

## 站内文章

<ChapterNav variant="sub">
  <ChapterLink href="01-demo-registry-and-arena">一幕一房:demo 注册中心、任务创建三条路与遗言成义务</ChapterLink>
  <ChapterLink href="02-ci-gates">跑五秒,说出你的台词:CI 冒烟、include 防火墙与体积闸门</ChapterLink>
</ChapterNav>

安全网挂好了,咱们就该回答那个从头悬着的问题:一次上下文切换的成本,到底多贵?下一站咱们整章上真硅,拿测量的数字说话。
