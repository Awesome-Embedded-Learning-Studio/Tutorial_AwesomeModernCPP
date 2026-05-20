---
title: "并发思维与基础"
description: "建立并发判断力：理解为什么需要并发、并发会出什么问题、硬件与 OS 如何支撑多线程"
---

# 并发思维与基础

笔者认为，并发是 C++ 工程能力的一道分水岭。当单核频率的增长撞上功耗墙，现代软件性能的提升几乎全部依赖两个方向：更好的算法，以及更好的并行化。而并行化的基础就是并发——在正确性不被破坏的前提下，让多个执行流协同工作，充分压榨多核硬件的算力。坦白说，如果你写的程序始终是单线程顺序执行的，那不管代码多漂亮，你都在浪费手上那颗 CPU 的大部分晶体管。

不过在动手写任何多线程代码之前，我们必须先回答三个问题：为什么需要并发？并发到底会出什么问题？CPU 和操作系统又是怎么支撑多线程的？大多数教程可能起手就会教你 `std::thread`，但是笔者不太希望这样，笔者希望先把并发这个领域的思维方式建立起来——先正确性，再性能，这是我们整卷的原则。

## 本章内容

<ChapterNav variant="sub">
  <ChapterLink href="01-why-concurrency">为什么需要并发</ChapterLink>
  <ChapterLink href="02-concurrency-problems">并发基本问题</ChapterLink>
  <ChapterLink href="03-cpu-cache-and-os-threads">CPU cache 与 OS 线程</ChapterLink>
</ChapterNav>
