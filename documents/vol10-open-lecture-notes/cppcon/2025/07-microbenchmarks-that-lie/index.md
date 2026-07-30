---
title: "为什么 99% 的微基准测试都在撒谎"
description: "CppCon 2025 演讲笔记 —— Kris Jusiak 讲微基准测试里的编译器优化、噪声、偏差、分支预测与相关性陷阱，附 GCC 16.1.1 本机实测"
conference: cppcon
conference_year: 2025
talk_title: 'Why 99% of C++ Microbenchmarks Lie – and How to Write the 1% that Matter!'
speaker: "Kris Jusiak"
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
  talkTitle="Why 99% of C++ Microbenchmarks Lie – and How to Write the 1% that Matter!"
  speaker="Kris Jusiak"
  conference="cppcon"
  :year="2025"
  videoYoutube="https://www.youtube.com/watch?v=s_cWIeo9r4I"
/>

这是 CppCon 2025 上 Kris Jusiak 的演讲笔记。Kris 是 [Boost].UT 的作者，常年折腾编译期计算和测试框架，这场他盯住一个让人血压拉满的问题：你写了个 benchmark，跑出一个漂亮的纳秒数，然后照着优化、合代码、上线，结果线上纹丝不动，甚至更慢了。Kris 的回答很扎心——你那个 benchmark 大概率在撒谎，而且撒谎的不是一处，是好几处串在一起。

笔记拆成五篇，按"骗子一层层揭"的顺序走：先讲编译器怎么把你要测的循环直接优化没了，再讲噪声和偏差这两种性质完全不同的误差，接着拆分支预测器和缓存怎么联手给你一个假数字，然后是延迟与吞吐量这两个常被混为一谈的维度，最后落到最要命的一条——微基准变快和整个程序变快之间，根本不是一回事。

::: warning 关于本机环境
后面所有实验都在同一台机器上跑：**Arch Linux / WSL2，AMD Ryzen 7 9700X（Zen 5 架构），GCC 16.1.1，`-std=c++20`**。这台机器没绑核、没锁频、没关后台，就是一个随便跑的 WSL2 环境——这意味着噪声会比正经调过的环境大，但恰好能让"噪声长什么样"这件事看得更清楚。你手上的数字会不一样，结论的方向是一样的。
:::

## 笔记目录

<ChapterNav variant="sub">
  <ChapterLink href="01-compiler-ate-your-benchmark">编译器把你骗了：微基准的头号谎言</ChapterLink>
  <ChapterLink href="02-noise-vs-bias">噪声压得下去，偏差才是噩梦</ChapterLink>
  <ChapterLink href="03-branch-prediction-cheats">分支预测器在帮你作弊</ChapterLink>
  <ChapterLink href="04-latency-throughput-cycles">延迟、吞吐量、cycles：你到底在测什么</ChapterLink>
  <ChapterLink href="05-correlation-and-discipline">微基准变快，不代表程序变快</ChapterLink>
</ChapterNav>
