---
title: "现代模板技术（C++17）"
description: "C++17 给模板工程的现代工具:if constexpr、可变参数模板、完美转发、CTAD,以及类型擦除综合项目"
---

# 现代模板技术（C++17）

vol1 讲了模板的编译模型、特化、CRTP 这些「机制」。这一部分接着讲 C++17 给模板工程带来的几样现代工具:`if constexpr` 让模板里按类型分派不再依赖一摞重载,可变参数模板处理任意个参数,完美转发让泛型工厂把实参原样往里传,CTAD 让您少写一串尖括号。最后用一个类型安全的 `any` 把这几样焊在一起。

有个安排得先说清楚:类型萃取、SFINAE、`void_t`、fold expressions 这些 TMP 老技巧,放在了 vol3 元编程子卷的「TMP 核心技巧」一篇里(它是 concepts 的前身),这一部分不重复。

配套可运行示例在 [code/examples/vol4/vol2-modern-cpp17/](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/examples/vol4/vol2-modern-cpp17),每个文件 `g++ -std=c++17 xxx.cpp` 直接跑。

<ChapterNav variant="sub">
  <ChapterLink href="01-if-constexpr">if constexpr:编译期分支</ChapterLink>
  <ChapterLink href="02-variadic-templates">可变参数模板:参数包的展开</ChapterLink>
  <ChapterLink href="03-perfect-forwarding">完美转发:forwarding references 与引用折叠</ChapterLink>
  <ChapterLink href="04-ctad">CTAD:类模板参数推导</ChapterLink>
  <ChapterLink href="05-type-safe-any">综合项目:类型安全的 any</ChapterLink>
  <ChapterLink href="06-designated-initializers">指定初始化器</ChapterLink>
  <ChapterLink href="07-ranges-basics-and-views">C++20 范围库基础与视图</ChapterLink>
  <ChapterLink href="08-ranges-pipeline-in-practice">管道操作与 Ranges 实战</ChapterLink>
</ChapterNav>

01 到 05 是 C++17 模板线的主干:`if constexpr` 起手,讲清编译期分支为什么能替掉一摞重载;02 把参数包的展开机制拆开(递归、`if constexpr` 终止、fold 三种写法对照);03 讲完美转发和引用折叠,这是泛型工厂绕不开的机制;04 是 CTAD,讲编译器怎么从构造参数反推模板参数;05 用一个手写的类型安全 `any` 把前面四样收口。06 到 08 是这卷里另外几篇现代特性(designated initializers 和 Ranges),风格偏早期,和 01 到 05 的写法不完全一致,留待后续整理。
