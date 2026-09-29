---
sidebar_order: 5
title: "从线程到任务"
description: "从 std::async/future 的启动与结果获取，到 promise/packaged_task 的手动通道，再到 worker 循环正源与优雅关闭的线程池：把执行流交给任务的一章"
---

# 从线程到任务

第 1 章 到 第 4 章 咱们把执行流的底层一路配齐了：线程本身、锁与同步、原子与内存序，第 4 章 还把无锁的性能量过了。可咱们手里攥着的仍是线程：开几条线程、怎么配同步、结果从哪取，样样都得咱们自己动手。卷首的 `先同步再任务` 说的就是这道顺序：同步的机制前面立住了，到了 第 5 章，咱们改在任务的层面组织代码：做什么交给任务、谁来跑交给池子。

三篇的走法是这样排的。第一篇讲的是 `std::async` 与 future：一句调用就把任务发了出去，结果和异常都替咱们接住，launch 策略的分岔也在这篇里定。第二篇把手动通道立了起来：promise 怎么写值、packaged_task 怎么把任务打包、future 那头怎么取，咱们自己接线。第三篇要立的是正源：线程池的 worker 循环。咱们从取任务的循环写起，经优雅关闭的三步时序，一路讲到析构的收场——析构之后再 submit 就是未定义的行为，任务里反过来把池子析构了，等来的就是 terminate。第 1 章 留下的 stop_token 三件套，也在这里头一回真正派上了用场。

## 本章内容

<ChapterNav variant="sub">
  <ChapterLink href="01-async-and-future">std::async 与 future</ChapterLink>
  <ChapterLink href="02-promise-and-packaged-task">promise 与 packaged_task</ChapterLink>
  <ChapterLink href="03-thread-pool">线程池：worker 循环与优雅关闭</ChapterLink>
</ChapterNav>
