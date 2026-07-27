---
title: "Stage 2 · Dense 与权重布局"
description: "推理器第一个会算东西的层。Dense 公式与权重布局引入(01-02)+ 实现主文档(03-dense.md)"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
---

# Stage 2 · Dense 与权重布局

这一 stage 造推理器第一个真正"会算东西"的层:`Dense<In, Out>`,吃一个长度 In 的输入向量,吐一个长度 Out 的输出向量,算的是 `y = W·x + b`。激活(ReLU)留到 Stage 3,这里只做仿射。

如果你对 Dense 在算什么、权重为什么是 `[Out, In]` 还陌生,先读 01-02 两篇引入,它们把公式和布局拆清楚;已经熟悉、只想看工程实现的,可以直接跳到 [03-dense.md](./03-dense.md)。

## 引入:Dense 在算什么、权重怎么摆

1. [Dense 在算什么——一次乘加,拆到每个输出](./01-what-is-dense.md) —— 把 `y=W·x+b` 拆成 Out 个加权和,激活留 Stage 3
2. [权重为什么是 [Out, In]——行主序下的 cache 账](./02-weight-shape.md) —— cache 友好 + 跟 PyTorch 对齐,Stage 5 对拍的基础

## 实现:动手写 Dense

- [Dense 层——span 视图与权重布局](./03-dense.md) —— span 视图存储、Stage 2→Stage 5 接口契约、CMake、验证测试、常见坑

读完引入两篇,03-dense.md 里那些存储取舍读起来就不再悬空了。
