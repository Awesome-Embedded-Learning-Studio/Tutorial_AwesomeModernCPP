---
title: "贯穿式实战项目"
description: "把各卷学到的零散知识串成完整项目——从协程服务器、迷你运行时到工业级组件研读"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
---

# 贯穿式实战项目

> 这一栏不是新知识的堆砌，而是把各卷学到的碎片——并发、协程、模板、内存管理——串成一个能跑、能测、能交付的完整项目。下面先列出已经在其它卷里落地、可以直接接着做的项目，再列出还在规划中的远期目标。

## 已经有基础的项目

这些项目在其它卷里已经有了教程或可运行骨架，从这里可以顺藤摸瓜深入：

- **协程 Echo 服务器**：在 [卷五·协程 Echo 服务器](../vol5-concurrency/ch06-async-io-coroutine/05-coroutine-echo-server.md) 里，从 `co_await` 一路搭到了一个能收发的回显服务，是理解协程调度最实在的练手项目。
- **迷你并发运行时（capstone）**：[卷五·迷你运行时 capstone](../vol5-concurrency/exercises/06-capstone-mini-runtime.md) 把线程池、定时器、任务队列揉成一个最小调度器，是后续做 "Mini Concurrent Runtime" 的现成起点。
- **OnceCallback 组件研读**：[卷九·OnceCallback](../vol9-open-source-project-learn/chrome/01_once_callback/index.md) 用 16 篇文章手撕了 Chromium 的回调机制，是从读源码走向 "自己设计工业级组件" 的范例。
- **INI 解析器**：作为 C++ 工程化的第一个完整项目，放在独立仓库 [Tutorial_cpp_SimpleIniParser](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_cpp_SimpleIniParser)——从词法分析到错误处理，适合跟着做一遍。

## 规划中的项目

这些还没动工，是远期目标，按 "素材就绪程度" 排序：

- **手写 STL 组件**：vector / string / unique_ptr / optional / function / variant 各手写一遍，配合卷三标准库的源码阅读。
- **迷你 HTTP 服务器**：从 TCP socket 到协程异步化，承接卷五并发和卷八网络编程。
- **迷你 GUI 框架**：事件循环、控件系统、布局引擎、渲染后端。
- **嵌入式迷你 OS**：调度器、同步原语、内存管理、驱动框架，承接卷八嵌入式主线。

> 这些项目都不会一蹴而就，会随着对应卷的内容完善而逐步启动。如果你有想做的项目，欢迎在 Discussion 里提出。
