---
title: "模板基础（C++11-14）"
description: "C++ 模板编程的核心基础:从函数模板到 CRTP 的完整入门"
---

# 模板基础（C++11-14）

C++ 模板是泛型编程的核心机制。本部分从「会用模板」推进到「想写库、读得懂 STL 源码」的视角,讲透模板的编译模型、特化与偏特化、非类型参数、两阶段名字查找、隐藏友元、别名模板和 CRTP,最后用一个 `fixed_vector<T, N>` 综合项目把前九篇焊在一起。

每篇都带「上手跑一跑」的真实编译输出,该踩的坑写成 `::: warning` 预警块,关键的编译期行为用汇编佐证。

配套可运行示例在 [code/examples/vol4/vol1-basics-cpp11-14/](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/examples/vol4/vol1-basics-cpp11-14),四个最有复用价值的例子(fixed_vector、CRTP 静态多态、Comparable mixin、手写 type_traits),每个文件 `g++ -std=c++20 xxx.cpp` 直接跑。

<ChapterNav variant="sub">
  <ChapterLink href="01-templates-introduction">模板导论:从一份代码配方说起</ChapterLink>
  <ChapterLink href="02-function-templates-deep">函数模板深化:编译模型与那个不能偏特化的坑</ChapterLink>
  <ChapterLink href="03-class-templates">类模板:成员、依赖名与惰性实例化</ChapterLink>
  <ChapterLink href="04-specialization-partial">模板特化与偏特化:模式匹配的艺术</ChapterLink>
  <ChapterLink href="05-non-type-parameters">非类型模板参数:从整数到 C++20 的浮点与类类型</ChapterLink>
  <ChapterLink href="06-name-lookup-and-adl">名字查找与 ADL:两阶段查找是怎么回事</ChapterLink>
  <ChapterLink href="07-friends-and-barton-nackman">模板友元与 Barton-Nackman:隐藏友元技巧</ChapterLink>
  <ChapterLink href="08-alias-and-using">别名模板与 using 声明:给类型起个短名字</ChapterLink>
  <ChapterLink href="09-crtp">CRTP:用奇递归模板做静态多态</ChapterLink>
  <ChapterLink href="10-fixed-vector">综合项目:fixed_vector</ChapterLink>
</ChapterNav>
