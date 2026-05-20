---
title: "原子操作与内存模型"
description: "从 std::atomic 的操作集到六种内存序的完整拆解，建立无锁编程的理论基础"
---

# 原子操作与内存模型

前两章我们讨论了线程生命周期和 mutex 同步机制——它们解决了"怎么让多个线程安全地协作"这个基本问题。但 mutex 有一个固有代价：即使临界区只有一个变量的简单自增，也必须走 lock → modify → unlock 的完整流程。当性能要求更高、临界区更小时，我们需要更轻量的工具。

这一章我们进入 `std::atomic` 和 C++ 内存模型的世界。`std::atomic` 利用 CPU 的原子指令，在不加锁的情况下保证操作的不可分割性。而内存序（memory order）则控制编译器和 CPU 的指令重排行为，让你在性能和可预测性之间做出精确的权衡。这两者合在一起，构成了无锁编程的理论基础——是后续章节讨论无锁数据结构和原子操作模式的前置知识。

## 本章内容

<ChapterNav variant="sub">
  <ChapterLink href="01-atomic-operations">atomic 操作</ChapterLink>
  <ChapterLink href="02-memory-ordering">内存序详解</ChapterLink>
  <ChapterLink href="03-fence-and-barrier">fence 与编译器屏障</ChapterLink>
  <ChapterLink href="04-atomic-wait-and-ref">atomic_wait 与 atomic_ref</ChapterLink>
  <ChapterLink href="05-atomic-patterns">原子操作模式</ChapterLink>
</ChapterNav>
