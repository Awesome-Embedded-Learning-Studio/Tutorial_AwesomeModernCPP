---
title: "调试、测试与性能"
description: "用工具链保障并发程序的正确性与性能——从 ThreadSanitizer 到 Google Benchmark"
---

# 调试、测试与性能

写完并发代码不等于完事——你得确认它既正确又高效。并发 bug 有一种特别的阴险之处：它可能在你的开发机上跑一万次都没事，到了生产环境就隔三差五炸一次。而"性能好"这件事在并发语境下更是一个需要科学测量的工程问题，不是凭感觉说了算的。

这一章我们解决两个终极问题：第一，怎么用工具系统地发现和诊断并发 bug（data race、死锁、活锁、悬挂引用）；第二，怎么科学地测量并发程序的性能，避开 benchmark 里的各种陷阱。

## 本章内容

<ChapterNav variant="sub">
  <ChapterLink href="01-debugging-concurrency">并发程序调试技巧</ChapterLink>
  <ChapterLink href="02-concurrency-benchmarks">并发性能测试与基准</ChapterLink>
</ChapterNav>
