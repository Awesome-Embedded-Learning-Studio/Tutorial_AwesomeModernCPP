---
title: "嵌入式开发"
description: "现代 C++ 嵌入式主线:Renode 模拟器先行,从 STM32F103 走到 STM32F407,不用买板子也能从头跟到尾"
---

# 嵌入式开发

TAMCPP的初衷就是为了做嵌入式的C++开发，咱们终于走到了这里。现在，还请您深呼吸，准备开始我们的旅程！

这套教程回答一个问题:现代 C++ 拿到单片机上,到底该怎么用、能好到什么程度。主角是 C++,芯片是道具。所以咱们从门槛最低的 STM32F103 起步,一路走到资源宽裕的 STM32F407,同一套 C++ 写法在两块芯片上各走到头。F103 走完之后还有一条更深的线:把 F103 线里当依赖用的那个 RTOS——ZerOS——按 commit 时序亲手实现一遍,从空仓库走到 v0.1.0。

和多数嵌入式教程的另一个不同:**Renode 模拟器先行**。您手头没有单片机,也能把每段代码跑起来、验证对;实际板子验证放在每站的最后,有板子跟着做,没板子不耽误学。

## 章节导航

<ChapterNav variant="sub">
  <ChapterLink href="f103/">STM32F103 + Renode</ChapterLink>
  <ChapterLink href="zeros/">手搓 ZerOS:按 commit 时序,从裸机到 RTOS</ChapterLink>
  <ChapterLink href="f407/">STM32F407 进阶 — 规划中</ChapterLink>
</ChapterNav>

::: tip 正在重构中
内容正在按新结构逐步翻新,您从起步篇开始跟,后面陆续上线。老版 STM32F103 教程(HAL 库起步)已归档,它的知识内核会融合进新教程的对应站点;起步篇已随配套外设库 libestdx 重新落地。
:::
