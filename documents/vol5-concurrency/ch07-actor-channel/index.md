---
title: "Actor 模型与 CSP"
description: "探索\"不共享内存\"的并发范式——Actor 模型的消息传递与 CSP 的 channel 通信"
---

# Actor 模型与 CSP

前面的章节里，我们用 mutex、atomic、future 这些工具来保护共享状态、协调线程间的执行顺序。但共享内存加锁只是并发编程的一种范式——还有另一个流派主张"不要共享内存"，用消息传递来替代锁。

这一章我们深入两种"不共享内存"的并发模型：Actor 模型和 CSP（Communicating Sequential Processes）。Actor 模型由 Carl Hewitt 在 1973 年提出，用有身份的 Actor 和异步消息传递来组织并发，在 Erlang 和 Akka 中被大规模工业验证。CSP 由 Tony Hoare 在 1978 年提出，用匿名的 channel 来连接独立的顺序进程，Go 语言的 goroutine + channel 就是 CSP 的经典实现。

我们会用 C++ 从零实现 Actor 框架的核心组件（邮箱、消息循环、supervisor）和类 Go channel 的通信管道（有缓冲/无缓冲、close 语义、select 模式），理解它们的设计动机和实现原理，并讨论在实际项目中如何选择合适的并发抽象。

## 本章内容

<ChapterNav variant="sub">
  <ChapterLink href="01-actor-model">Actor 模型与消息传递</ChapterLink>
  <ChapterLink href="02-channel-and-csp">Channel 与 CSP 模型</ChapterLink>
</ChapterNav>
