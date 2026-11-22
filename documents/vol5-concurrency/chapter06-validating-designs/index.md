---
title: "第六单元：怎样判断并发设计是否有效"
description: "从前五个单元的会写，走到证明写对：寻找并发错误、衡量并发收益、从测量回到设计"
---

# 第六单元：怎样判断并发设计是否有效

前五个单元把线程、共享、等待、任务与原子操作都安排上了，工具您已经用熟。本单元往后站一步，问验收的问题：并发设计写完之后，怎么证明它找得到错、量得出收益、调得动设计。单元内容按节分篇，正文五篇齐了。

## 本单元内容

<ChapterNav variant="sub">
  <ChapterLink href="01-finding-concurrency-bugs">竞争找出来：重复测试与 TSan</ChapterLink>
  <ChapterLink href="02-stop-path-and-diagnostics">停得下来吗：停止路径测试与诊断三件</ChapterLink>
  <ChapterLink href="03-measuring-benefits">量之前，先把条件立住：串行基线与测量的陷阱</ChapterLink>
  <ChapterLink href="04-benchmark-and-variables">基准工具与三个变量：Benchmark、规模、粒度、线程数</ChapterLink>
  <ChapterLink href="05-adjusting-design">怎样调整设计</ChapterLink>
</ChapterNav>
