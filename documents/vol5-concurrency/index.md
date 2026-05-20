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

从线程原语到协程异步，从锁到无锁，从同步到任务——卷五帮你建立完整的并发判断力。我们的原则是：**先正确性，再性能；先锁，再无锁；先同步，再任务**。

## 章节导航

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

## 旧版文章（逐步重写中）

以下文章属于旧版结构，将在新章节逐步完善后归档替换。

<ChapterNav variant="sub">
  <ChapterLink href="01-atomic">原子操作</ChapterLink>
  <ChapterLink href="02-memory-order">内存序</ChapterLink>
  <ChapterLink href="03-lock-free-data-structures">无锁数据结构</ChapterLink>
  <ChapterLink href="04-mutex-and-raii-guards">mutex 与 RAII 守卫</ChapterLink>
  <ChapterLink href="06-critical-section-protection">临界区保护</ChapterLink>
  <ChapterLink href="03-coroutine-echo-server">协程 Echo Server</ChapterLink>
</ChapterNav>
