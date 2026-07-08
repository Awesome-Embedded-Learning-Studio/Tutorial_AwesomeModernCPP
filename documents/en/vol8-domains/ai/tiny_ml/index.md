---
title: "TinyInferCpp-Lab"
description: "A hands-on lab: build a toy neural-network inference engine from scratch in C++23"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
---

# TinyInferCpp-Lab

Build a toy neural-network inference engine from scratch in C++23 (Input → Dense → ReLU → Dense → Argmax): float32, fixed structure, no heap / no exceptions / no RTTI on the core path. For learning — not a replacement for TFLite Micro / STM32Cube.AI / CMSIS-NN / TinyMaix / NNoM / emlearn.

## Articles

- [Stage 0 · Project scaffold](./stage0/scaffold.md) — A standalone CMake23 project + a Catch2 smoke test; pour the toolchain foundation first.
- [Stage 1 · Fixed-dimension Tensor](./stage1/06-tensor.md) — A compile-time fixed-dimension, row-major, `std::array`-backed Tensor + `std::expected`.

## What's next (in progress)

Stage 2 (Dense layer & weight layout) → Stage 3 (ReLU / Argmax) → Stage 4 (DemoModel wiring) → Stage 5 (NumPy training & export) → Stage 6 (Python/C++ golden-test diff) → Stage 7 (embedded-friendliness audit) → Stage 8 (MCU porting plan). Companion project at `code/volumn_codes/vol8-labs/ai/tiny_ml/`.
