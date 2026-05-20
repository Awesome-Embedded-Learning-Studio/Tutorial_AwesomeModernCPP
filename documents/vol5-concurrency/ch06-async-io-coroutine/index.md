---
title: "异步 I/O 与协程"
description: "从异步编程范式演进到 C++20 协程机制，掌握 co_await/co_yield/co_return 与协程生命周期管理"
---

# 异步 I/O 与协程

前面的章节里，我们用线程、mutex、atomic、future 这些工具构建了并发程序的基础设施。但当我们面对"I/O 密集型"场景——比如一个网络服务器需要同时处理上千个连接——传统的"一个连接一个线程"模型就会暴露出严重的资源浪费问题。线程在等待 I/O 时白白占用内存和调度资源，我们需要一种更轻量的方式来表达"先去干别的事，等 I/O 完成了再回来"。

这一章我们从异步编程范式的演进开始，对比回调、future 链、协程三种模型的动机和痛点，理解为什么协程被认为是"异步编程的正确打开方式"。然后深入 C++20 协程的内部机制——编译器对协程函数的状态机变换、协程帧的分配与销毁、coroutine_handle 的生命周期管理——并从零实现一个完整的 generator，把所有概念串联起来。接着我们把目光投向协程的两大定制扩展点（promise_type 与 awaitable），将协程和操作系统的 I/O 多路复用机制接通，构建协程驱动的事件循环，最后用一个完整的协程 Echo Server 实战串联所有知识点。

## 本章内容

<ChapterNav variant="sub">
  <ChapterLink href="01-async-programming-evolution">异步编程演进：从回调地狱到协程</ChapterLink>
  <ChapterLink href="02-coroutine-basics">C++20 协程基础</ChapterLink>
  <ChapterLink href="03-promise-type-and-awaitable">promise_type 与 awaitable</ChapterLink>
  <ChapterLink href="04-async-io-and-event-loop">异步 I/O 与事件循环</ChapterLink>
  <ChapterLink href="05-coroutine-echo-server">实战：协程 Echo Server</ChapterLink>
</ChapterNav>
