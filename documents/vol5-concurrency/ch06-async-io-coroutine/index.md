---
sidebar_order: 6
title: "协程"
description: "从 C++20 协程机制到手写 task<T> 正源，再到协程取消与事件循环，用更轻的任务单位组织异步 I/O"
---

# 协程

咱们在前面的几章里，手里的执行流始终是线程：开一条线程，协调用的是 mutex 和 atomic，传结果走的是 future。到了 I/O 密集的场景，这套模型就开始吃力了，一个连接一条线程的开销不小，线程等 I/O 的时候又白白占住了内存和调度资源。咱们需要一种更轻的表达：咱们去干别的，等 I/O 完成了再回来。卷首的 `先同步再任务` 说的就是这道顺序：同步的机制前面立住了，这一章把任务这个单位做得更轻了。

这一章咱们从 C++20 协程的机制地基开始，把三个关键字、promise 的钩子、awaiter 协议一样样过一遍。接着咱们手写惰性的 `Task<T>`，它是全卷六大正源里的一席，值、异常、续体的三条通道一次接通，后面的取消与调度都在 Task 身上接着搭。然后补上协程的取消：任务挂在 `co_await` 上等结果的时候，停止的信号怎么送进去，收到信号的任务怎么收尾。收尾的时候咱们把 Task 接上事件循环与定时器，让调度循环成为唯一调用 resume 的地方，蹦床接线的栈真相也在那里落定。

## 本章内容

<ChapterNav variant="sub">
  <ChapterLink href="01-coroutine-basics">C++20 协程基础</ChapterLink>
  <ChapterLink href="02-task-coroutine">手写 task&lt;T&gt;：惰性任务与对称转移</ChapterLink>
  <ChapterLink href="03-coroutine-cancellation">协程取消</ChapterLink>
  <ChapterLink href="04-event-loop-and-timers">事件循环与定时器</ChapterLink>
</ChapterNav>
