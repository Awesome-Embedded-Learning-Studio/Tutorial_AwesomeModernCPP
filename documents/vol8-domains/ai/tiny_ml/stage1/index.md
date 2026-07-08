---
title: "Stage 1 · 固定维度 Tensor"
description: "推理器的数据底座。Tensor 概念引入(01-05)+ 实现主文档(06-tensor.md)"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
---

# Stage 1 · 固定维度 Tensor

这一 stage 造整个推理器的数据底座:一个编译期固定维度、行主序、`std::array` 存储的 `Tensor<Rows, Cols, StorageType>`。

如果你对 Tensor 这个概念本身还陌生,先读 01-05 五篇引入,它从"Tensor 是什么"一路讲到"为什么这么设计";已经熟悉 Tensor、只想看工程实现的,可以直接跳到 [06-tensor.md](./06-tensor.md)。

## 引入:Tensor 是什么、为什么这么造

1. [Tensor 是什么——先把名字从神坛上拿下来](./01-what-is-tensor.md) —— 祛魅:它就是一张固定大小的二维数表
2. [Tensor 在神经网络里装什么——四种数据,一种容器](./02-tensor-in-neural-network.md) —— 输入 / 权重 / 偏置 / 输出,全是 Tensor
3. [为什么不用现成的——三个候选过堂](./03-why-not-built-in.md) —— vector、原生数组、嵌套 array 各死在哪
4. [行主序——二维坐标怎么落进一维内存](./04-row-major.md) —— `i*Cols + j`,跟 NumPy 对齐
5. [形状塞进类型——维度为什么是模板参数](./05-shape-in-type.md) —— 类型系统当免费的形状检查器

## 实现:动手写 Tensor

- [固定维度 Tensor——推理器的数据底座](./06-tensor.md) —— 完整接口草图、`std::expected` 错误处理、CMake、验证测试、常见坑

读完引入五篇,06-tensor.md 里那些设计取舍读起来就不再悬空了。
