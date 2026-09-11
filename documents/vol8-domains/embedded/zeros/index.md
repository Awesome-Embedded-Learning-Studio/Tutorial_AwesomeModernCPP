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

之前的 F103 教程走下来,您已经能在 libestdx 之上把裸机应用写得很顺手,`third_party` 里还挂着一个 ZerOS 当现成依赖用。这条线要做的事,就是把这份依赖的内部亲手实现一遍:ZerOS 是一个 C++23 裸机 RTOS,无堆、无 RTTI、无异常、无 C ABI,真机就是您手边那块 Blue Pill(STM32F103C8T6)。

教程不另写一份教学版代码,而是沿着仓库真实的 26 个 commit 讲:每一篇锚定一个或几个状态,您在自己的目录里把代码亲手复现出来,写出来的文件和参考答案一字不差;仓库降级成参考答案,卡住了或想对 diff,随时 checkout 对应状态翻看。同一份内核代码在四个环境验证——host 桌面单测、Renode 仿真、QEMU mps2-an385、真机,每一篇都在其中至少一个环境里验收,Renode 串口窗是全程的主要验收手段。

路线按 commit 时序推进:咱们先把仓库跑起来(工程搭建、无堆内存、时间与临界区),然后是调度器与第一次上下文切换,接着是同步原语与内核服务(信号量、互斥锁、队列、定时器、事件组),最后是性能实测、第二块板和 v0.1.0 发布。内容写完一站发一站,没写的站点不挂链接。

## 跟读姿势

```shell
git clone https://github.com/Charliechen114514/ZerOS.git
cd ZerOS
git submodule update --init
# 每篇开头会给这一篇的 commit,例如:
# git checkout fe5a0e8
```

有些事得提前说清,免得您半路怀疑是自己 clone 坏了。其一,个别中间 commit 在 host 上编不过——先交接口、后交实现是真实节奏,对应文章会写明 checkout 到哪个 commit 才能跑绿,甚至有补两个空函数的练习。其二,性能那一站要上真芯片(Blue Pill + ST-Link/OpenOCD);没有板子,host、Renode、QEMU 三个环境足够跟完整条线,真机相关的数字文中会标注只在真芯片上验证过。

您跟过前面 F103 教程的起步篇,再来这条线最顺,工具链、Renode、交叉构建都是旧识,相关文章只讲 ZerOS 的不同处;没跟过也能进,把起步篇当参考随用随查。

## 站点导航

<ChapterNav>
  <ChapterLink num="0" href="00-why-rtos/">为什么是 RTOS:从超级循环到 ZerOS</ChapterLink>
  <ChapterLink num="1" href="01-heapless-memory/">没有堆的世界:先把内存发对</ChapterLink>
</ChapterNav>

后续站点(时间与临界区、调度器、同步原语、内核服务、demo 与 CI、真机性能、第二块板与发布)陆续上线,您可以先从已发布的部分跟起。
