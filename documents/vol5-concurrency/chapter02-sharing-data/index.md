---
title: "第二单元：多个任务怎样共同使用数据"
description: "从会算错的程序认识 data race 与 race condition，再看互斥量怎样保护共享状态、一次操作的边界在哪、以及怎样减少共享与避免死锁"
---

# 第二单元：多个任务怎样共同使用数据

第一单元解决了“线程怎么开始、怎么结束”，咱们换一个问题：几条执行流碰到同一份数据，会出什么事，应该怎么组织访问。

咱们把顺序定成看清错误在前，学工具在后。从错误的程序入手，把 data race、race condition、死锁、活锁、饥饿这些形状认全，然后才动互斥量与锁，最后讨论减少共享和避免死锁的各种方案。判断走在动手前面，这是全卷的原则。单元内容按节分篇，随迁移逐步补齐。

## 本单元内容

<ChapterNav variant="sub">
  <ChapterLink href="01-from-wrong-programs">从错误程序认识竞争</ChapterLink>
  <ChapterLink href="02-mutex-and-locks">用互斥保护共享状态</ChapterLink>
  <ChapterLink href="03-check-then-act">一次操作的边界在哪里</ChapterLink>
  <ChapterLink href="04-sharing-less-and-deadlock">减少共享与避免死锁</ChapterLink>
</ChapterNav>
