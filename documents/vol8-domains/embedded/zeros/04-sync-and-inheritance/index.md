---
title: "正确的等待:计数信号量与优先级继承"
description: "SystemContext 八组操作的接缝让同一份信号量在 host mock 与真正的开发板上各跑一遍,P/V 语义保证事件先到也不丢;互斥锁在计数之上加所有权,等待链按优先级排座次,unlock 当面移交,单层优先级继承把卡在外面的最高任务等的时间压短;Renode 串口剧场里 M 任务安静的那 300 毫秒就是继承在付的代价"
chapter: 4
order: 0
tags:
  - stm32f1
  - advanced
  - 嵌入式
  - mutex
difficulty: advanced
platform: stm32f1
cpp_standard: [23]
---

# 正确的等待:计数信号量与优先级继承

> 咱们写完一篇发一篇,本章两篇全部上线了。

## 概述

中断会叫醒任务了,可事件来得比任务睡下去更早的时候,一字节的邮箱装不下,丢事件的老毛病就回来了。本站咱们给内核配上正确的等待,咱们分两步走。头一步咱们上计数信号量,它把事件发生过这件事记成了不会丢的计数。而让同一份同步代码在桌面与芯片上各跑一遍的,是 `SystemContext` 的八组操作接缝,那是应用与内核之间约好的清单,host 上咱们把它交给替身,板上交给了 CortexM3System。第二步咱们上互斥锁,它在计数之上加了所有权:等待链按优先级排好了座次,解锁把所有权当面移交给队头的等待者而不放回竞争,单层的优先级继承把低任务的级别临时抬起来。收尾是串口剧场里 M 任务突然安静的那 300 毫秒,您亲眼看一次优先级反转,再看继承怎么把它治好。

## 站内文章

<ChapterNav variant="sub">
  <ChapterLink href="01-semaphore-and-system-context">事件来得再早也不许丢:计数信号量与八操作的 SystemContext 接缝</ChapterLink>
  <ChapterLink href="02-mutex-priority-inheritance">看 M 闭嘴那三秒:互斥锁、优先级继承与所有权移交</ChapterLink>
</ChapterNav>

同步的两条腿都站住了,可内核服务的面还缺着几样:芯片死了怎么办,中断卸下来的重活交给谁,等三百毫秒要不要养一个专门的任务。下一站咱们按着“代码跑在哪个上下文”的纲,把它们一件一件配齐了。
