---
title: "给内核一秒钟:时间与临界区"
description: "Ticks 值类型用带符号差值让 uint32 回绕后照样比早晚,SelfList 侵入式链表把到期任务排成队列,BASEPRI 嵌套临界区在 host mock 与 Blue Pill 双端跑通同一份代码;收尾拆掉 CMSIS 直写 SysTick 三寄存器,与 core_cm3.h 当庭对质出优先级移位陷阱:注释写 Lowest,公式算出 0x00,救场的其实是 SysTick_Config 自带的库默认值"
chapter: 2
order: 0
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - 寄存器
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
---

# 给内核一秒钟:时间与临界区

> 咱们写完一篇发一篇,到今天已经上线两篇了。

## 概述

内存发对了,内核要接手的第一份真实职责是时间。咱们要回答三个问题:一秒在芯片上是多少个计数,计数器回绕了怎么比早晚,到期的任务按什么顺序叫醒。本站开头咱们用一对方括号了结上一站留的 constinit 悬念,然后咱们给内核装上时间子系统。`Ticks` 把时间包成了强类型,配上带符号的差值比较,咱们的“早晚”判断哪怕计数器翻了身,给出的也还是同一个答案。`SelfList` 是个侵入式的链表,咱们拿它把到期任务按截止时间排成了队列。咱们又让同一份 `Kernel<Driver>` 模板在桌面 mock 与 Blue Pill 上各跑了一遍,“一套代码四个目标”的承诺在这里第一次兑现了。收尾咱们把 CMSIS 从 arch 层拆掉、直写 SysTick 三寄存器,顺手跟仓库自带的 `core_cm3.h` 当庭对质。对质的结果值得您坐稳:注释写的是 Lowest,移位公式算完落进寄存器的却是 0x00,真正的最高优先级。运行时倒是一次都没出过事,咱们顺着源码找到了原因,是 `SysTick_Config` 自带的库默认值把错值覆写了,咱们把这段因果在正文里拆给读者看。时间会走了,调度器才有登台的舞台。

## 站内文章

<ChapterNav variant="sub">
  <ChapterLink href="01-ticks-and-timer-queue">一秒钟的定义权:tick、回绕算术与到期队列</ChapterLink>
  <ChapterLink href="02-systick-priority-truth">注释写 Lowest,硅片判 Highest:拆掉 CMSIS 的优先级移位</ChapterLink>
</ChapterNav>

时间会走了,CPU 还在 `main` 的 WFI 里打盹,咱们下一站就让调度器进场。就绪位图选出最急的任务,咱们在栈顶伪造一份出生证明,然后是第一次真正的上下文切换。您要是跟到了这里,咱们下一篇见。
