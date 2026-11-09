---
title: "手搓 ZerOS:按 commit 时序,从裸机到 RTOS"
description: "拿一个真实存在、每个 commit 都能 checkout 的 C++23 裸机 RTOS,从空仓库一路跟到 v0.1.0 发布;文章锚定仓库真实的 26 个 commit"
platform: stm32f1
tags:
  - stm32f1
  - advanced
  - 嵌入式
  - cpp-modern
difficulty: advanced
cpp_standard: [23]
---

# 手搓 ZerOS:按 commit 时序,从裸机到 RTOS

之前的 F103 教程走下来,您已经能在 libestdx 之上把裸机应用写得很顺手,`third_party` 里还挂着一个 ZerOS 当现成的依赖用。咱们要做的,就是把它的内部亲手实现一遍。ZerOS 是一个 C++23 的裸机 RTOS,它把堆、RTTI、异常与 C ABI 都关在了门外。真正的开发板,就是您手边的 Blue Pill(STM32F103C8T6)。

咱们不另写一份教学版代码,而是沿着仓库真实的 26 个 commit 讲。而每一篇锚定一个或几个状态,您在自己的目录里把代码亲手复现出来,写出来的文件和参考答案一字不差。仓库降级成了参考答案,您卡住了或想对 diff,随时 checkout 对应的状态翻看。同一份内核代码咱们在 host 桌面单测、Renode 仿真、QEMU mps2-an385 与真正的开发板四个环境里验证,而每一篇都在其中至少一个环境里验收,Renode 的串口窗是全程主要的验收手段。

咱们的路线按 commit 时序推进:咱们把仓库跑起来(工程搭建、无堆内存、时间与临界区),然后走调度器与第一次的上下文切换,再进同步原语与内核服务的部分(信号量、互斥锁、队列、定时器、事件组),最后做性能实测、第二块板和 v0.1.0 的发布。咱们的内容写完一站就发一站,没写的站点不挂链接。

## 跟读姿势

```shell
git clone https://github.com/Awesome-Embedded-Learning-Studio/ZerOS.git
cd ZerOS
git submodule update --init
# 每篇开头会给这一篇的 commit,例如:
# git checkout fe5a0e8
```

有些事咱们得提前说清,免得您半路怀疑是自己 clone 坏了。个别中间 commit 在 host 上是编不过的,因为接口在前、实现在后是真实的节奏,对应的文章会写明 checkout 到哪个 commit 才能跑绿,甚至有补两个空函数的练习留给您。性能的倒数第二站要上真芯片(Blue Pill + ST-Link/OpenOCD),不过 host、Renode、QEMU 三个环境足够您跟完整条线,而真芯片上的数字,咱们会在文中标注它们只在真芯片上验证过。

您跟过前面 F103 教程的起步篇,进咱们的手搓线就会最顺,工具链、Renode、交叉构建都是您的旧识,相关的文章只讲 ZerOS 的不同处。没跟过的也能进,把起步篇当成随用随查的参考。

## 站点导航

<ChapterNav>
  <ChapterLink num="0" href="00-why-rtos/">为什么是 RTOS:从超级循环到 ZerOS</ChapterLink>
  <ChapterLink num="1" href="01-heapless-memory/">没有堆的世界:先把内存发对</ChapterLink>
  <ChapterLink num="2" href="02-time-kernel/">给内核一秒钟:时间与临界区</ChapterLink>
  <ChapterLink num="3" href="03-first-scheduler/">CPU 归谁:从就绪位图到中断唤醒</ChapterLink>
  <ChapterLink num="4" href="04-sync-and-inheritance/">正确的等待:计数信号量与优先级继承</ChapterLink>
  <ChapterLink num="5" href="05-context-economics/">谁的上下文:遗言、底半与定时器</ChapterLink>
  <ChapterLink num="6" href="06-fairness-and-closure/">公平与收束:时间片、事件组与直接通知</ChapterLink>
  <ChapterLink num="7" href="07-api-and-ci/">收口与门禁:demo 注册中心与 CI</ChapterLink>
  <ChapterLink num="8" href="08-performance-on-silicon/">真硅秒表:切换到底多贵</ChapterLink>
  <ChapterLink num="9" href="09-second-board-and-release/">第二块板与出门:QEMU、tickless、v0.1.0</ChapterLink>
</ChapterNav>

十个站点全部上线了,咱们从超级循环一路写到了挂牌出门,您可以从头跟起,也可以挑自己感兴趣的站看起来。
