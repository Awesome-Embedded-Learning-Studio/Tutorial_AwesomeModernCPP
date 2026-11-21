---
title: "第三单元：任务之间怎样等待和协调"
description: "从条件变量接手等待，到有容量上限的任务队列、等待的结束路径，再按协作需求比较 latch、barrier 与信号量"
---

# 第三单元：任务之间怎样等待和协调

第二单元解决的是“几条执行流碰到同一份数据怎么办”。这一单元咱们换一个问题：一条线程怎么等另一件事成立，然后接着往下走。

咱们把等待从最底层的工具讲起。条件变量负责“睡下去、有人叫醒、醒来重查条件”，后面几篇都建在它上面：3.3 拿它搭有容量上限的任务队列，3.6 给每一处等待补上结束路径，3.7 才轮到 latch、barrier 和信号量这些更高一层的协作工具。单元内容按节分篇，本单元七篇已补齐。

## 本单元内容

<ChapterNav variant="sub">
  <ChapterLink href="01-wait-for-a-condition">等一个条件：从轮询到条件变量</ChapterLink>
  <ChapterLink href="02-notify-and-timeout">叫谁，等多久：notify 的选择与超时</ChapterLink>
  <ChapterLink href="03-bounded-queue">有容量的队列：先让它站得住</ChapterLink>
  <ChapterLink href="04-queue-close-and-backpressure">队列关不上会怎样：关闭与背压</ChapterLink>
  <ChapterLink href="05-queue-exception-safety">搬一半抛了算谁的：队列的异常安全</ChapterLink>
  <ChapterLink href="06-ending-a-wait">等待也需要结束路径</ChapterLink>
  <ChapterLink href="07-choosing-tools">按协作需求选择工具</ChapterLink>
</ChapterNav>
