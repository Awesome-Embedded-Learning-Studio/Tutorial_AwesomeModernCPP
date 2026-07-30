---
title: "Cache-Friendly C++"
description: "CppCon 2025 演讲笔记 —— Jonathan Müller 讲 CPU 缓存机制与缓存友好的 C++ 代码，附 GCC 16.1.1 本机实测：vector 怎么摩擦 unordered_set、内存访问慢 100 倍、缩小类型未必更快"
conference: cppcon
conference_year: 2025
talk_title: 'Cache-Friendly C++'
speaker: "Jonathan Müller"
tags:
  - cpp-modern
  - host
  - intermediate
  - 优化
difficulty: intermediate
platform: host
cpp_standard: [20]
---

<TalkInfoCard
  talkTitle="Cache-Friendly C++"
  speaker="Jonathan Müller"
  conference="cppcon"
  :year="2025"
  videoYoutube="https://www.youtube.com/watch?v=g_X5g3xw43Q"
/>

这是 CppCon 2025 上 Jonathan Müller 的演讲笔记。Jonathan 长期做低延迟 C++（演讲时在 think-Cell，后来去了 LSEG 做 HFT 的行情数据通路），这场他从一个让很多人血压拉满的反直觉现象讲起:`std::unordered_set` 的查找是 O(1),`std::vector` 的线性查找是 O(n),可在真实机器上一跑,小数据量下 vector 居然把 unordered_set 摩擦了。答案只有一个方向:CPU 缓存。

这场演讲跟 [微基准测试那场](../07-microbenchmarks-that-lie/)是姊妹篇。那场的[第三篇](../07-microbenchmarks-that-lie/03-branch-prediction-cheats.md)已经点到分支预测器和缓存联手作弊,但没展开缓存本身。这一场就是把缓存这件事从根上讲透:它为什么存在、按什么单位搬运数据、各级延迟差多少、以及你在 C++ 里选容器、选数据类型、排结构体布局时,每一个决定是怎么落到缓存行为上的。

笔记拆成四篇,沿着"先打破复杂度迷信,再建缓存直觉,最后落到代码"的顺序走。所有实验都在同一台机器上跑:**Arch Linux / WSL2,AMD Ryzen 7 9700X(Zen 5),GCC 16.1.1,`-std=c++20`**,缓存层级是 L1d 每核 48KiB / L2 每核 1MiB / L3 32MiB 共享。你手上的数字会不一样,但结论的方向是一样的。

## 笔记目录

<ChapterNav variant="sub">
  <ChapterLink href="01-complexity-is-not-everything">复杂度不是一切:O(1) 怎么输给了 O(n)</ChapterLink>
  <ChapterLink href="02-memory-is-100x-slower">内存访问慢 100 倍是真的:缓存层级与缓存行</ChapterLink>
  <ChapterLink href="03-data-types-and-cache">数据类型也是缓存变量:缩小类型未必更快</ChapterLink>
  <ChapterLink href="04-writing-cache-friendly-code">写对缓存友好的代码:布局、对齐与决策</ChapterLink>
</ChapterNav>
