---
title: "第一单元：让程序开始并发执行"
description: "从日常例子认识并发，用时间线理解 CPU 与线程，再动手启动、等待和停止线程。"
---

# 第一单元：让程序开始并发执行

您可以先从下载讲义时还能继续翻网页这件小事入手，看看程序里的几件事怎样一起推进。咱们画过 CPU 和线程的时间线，就来写第一个 `std::thread` 程序，然后学怎么等线程结束、怎么让它有序停下来。

## 本单元内容

<ChapterNav variant="sub">
  <ChapterLink href="00-why-concurrency-unavoidable">为什么躲不开并发</ChapterLink>
  <ChapterLink href="01-work-and-order">讲义还没下完，列表怎么就能滚了</ChapterLink>
  <ChapterLink href="02-cpu-and-threads">一个 CPU 核心怎么轮着干活</ChapterLink>
  <ChapterLink href="03-first-std-thread">第一个 std::thread 程序</ChapterLink>
  <ChapterLink href="04-lifetime-and-ownership">谁负责数据的生命周期</ChapterLink>
  <ChapterLink href="05-thread-lifetime-and-raii">谁负责线程的生命周期</ChapterLink>
  <ChapterLink href="06-jthread-and-stopping">让工作有序结束：std::jthread 与停止三件套</ChapterLink>
  <ChapterLink href="07-stop-callback-and-checklist">注册竞态与退出检查表：停止协议的细节</ChapterLink>
</ChapterNav>

## 选读

<ChapterNav variant="sub">
  <ChapterLink href="08-when-concurrency">什么时候值得引入并发</ChapterLink>
  <ChapterLink href="09-what-is-a-thread">线程在机器上是什么</ChapterLink>
</ChapterNav>
