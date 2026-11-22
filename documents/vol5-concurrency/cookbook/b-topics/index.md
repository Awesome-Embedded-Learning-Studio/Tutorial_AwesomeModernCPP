---
title: "主题选读（B 区）"
description: "超出必修线的内容整篇平移于此：线程安全容器策略、无锁数据结构、教学级线程池、SeqLock 与自旋锁、协程事件循环与 echo server、Actor 与 CSP 两篇范式选读。"
---

# 主题选读（B 区）

这一区收的是超出必修线的内容。卷五正文只要求到必修线的深度，线程池、无锁容器、协程运行时、消息传递范式这些主题，咱们把整篇平移进这里。每篇开头都有一段选读定位，交代它为什么住这里，前置知识又在正文的哪个单元。

## 本区内容

<ChapterNav variant="sub">
  <ChapterLink href="01-thread-safe-containers">线程安全容器策略：粗粒度锁、分片锁、Copy-on-Write</ChapterLink>
  <ChapterLink href="02-lock-free-intro">无锁入门：CAS 循环、ABA、内存回收</ChapterLink>
  <ChapterLink href="03-lock-free-queues">SPSC 与 MPMC 无锁队列</ChapterLink>
  <ChapterLink href="04-teaching-threadpool">教学级线程池：五步演进</ChapterLink>
  <ChapterLink href="05-seqlock-spinlock">SeqLock 与自旋锁</ChapterLink>
  <ChapterLink href="06-async-io-event-loop">异步 I/O 与事件循环</ChapterLink>
  <ChapterLink href="07-coroutine-echo-server">协程 echo server</ChapterLink>
  <ChapterLink href="08-actor-model">Actor 模型与 Supervisor 策略</ChapterLink>
  <ChapterLink href="09-channel-csp">Channel 与 CSP：Select、管道</ChapterLink>
</ChapterNav>
