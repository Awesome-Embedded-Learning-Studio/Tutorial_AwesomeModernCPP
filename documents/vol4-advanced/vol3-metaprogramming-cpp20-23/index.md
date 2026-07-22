---
title: "元编程精要（C++20-23）"
description: "C++20/23 元编程：Concepts、requires 表达式、TMP 核心技巧、编译期字符串、C++26 反射"
---

# 元编程精要（C++20-23）

本部分接着 vol1 的模板基础往下走。vol1 讲的是模板的编译模型、特化、两阶段查找这些「机制」,这一部分讲的是怎么用这些机制做**编译期计算和类型推导**,以及 C++20 的 Concepts 如何把过去依赖 SFINAE、`enable_if` 的苦活改写成可读的约束。

三条主线:Concepts 和 requires 表达式(C++20)、经典模板元编程(TMP)技巧和它的现代化、编译期字符串与 C++26 反射。每篇都带「上手跑一跑」的真实编译输出,该踩的坑写成 `::: warning` 预警块,关键的编译期行为用汇编佐证。

配套可运行示例在 [code/examples/vol4/vol3-metaprogramming-cpp20-23/](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/examples/vol4/vol3-metaprogramming-cpp20-23),每个文件 `g++ -std=c++20 xxx.cpp` 直接跑。

<ChapterNav variant="sub">
  <ChapterLink href="01-concepts">Concepts:把模板约束写进签名</ChapterLink>
  <ChapterLink href="02-constraining-templates">使用 Concepts 约束模板:subsumption 与重载</ChapterLink>
  <ChapterLink href="03-requires-expressions">Requires 表达式深度解析:四种成分</ChapterLink>
</ChapterNav>

Concepts 三连(01-03)是这一部分的地基,讲清约束怎么写、怎么参与重载、requires 表达式的四种成分和它的两个坑。后续会接着讲:TMP 核心技巧(type_traits 内幕、`void_t` SFINAE、typelist、fold expressions,以及 SFINAE 往 concepts 的迁移)、编译期字符串处理(C++20 NTTP 的 class type 与 `fixed_string`)、C++26 静态反射(P2996 的 `^` 与 `[: :]`)、模板实例化控制、模板与异常安全,最后用一个 concepts 约束的 mini-STL 算法库把前面焊在一起。
