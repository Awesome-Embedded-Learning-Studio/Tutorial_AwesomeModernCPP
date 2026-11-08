---
title: "谁的上下文:遗言、底半与定时器"
description: "内核服务的经济学按一条纲收拢:代码跑在哪个上下文。HardFault 的遗言报告与栈金丝雀管 fault 上下文,环形队列加工作者线程管 ISR 卸货(中断只递条子),软件定时器管免任务的等待;host 考场与 Renode 注入实验逐站验收"
chapter: 5
order: 0
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - 回调机制
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
---

# 谁的上下文:遗言、底半与定时器

> 咱们写完一篇发一篇,目前上线两篇了。

## 概述

内核能跑了,可想在它上面继续写应用的时候,咱们还有三笔开销绕不开:芯片崩了要看得见死因,中断收到的重活得卸给干活的人,等三百毫秒不该烧掉一整个任务控制块的成本。本站咱们按着“代码跑在哪个上下文”的纲把三件套配上。遗言管的是 fault 的上下文:咱们用几行 naked 汇编判出了 MSP 与 PSP,CFSR、HFSR、BFAR 也都被解码成了报告,栈金丝雀挂在 PendSV 的路径上,巡逻只付一次比较的代价。底半管的是 ISR 的卸货:定容环形缓冲配上了计数信号量,工作者线程是零堆出生的,中断只递条子而自己永不睡觉。定时器管的是免任务的等待:等三百毫秒的时候,您不必养一个专门的任务。三篇是互相独立的,而彼此支撑着,合起来就是内核服务面的经济学。

## 站内文章

<ChapterNav variant="sub">
  <ChapterLink href="01-fault-canary-and-log">芯片死前立遗嘱:HardFault 现场、栈金丝雀与零分配日志</ChapterLink>
  <ChapterLink href="02-msg-queue-and-worker">中断只递条子:环形队列、工作者线程与底半推迟</ChapterLink>
</ChapterNav>

软件定时器与 fn 加 void* 的硬通货还在后面的篇目里等着您,咱们写好一篇就挂一篇。
