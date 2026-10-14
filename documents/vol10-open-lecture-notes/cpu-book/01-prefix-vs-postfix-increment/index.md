---
title: "i++ 与 ++i 生成的指令到底一样吗"
description: "i++ 与 ++i 在源码层只差一个符号的位置，落到指令层的故事要多不少。这个主题用同一份源码在 -O0 与 -O2 下的输出做对照，把自增的侧效应到底发生在哪一步讲清楚。"
sidebar_order: 1
---

# i++ 与 ++i 生成的指令到底一样吗

i++ 与 ++i 在源码层只差一个符号的位置，落到指令层的故事要多不少。这个主题用同一份源码在 -O0 与 -O2 下的输出做对照，把自增的侧效应到底发生在哪一步讲清楚。

<ChapterNav variant="sub">
  <ChapterLink href="01-int-instructions">i++ 与 ++i：int 在 -O0 下的汇编对比</ChapterLink>
  <ChapterLink href="02-class-type-cost">i++ 与 ++i：类类型的拷贝开销</ChapterLink>
  <ChapterLink href="03-vector-and-side-effects">i++ 与 ++i：vector 迭代器与副作用的边界</ChapterLink>
</ChapterNav>
