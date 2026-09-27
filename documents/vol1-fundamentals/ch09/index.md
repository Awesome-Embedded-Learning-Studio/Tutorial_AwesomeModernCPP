---
title: "模板初步"
description: "函数模板、类模板和模板特化的入门"
---

# 模板初步

模板是 C++ 实现泛型编程的核心机制——写一份代码，适配多种类型。这一章先看模板怎么把类型变成编译期的参数，让一份逻辑伺候多种类型，顺带把模板和上一章虚函数的分工划清楚；接着从函数模板入手，看看编译器怎么帮咱们自动推导类型参数；然后过渡到类模板，理解它和普通类的区别；最后了解一下模板特化，知道当您需要对某个类型做特殊处理时该怎么做。这里是模板的入门，后面高级卷会深入展开。

## 本章内容

<ChapterNav variant="sub">
  <ChapterLink href="00-why-generics">为什么需要模板</ChapterLink>
  <ChapterLink href="01-function-templates">函数模板</ChapterLink>
  <ChapterLink href="02-class-templates">类模板</ChapterLink>
  <ChapterLink href="03-specialization-basics">模板特化初步</ChapterLink>
</ChapterNav>
