---
sidebar_order: 1
title: "线程：第一条执行流"
description: "从 std::thread 的构造、传参与收尾，到 move-only 所有权、jthread 自动 join 与 stop_token 三件套：把第一条执行流的生命周期管到底"
---

# 线程：第一条执行流

第 0 章 咱们给 OS 线程标了价，创建的价钱、切换的价钱，心里都有了数。这一章咱们把 `std::thread` 正经拿在手里用——第 0 章 里它只被当把手用了两回，开篇的示意算一回，掐表基准里真用的算一回，真正的用法都留给了这一章。卷首的 `先正确性，再性能` 在这一章不止一次派上用场：detach 图省事的写法，常常就是日后出事的写法。

三篇的走法是这样排的。第一篇把 `std::thread` 立了起来：构造的三种入口、join 与 detach 的分岔、joinable 的判据，文末拼出一个手动 join 的 `parallel_for_each`。第二篇钻的是传参：参数是怎么被 decay-copy 拷进线程的，`std::ref` 与 `std::move` 分头管的是哪些参数，detach 用错了引用为什么会悬垂，ASan 又怎么把它抓住。第三篇讲的是所有权与收尾：从 move-only 的所有权语义、手写的自动 join 类，到 C++20 的 `jthread` 与 stop_token 三件套，咱们把协作式取消一次讲全，往后的线程池与协程要用到取消，您到时候回这一篇取料就行。

## 本章内容

<ChapterNav variant="sub">
  <ChapterLink href="01-std-thread">std::thread 基础</ChapterLink>
  <ChapterLink href="02-thread-arguments-and-lifetime">线程参数与生命周期陷阱</ChapterLink>
  <ChapterLink href="03-thread-ownership-and-jthread">线程所有权与 jthread/stop_token</ChapterLink>
</ChapterNav>
