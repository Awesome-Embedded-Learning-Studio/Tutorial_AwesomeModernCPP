---
title: "第二块板与出门:QEMU、tickless、v0.1.0"
description: "单板证明能跑,四考场才是主张:QEMU mps2-an385 内核零改动补上第三格,栈水位回答用多少、tickless 回答省多少,最后 checkout v0.1.0 做全链路终验,挂牌出门"
chapter: 9
order: 0
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - 工具链
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
---

# 第二块板与出门:QEMU、tickless、v0.1.0

> 咱们写完一篇发一篇,本章四篇全部上线了,整条手搓线到这儿也走完了。

## 概述

单板的能跑只是第一步,四个考场的全跑通才是主张。本章咱们把同一个 RTOS 搬上了 QEMU 的 mps2-an385,靠的是当初拆掉 CMSIS 换来的薄 port,内核的一行都没改。咱们再把一个大 commit 拆成了两篇:栈水位这一篇回答的是用多少的问题,咱们按证据来定栈深,而不是按感觉来。tickless 这一篇回答的是省多少的问题,咱们让睡着的心跳调慢,对准了下一到期点再醒。收尾咱们 checkout 了 v0.1.0,做了一趟全链路的终验,host、Renode、QEMU 与真正的开发板各走了一遍,给整条手搓线挂上了牌、出了门。

## 站内文章

<ChapterNav variant="sub">
  <ChapterLink href="01-qemu-mps2-second-board">QEMU 里的同款心跳:第二块板零改,兑现薄 port</ChapterLink>
  <ChapterLink href="02-stack-watermark">把栈刷满魔法数:金丝雀升级成水印,栈深按证据定</ChapterLink>
  <ChapterLink href="03-tickless-idle">睡着就调慢心跳:tickless、读清竞态与 24 位分期续杯</ChapterLink>
  <ChapterLink href="04-release-v010">挂牌 v0.1.0:README 即大门,四个考场挨个走到</ChapterLink>
</ChapterNav>

咱们从超级循环一路走到了 v0.1.0,手搓线到这儿就齐了。您跟着复现出来的每一行代码,和参考答案是同一棵树上的果实。再往深处走的时候,内核就在您自己的手里了。
