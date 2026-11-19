---
title: "第一单元：让程序开始并发执行"
description: "从引入并发的判断开始，到线程的机器模型、第一个 std::thread 程序、生命周期归属与有序结束"
---

# 第一单元：让程序开始并发执行

本单元回答一个顺序固定的问题链：什么时候值得引入并发，线程在机器上是什么，怎么写出第一个 `std::thread` 程序，谁负责线程和数据的生命周期，以及怎么让工作有序结束。判断走在动手前面，这是全卷的原则。单元内容按节分篇，随迁移逐步补齐。

## 本单元内容

<ChapterNav variant="sub">
  <ChapterLink href="01-when-concurrency">什么时候值得引入并发</ChapterLink>
  <ChapterLink href="02-what-is-a-thread">线程在机器上是什么</ChapterLink>
  <ChapterLink href="03-first-std-thread">第一个 std::thread 程序</ChapterLink>
  <ChapterLink href="04-lifetime-and-ownership">谁负责线程和数据的生命周期</ChapterLink>
  <ChapterLink href="05-jthread-and-stopping">让工作有序结束：std::jthread 与协作式停止</ChapterLink>
</ChapterNav>
