---
title: "线程生命周期与 RAII"
description: "从 std::thread 创建到 RAII 包装，掌握 C++ 线程的所有权语义与生命周期管理"
---

# 线程生命周期与 RAII

上一篇我们了解了 CPU cache 和 OS 线程的底层机制，现在终于可以动手写第一个多线程程序了。但在敲下 `std::thread t(...)` 之前，我们需要先想清楚一件事：线程是一种**资源**，它占用操作系统内核对象、栈空间、TLS 存储等。跟文件句柄、动态内存一样，线程也必须被正确地获取和释放——否则你要面对的就是 `std::terminate` 和资源泄漏。

这一章我们从 `std::thread` 的基础用法开始，逐步深入参数传递的陷阱、所有权转移的语义，最后用 RAII 把这些复杂性封装起来。目标是让多线程代码和单线程代码一样，拥有清晰的所有权和确定性的资源释放。

## 本章内容

<ChapterNav variant="sub">
  <ChapterLink href="01-std-thread">std::thread 基础</ChapterLink>
  <ChapterLink href="02-thread-arguments-and-lifetime">线程参数与生命周期</ChapterLink>
  <ChapterLink href="03-thread-ownership-and-raii">线程所有权与 RAII</ChapterLink>
  <ChapterLink href="04-thread-local-and-call-once">thread_local 与 call_once</ChapterLink>
</ChapterNav>
