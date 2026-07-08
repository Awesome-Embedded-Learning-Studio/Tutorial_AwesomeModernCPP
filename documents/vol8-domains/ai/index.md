---
title: "AI 与 TinyML"
description: "用现代 C++ 从零手搓玩具级神经网络推理器,理解 TinyML 推理在 MCU 上到底怎么跑"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
---

# AI 与 TinyML

这个子域收录 TAMCPP 的 AI / TinyML 方向工程 Lab。和 [嵌入式开发](../embedded/)、[网络编程](../networking/) 一样,这里不放概念速查,只放"从零造一个能跑的东西"的动手项目——目标是用现代 C++ 把神经网络推理这件事表达得清晰、可控、可解释,顺便把 `std::array` / `std::span` / `constexpr` / 模板维度约束 / 无异常错误处理 / 无堆分配 / 静态权重这些点焊在一起。

## 项目

- [TinyInferCpp-Lab](./tiny_ml/) —— 用 C++23 从零手搓一个玩具级神经网络推理器(Input → Dense → ReLU → Dense → Argmax),float32、固定结构、核心路径无堆分配无异常无 RTTI。PC 闭环优先,STM32 部署作为后续环节。
