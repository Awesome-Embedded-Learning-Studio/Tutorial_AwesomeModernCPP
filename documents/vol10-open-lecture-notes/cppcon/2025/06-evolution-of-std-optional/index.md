---
title: "The Evolution of std::optional: From Boost to C++26"
description: "CppCon 2025 演讲笔记 —— Steve Downey 讲 std::optional 从 Boost 到 C++26 的演进，聚焦 optional 引用（P2988）为何等了二十年才进标准"
conference: cppcon
conference_year: 2025
talk_title: 'The Evolution of std::optional: From Boost to C++26'
speaker: "Steve Downey"
tags:
  - cpp-modern
  - host
  - intermediate
  - optional
difficulty: intermediate
platform: host
cpp_standard: [17, 23, 26]
---

<TalkInfoCard
  talkTitle="The Evolution of std::optional: From Boost to C++26"
  speaker="Steve Downey"
  conference="cppcon"
  :year="2025"
/>

这是 CppCon 2025 上 Steve Downey（Bloomberg）的演讲笔记。他是把 `std::optional<T&>` 推进 C++26 的提案 P2988 的主要作者。这场演讲的核心是回答一个问题：一个看起来"不就是能装空值的引用嘛"的特性，为什么从 2005 年第一次提出，一直拖到 2025 年 6 月的 Sofia 会议才投票通过。答案绕不开引用在 C++ 里的三重身份、assign-through 和 rebind 的二十年之争，以及最终"它就是个带约束的指针"这个结论。

笔记拆成六篇，沿着"先讲值版本底子，再进引用版本核心，最后跳出来看标准化"的顺序展开。所有涉及 `optional<T&>` 的代码都在 GCC 16.1.1（`-std=c++26`）上实跑过，不是纸上谈兵。

## 笔记目录

<ChapterNav variant="sub">
  <ChapterLink href="01-why-optional-reference-took-20-years">为什么 optional 引用折腾了二十年</ChapterLink>
  <ChapterLink href="02-value-semantics-of-optional">std::optional 的值语义底子</ChapterLink>
  <ChapterLink href="03-optional-reference-and-assignment">optional 引用是什么，以及赋值为什么一定是重绑定</ChapterLink>
  <ChapterLink href="04-shallow-traps-const-value-or-dangling">optional 引用的浅层陷阱：const、value_or 与悬垂</ChapterLink>
  <ChapterLink href="05-move-semantics-traps">optional 引用里藏着的移动语义陷阱</ChapterLink>
  <ChapterLink href="06-standardization-and-beman">标准化真相：The Beman Project 与一份能跑的参考实现</ChapterLink>
</ChapterNav>
