---
title: "互斥量、条件变量与同步原语"
description: "从 mutex 到 condition_variable 再到 shared_mutex，系统掌握 C++ 的等待-通知机制与读写锁模式"
---

# 互斥量、条件变量与同步原语

前一章我们搞清楚了线程的生命周期和 RAII 管理——知道怎么创建线程、怎么安全地等待它们完成。但光有线程还不够，多个线程如果要访问同一份数据，就必须有一套协调机制。这一章我们聚焦 C++ 标准库中最核心的同步原语：互斥量（mutex）用于保护临界区，条件变量（condition_variable）用于线程间的等待-通知协调，读写锁（shared_mutex）用于读多写少场景的并发优化。

我们会从 mutex 和 RAII 锁的基本用法开始，理解 `lock_guard` 和 `unique_lock` 的区别；然后深入 condition_variable 的等待语义，搞清楚虚假唤醒、丢失唤醒这些必须掌握的坑点；最后介绍 C++17 的 shared_mutex，分析它的适用场景和性能边界。每一步都配有可编译的代码示例和实战练习。

## 本章内容

<ChapterNav variant="sub">
  <ChapterLink href="01-mutex-and-raii-guards">mutex 与 RAII 锁</ChapterLink>
  <ChapterLink href="02-deadlock-and-lock-ordering">死锁与锁顺序</ChapterLink>
  <ChapterLink href="03-condition-variable">condition_variable 与等待语义</ChapterLink>
  <ChapterLink href="04-shared-mutex">读写锁与 shared_mutex</ChapterLink>
  <ChapterLink href="05-latch-barrier-semaphore">latch、barrier 与 semaphore</ChapterLink>
</ChapterNav>
