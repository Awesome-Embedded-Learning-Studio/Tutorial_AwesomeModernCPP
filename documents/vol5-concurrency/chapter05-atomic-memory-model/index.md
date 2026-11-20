---
title: "第五单元：理解原子操作与内存模型"
description: "从原子操作的能力边界走到 happens-before，再为每一次原子操作挑选内存序"
---

# 第五单元：理解原子操作与内存模型

前面的单元把线程、共享、等待与任务都安排上了，工具您已经用熟。本单元往后退半步，问工具下面的事：原子操作到底保证到哪一步，两个操作之间的顺序怎么算，以及为每次原子操作挑选内存序时依据什么。单元内容按节分篇，三篇已补齐。

## 本单元内容

<ChapterNav variant="sub">
  <ChapterLink href="01-what-atomics-solve">原子操作解决到哪一步</ChapterLink>
  <ChapterLink href="02-happens-before">一个线程怎样观察另一个线程的修改</ChapterLink>
  <ChapterLink href="03-choosing-memory-orders">选择内存序需要什么依据</ChapterLink>
</ChapterNav>
