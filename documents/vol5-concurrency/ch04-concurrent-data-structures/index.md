---
title: "并发数据结构"
description: "从线程安全队列到并发容器，掌握基于锁的并发数据结构设计策略"
---

# 并发数据结构

前两章我们搞定了同步原语（mutex、condition_variable、shared_mutex）和原子操作（atomic、memory order）。现在该把这些工具用起来了——这一章我们聚焦并发数据结构的设计与实现。并发数据结构是多线程程序的核心组件：线程池的任务队列、服务器的路由缓存、消息系统的缓冲区，背后都需要线程安全的数据结构支撑。

我们会从最实用的线程安全队列开始——它是生产者-消费者模式的基石，也是理解"如何用 mutex + condition_variable 构建正确并发组件"的最佳案例。然后扩展到更通用的并发容器设计——讨论粗粒度锁、细粒度锁、分片锁和 copy-on-write 四种策略的设计与权衡。最后我们进入无锁编程领域——从 CAS 循环、ABA 问题到 SPSC ring buffer 和 Michael-Scott MPMC 队列，建立无锁并发数据结构的设计与判断能力。

## 本章内容

<ChapterNav variant="sub">
  <ChapterLink href="01-thread-safe-queue">线程安全队列</ChapterLink>
  <ChapterLink href="02-thread-safe-containers">线程安全容器设计</ChapterLink>
  <ChapterLink href="03-lock-free-basics">无锁编程基础</ChapterLink>
  <ChapterLink href="04-lock-free-queues">SPSC 与 MPMC 队列</ChapterLink>
</ChapterNav>
