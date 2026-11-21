---
title: "卷五：并发编程"
description: "从线程原语到协程异步"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
---

# 卷五：并发编程

这一卷，我们聊的是各个领域编程中难以绕过的一部分，也就是并发编程。

需要说明的是，这个章节正在进行如火如荼的重构和建设。新版本和旧版本的目录会并存一段时间。直到新版本的并发编程章节完全可以发布后，旧版本的目录才会撤下。

## 实验与查证

本卷的实验程序都收在 `code/examples/vol5/` 和所在页的在线示例里，您点开就能直接运行。原实验在 GCC 16.2.1、WSL2（16 逻辑核，2026-11-14）下实测过，抽成独立文件时复验过。涉及 C++ 标准库的行为查过 cppreference 和 eel.is 上的现行草案，条款链接收在各篇的参考资源里。不同运行里的输出顺序可能变化，耗时则会随机器漂，量级、编号和机制不变。

## 章节导航

这是新版本的并发编程，目前八关，此外还有一个 cookbook，是给懂并发编程懒得听笔者碎碎念的朋友看的

<ChapterNav variant="sub">
  <ChapterLink href="chapter01-start-concurrency">第一关 · 让程序开始并发执行</ChapterLink>
  <ChapterLink href="chapter02-sharing-data">第二关 · 多个任务怎样共同使用数据</ChapterLink>
  <ChapterLink href="chapter03-wait-and-coordinate">第三关 · 任务之间怎样等待和协调</ChapterLink>
  <ChapterLink href="chapter04-organizing-tasks">第四关 · 从管理线程走向组织任务</ChapterLink>
  <ChapterLink href="chapter05-atomic-memory-model">第五关 · 理解原子操作与内存模型</ChapterLink>
  <ChapterLink href="chapter06-validating-designs">第六关 · 怎样判断并发设计是否有效</ChapterLink>
  <ChapterLink href="chapter07-coroutines">第七关 · 用协程表达可以暂停的工作</ChapterLink>
  <ChapterLink href="cookbook">cookbook · 卷五的速查区</ChapterLink>
</ChapterNav>

## 旧版目录

迁移过渡期旧目录保留作素材，迁移完成后移出站点。

<ChapterNav variant="sub">
  <ChapterLink href="ch00-concurrency-fundamentals">ch00 · 并发思维与基础</ChapterLink>
  <ChapterLink href="ch01-thread-lifecycle-raii">ch01 · 线程生命周期与 RAII</ChapterLink>
  <ChapterLink href="ch02-mutex-condition-sync">ch02 · 互斥量、条件变量与同步原语</ChapterLink>
  <ChapterLink href="ch03-atomic-memory-model">ch03 · 原子操作与内存模型</ChapterLink>
  <ChapterLink href="ch04-concurrent-data-structures">ch04 · 并发数据结构</ChapterLink>
  <ChapterLink href="ch05-future-task-threadpool">ch05 · future、任务与线程池</ChapterLink>
  <ChapterLink href="ch06-async-io-coroutine">ch06 · 异步 I/O 与协程</ChapterLink>
  <ChapterLink href="ch07-actor-channel">ch07 · Actor 与 Channel</ChapterLink>
  <ChapterLink href="ch08-debug-testing-perf">ch08 · 调试、测试与性能</ChapterLink>
  <ChapterLink href="ch09-distributed-bridge">ch09 · 分布式桥接附录</ChapterLink>
</ChapterNav>
