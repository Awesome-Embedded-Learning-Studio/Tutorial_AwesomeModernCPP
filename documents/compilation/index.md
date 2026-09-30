---
title: "编译与链接深入"
description: "C/C++ 编译、链接、静态库、动态库、符号可见性的底层机制——懂了这些，链接报错会排查、库会设计、性能能优化"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
---

# 编译与链接深入

笔者记得，经典的 C / C++ 教程非常多，但是的确鲜有将“编译与链接”相关的，这里统筹为 **“编译技术”** 这个领域的相关知识作为 C/C++ 学习旅程中的一部分的教程。

如果您对这部分内容有些困惑，不太清楚他是做什么的。笔者可以用一句话说明：说明白我们的 C / C++ 源文件是如何在操作系统层面上成为一个可执行文件，或者是可分发的库。

> 最实用的说，是解决**我们遇到工程上构建的困难（诸如未定义的错误，重复定义的错误和其他衍生的奇奇怪怪的问题）** 。如果您惯用一些构建系统，但是对自己在使用构建系统中完成对工程文件组织后的构建步骤**出现出乎自己意料的如上错误**。那么，**“编译技术”**就是您需要的

彻底理解这些，我们必须要理解编译器和链接器在背后到底干了什么：源码怎么变成可执行文件、静态库和动态库的差别、符号怎么被找到又怎么找不到、`undefined reference` 这类报错卡在哪一步。读完您能排查链接报错、设计自己的动态库 ABI、看懂 GOT/PLT 这类底层机制（当然这个犯不着，知道就行）。不理解，我们就没办法正确的向构建系统传达我们的意图，抽象就会别扭，维护就会困难，你就要找LLM胡乱解决一通。

> 新手先看 [新手起步卷](/getting-started/) 把环境跑通。这一卷是机制深潜，适合已过卷一基础、想搞懂「为什么」的读者。配 [卷七·工程实践](/vol7-engineering/) 的 CMake 进阶一起读，机制和工具两不耽误。

## 章节导航

<ChapterNav>
  <ChapterLink num="1" href="01-compilation-and-linking-overview">编译与链接导论：undefined reference 是怎么来的</ChapterLink>
  <ChapterLink num="2" href="02-reuse-concept">复用的本质：从源码级到二进制级</ChapterLink>
  <ChapterLink num="3" href="03-creating-and-using-static-libs">静态库：用 ar 打包，用 -l/-L 链接</ChapterLink>
  <ChapterLink num="4" href="04-dynamic-libraries-1">动态库（上）：为什么必须有 -fPIC</ChapterLink>
  <ChapterLink num="5" href="05-dynamic-library-design">动态库设计：ABI 与跨工具链接口</ChapterLink>
  <ChapterLink num="6" href="06-symbol-visibility">符号可见性：控制动态库导出什么</ChapterLink>
  <ChapterLink num="7" href="07-symbol-missing-and-runtime-loading">符号缺失与运行时加载：dlopen 与 LoadLibrary</ChapterLink>
  <ChapterLink num="8" href="08-library-search-logic">库搜索逻辑：链接期与运行期怎么找库</ChapterLink>
  <ChapterLink num="9" href="09-dynamic-library-details">动态库细节：PLT/GOT 延迟绑定与符号介入</ChapterLink>
  <ChapterLink num="10" href="10-dynamic-lib-as-executable">番外：动态库能当可执行文件跑吗</ChapterLink>
</ChapterNav>
