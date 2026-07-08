---
title: "TinyInferCpp-Lab"
description: "用 C++23 从零手搓一个玩具级神经网络推理器的动手 Lab"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
---

# TinyInferCpp-Lab

用 C++23 从零手搓一个玩具级神经网络推理器(Input → Dense → ReLU → Dense → Argmax),float32、固定结构、核心路径无堆分配无异常无 RTTI。教学目的,不替代 TFLite Micro / STM32Cube.AI / CMSIS-NN / TinyMaix / NNoM / emlearn。

## 文章

- [Stage 0 · 工程脚手架](./stage0/scaffold.md) —— standalone CMake23 工程 + Catch2 冒烟,把工具链地基浇好
- [Stage 1 · 固定维度 Tensor](./stage1/06-tensor.md) —— 编译期固定维度、行主序、`std::array` 存储的 Tensor + `std::expected`

## 后续(进行中)

Stage 2(Dense 与权重布局)→ Stage 3(ReLU / Argmax)→ Stage 4(DemoModel 串联)→ Stage 5(NumPy 训练导出)→ Stage 6(Python/C++ golden test 对拍)→ Stage 7(嵌入式友好审查)→ Stage 8(MCU porting 规划)。配套工程在 `code/volumn_codes/vol8-labs/ai/tiny_ml/`。
