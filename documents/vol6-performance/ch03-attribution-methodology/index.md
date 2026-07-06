---
title: "归因方法论:从测量到瓶颈"
description: "测出慢之后要回答为什么慢、慢在哪。ch03 给四套互补的归因框架:USE 看系统全局、Roofline 判算力 vs 带宽、TMAM 四桶归到流水线哪段、火焰图+COZ+eBPF 落到代码,再用一篇实战把它们串成由粗到细的漏斗工作流"
---

# 归因方法论:从测量到瓶颈

ch01 教我们怎么把性能测准,ch02 给了硬件底座。但「测出慢」和「知道为什么慢」之间还隔着一整门学问:一个程序慢,可能是 CPU 算不过来,可能是数据搬不过来,可能在等锁、等 IO,可能根本是内存换页,**对症下药的前提是先确诊**。这一章就是把「确诊」做成一套可复用的流程。

四套工具按**由粗到细的漏斗**排列,每一步都比前一步贵(花的时间、需要的工具、侵入性都递增),但每一步都缩小了下一步的战场:

- **USE**(ch03-01):两分钟扫系统,排除「问题根本不在 CPU」。
- **Roofline**(ch03-01):一支笔算算术强度,判算力受限还是带宽受限,定优化大方向。
- **TMAM 四桶**(ch03-02):`toplev` 下钻,把瓶颈归到流水线的 Frontend / Backend Memory / Backend Core / Bad Speculation 哪一段,再精确采样到具体指令。
- **火焰图 + COZ + eBPF**(ch03-03):落到「哪个函数慢」,并用因果 profiler 排优化优先级。
- **实战 walk-through**(ch03-04):用一个加权点积的真实案例,把四套工具串成一遍完整流程。

归因这一章有个贯穿性的纪律:**瓶颈会迁移**。你修好 Backend Memory,可能露出 Bad Speculation;所以归因是迭代的,每改一处都要回测,而不是「跑一次 profiler 改一次」。定位准了,后面的 ch04「按瓶颈部位优化」才能对症下药。

> 本章的工具(`perf` / `toplev` / 火焰图脚本)大多是系统级 profiler。文章写于 WSL2 环境,本机没装这些,所以 profiler 的具体命令和输出**引自权威资料(Brendan Gregg、Bakhvalov、easyperf.net)并标注**,不假装在本机跑过;能本机实测的(算术强度、原始耗时、cache 行为)都标「实测」。命令本身是 Linux 性能分析的标准动作,换到装好工具的裸机上照样能用。

## 本章内容

<ChapterNav variant="sub">
  <ChapterLink href="03-01-use-and-roofline">USE 方法与 Roofline 模型</ChapterLink>
  <ChapterLink href="03-02-tmam-and-hw-sampling">TMAM 四桶与硬件采样:LBR / PEBS / Intel PT</ChapterLink>
  <ChapterLink href="03-03-flamegraph-perf">火焰图、perf 工作流与 COZ / eBPF</ChapterLink>
  <ChapterLink href="03-04-walkthrough">归因实战:从一个慢程序到定位瓶颈</ChapterLink>
</ChapterNav>
