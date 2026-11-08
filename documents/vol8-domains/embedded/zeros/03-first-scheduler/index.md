---
title: "CPU 归谁:从就绪位图到中断唤醒"
description: "全线的心脏:32 级就绪位图加每级 FIFO 选出最急的任务,fabricate_frame 在栈顶伪造出生证明,svc 0 完成首次切换,丢唤醒竞态与类型状态防线在后面的篇目兑现;同一份调度器代码在 host 考场跑绿 42 条断言,在 Renode 里让 A/B 两个任务按 1000/700ms 交错打印"
chapter: 3
order: 0
tags:
  - stm32f1
  - advanced
  - 嵌入式
  - 寄存器
  - concepts
difficulty: advanced
platform: stm32f1
cpp_standard: [23]
---

# CPU 归谁:从就绪位图到中断唤醒

> 咱们写完一篇发一篇,到今天已经上线两篇了。

## 概述

时间会走了,CPU 还在 `main` 的 WFI 里打盹,它此刻归谁、谁说了算,正是本站要回答的问题。咱们把仓库里最大的那个 commit(`0bf9461`)拆成两半来讲。咱们把仲裁面讲清楚,32 级的就绪位图配每级 FIFO,`pick_next` 与 `yield` 的入队方向故意不对称,被抢占的回队头、让出的去队尾,选人靠 RBIT 加 CLZ 在单周期里出了结果。机制面的主角是 `fabricate_frame`,咱们让它在栈顶伪造一份出生证明,xPSR 的 Thumb 位一个不能错,`svc 0` 点了火,内核就真的切起来了。您会在 host 考场亲眼看见 42 条断言全部通过,也会在 Renode 的串口窗里看到 A/B 两个任务按 1000/700ms 交错打印,那一刻就是 ZerOS 从时钟库变成 RTOS 的格子。

## 站内文章

<ChapterNav variant="sub">
  <ChapterLink href="01-ready-bitmap-pick-next">谁最急,谁上场:就绪位图、任务三态与选人仲裁</ChapterLink>
  <ChapterLink href="02-pendsv-first-switch">在栈顶伪造出生证明:PendSV、SVC 与第一次切换</ChapterLink>
</ChapterNav>

调度器能切了,手搓 task_sleep 会遇上的丢唤醒竞态,和中断把睡着的任务叫醒的第一条链路,还在后面的篇目里等着咱们,咱们写好一篇就挂一篇。
