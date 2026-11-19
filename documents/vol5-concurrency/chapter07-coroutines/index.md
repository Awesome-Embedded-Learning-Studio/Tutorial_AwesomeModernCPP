---
title: "第七单元：用协程表达可以暂停的工作"
description: "从前六个单元的线程级工具，走到可以暂停的工作：为什么需要暂停与恢复，一个最小协程怎样成立，co_await 怎样参与等待，把协程组织成可完成的任务"
---

# 第七单元：用协程表达可以暂停的工作

单元一到单元六，咱们把线程、共享、等待、任务、原子操作与验收一路安排上，工具全部压在线程身上。单元七请进协程：工作可以自己停住，停住的时候把线程还回去，事件到了再接着跑。按节分篇，从为什么需要可以暂停的工作讲起，再进机制。事件循环与 echo server 不进正文，选读的正式入口还在迁移路上，旧目录里有现成的两篇。

## 本单元内容

<ChapterNav variant="sub">
  <ChapterLink href="01-why-suspend-resume">为什么需要暂停与恢复</ChapterLink>
  <ChapterLink href="02-minimal-coroutine">一个最小协程怎样成立</ChapterLink>
  <ChapterLink href="03-co-await-and-awaiters">`co_await` 怎样参与等待</ChapterLink>
  <ChapterLink href="04-task-type">把协程组织成可完成的任务</ChapterLink>
</ChapterNav>
