---
title: "Benchmark 方法论"
description: "vol6 全卷锚点章:从「microbenchmark 为什么会骗你」出发,建立测量、统计、生产与 CI 回归的完整方法论,后续每篇性能文章都回引这一章"
---

# Benchmark 方法论

这是整卷的**锚点章**。ch00 立下了「先正确、再快」和「先测量、再优化」两条铁律,但「先测量」这三个字背后藏着一整门学问:性能不是一个布尔量,而是一个分布;一次性的手搓测量几乎没有意义;microbenchmark 这个最趁手的工具,恰恰也最会骗人。

这一章把「测得准」这件事彻底拆开:先认清 microbenchmark 的三类欺骗,再学怎么写一个不骗人的微基准(`DoNotOptimize` 的语义、参数扫描、重复聚合),然后一份 16 条环境就绪 checklist 把噪声源逐个关掉,接着用统计方法把分布变成可信的结论,最后讲怎么把测量搬进生产和 CI 里持续守护性能。后面每一篇性能文章,开头都会回引这一章的规矩——就像 vol5 用 TSan 贯穿并发正确性一样。

如果你只读这一卷的少数几篇,这一章应该占大半。

## 本章内容

<ChapterNav variant="sub">
  <ChapterLink href="01-why-microbenchmarks-lie">为什么 microbenchmark 会骗你</ChapterLink>
  <ChapterLink href="02-credible-microbenchmark">怎么写一个可信的 microbenchmark</ChapterLink>
  <ChapterLink href="03-pitfalls-and-env">测量陷阱与环境就绪:16 条 checklist</ChapterLink>
  <ChapterLink href="04-statistics-and-reporting">统计与报告:把分布变成结论</ChapterLink>
  <ChapterLink href="05-production-and-ci">生产测量与 CI 性能回归检测</ChapterLink>
  <ChapterLink href="06-methodology-reference">Benchmark 方法论参考卡</ChapterLink>
</ChapterNav>
