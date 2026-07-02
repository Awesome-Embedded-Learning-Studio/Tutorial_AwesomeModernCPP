---
title: "性能思维与正确性前置"
description: "建立性能优化的思维方式:efficiency 与 performance 的区别、两条铁律、Amdahl 天花板,以及 sanitizer 作为正确性地基"
---

# 性能思维与正确性前置

笔者认为,性能优化是 C++ 工程能力里最容易「自信地犯错」的一块。微架构的复杂度远远跑在人的直觉前面,凭感觉改代码,十有八九是在优化那 5% 的部分,而真正的瓶颈在另外 95% 里躺着。所以本卷的第一件事不是教你任何一条优化技巧,而是先把思维方式立起来——**先正确,再快;先测量,再优化**。

这一章我们做三件事:用一段同是 $O(\log n)$ 的查找代码,讲透 **efficiency(算法复杂度)和 performance(硬件上的真实表现)为什么不是一回事**;立下贯穿全卷的两条铁律和 Amdahl 天花板;并把 sanitizer 工具链安顿成「正确性地基」——没有正确性兜底的性能数字,一律不可信。

本章是整卷的命题入口,ch01 的 Benchmark 方法论会从这里接过去,把「我觉得」换成「我测过」。

## 本章内容

<ChapterNav variant="sub">
  <ChapterLink href="01-efficiency-vs-performance">性能思维:efficiency 与 performance 不是一回事</ChapterLink>
  <ChapterLink href="02-from-correctness-to-performance">从「先正确」到「再快」:为什么 sanitizer 是性能卷的地基</ChapterLink>
</ChapterNav>
