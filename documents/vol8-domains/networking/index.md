---
title: "网络编程"
description: "从 Linux socket 地基到 epoll/Reactor,再到 Boost.Asio、协程与 std::execution 的现代 C++ 网络编程"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
---

# 网络编程

C++作为长期在高性能优先的领域活跃的编程语言，一个重要的主战场就是网络编程（Networking Programming），由于大部分服务器都是Linux（笔者必须强调是大部分，因为仍然有些服务运行在Windows Server上），所以，基于**迁移自BSD Socket和随后派生的，著名的epoll机制**的学习，并且在这个基础上逐步学习和体验现代的C++网络编程，是我们这个子卷——网络编程种计划的一个核心要点。

目前计划是 **Linux First**，但是Windows的IOCP仍然重要，我们也会在这一卷的框架彻底完成之后，插入 Windows 提供的异步编程模型。随后，慢慢演进到理解Boost.ASIO的异步抽象（协程正式被支持前一个非常常见的解决方案），和重新看于 C++20 诞生的协程抽象方案，到Boost.Beast, 我们不会遗忘std::execution这一方案。截止到2026年6月底仍然让人兴奋的一个特性。


## Linux 地基

- [00 · 传统 socket 编程:服务器五步与 TCP 建链](./00-traditional-socket-basics.md) —— 经典 C 风格 BSD socket 五步、TCP 三次握手时序、字节序、`listen` 的两个队列与 backlog、SIGPIPE/SO_REUSEADDR
- [01 · 现代 socket 封装:RAII 与 `std::expected`](./01-modern-socket-wrapping.md) —— 用 Modern C++ 收掉 00 的裸 fd 与散落 errno,实测"每连接一线程"2000 并发吃 24GB 的 C10K 代价
- [02 · epoll:Linux I/O 多路复用](./02-epoll-io-multiplexing.md) —— 兴趣表 + 就绪队列 + 等待队列、ET vs LT 内核层差异、为什么 ET 必须非阻塞 + 循环读到 EAGAIN,复现 ET-read-once 丢 87KB 数据
- [03 · Reactor 模式:把 epoll 包成事件循环 + 回调](./03-reactor-pattern.md) —— POSA2 四角色、"同步非阻塞"、Reactor(就绪通知)↔ Proactor(完成通知)承重梁

## 后续(进行中)

io_uring(Linux 完成驱动,补全后端巡礼)→ Boost.Asio(同步到异步回调链)→ Asio 执行器 + completion token → C++20 协程 on Asio → Boost.Beast(HTTP/WebSocket)→ std::execution(P2300)展望。配套 **Lab:mini Reactor echo server**(对抗性验收 + TSan)进行中。

> 源码阅读层(Boost.Asio / Beast 源码深入)另见 [vol9 开源项目阅读](../../vol9-open-source-project-learn/)。
