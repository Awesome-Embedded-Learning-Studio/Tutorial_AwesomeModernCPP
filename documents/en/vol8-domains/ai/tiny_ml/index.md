---
title: "TinyInferCpp-Lab"
description: "A hands-on lab: hand-roll a toy neural-network inference engine from scratch in C++23"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
translation:
  source: documents/vol8-domains/ai/tiny_ml/index.md
  source_hash: 9242b14b90806e1dd1d158dbc6dd361db1a420952737d41cef4df825dac2c938
  translated_at: '2026-09-26T03:53:52+00:00'
  engine: anthropic
  token_count: 280
---

# TinyInferCpp-Lab

We hand-roll a toy neural-network inference engine from scratch in C++23 (Input → Dense → ReLU → Dense → Argmax): float32, fixed structure, and a core path with no heap allocation, no exceptions, and no RTTI. It's a teaching project — not a replacement for TFLite Micro / STM32Cube.AI / CMSIS-NN / TinyMaix / NNoM / emlearn.

## Articles

- [Stage 0 · Project scaffold](./stage0/scaffold.md) — A standalone CMake23 project + a Catch2 smoke test; pour the toolchain foundation first.
- [Stage 1 · Fixed-dimension Tensor](./stage1/06-tensor.md) — A compile-time fixed-dimension, row-major, `std::array`-backed Tensor + `std::expected`.
- [Stage 2 · Dense and Weight Layout](./stage2/03-dense.md) — The fully-connected layer `y=W·x+b`, `std::span` view storage, weights laid out `[Out,In]`.

## What's next (in progress)

Stage 3 (ReLU / Argmax) → Stage 4 (DemoModel wiring) → Stage 5 (NumPy training & export) → Stage 6 (Python/C++ golden-test cross-check) → Stage 7 (embedded-friendliness audit) → Stage 8 (MCU porting plan). The companion project lives at `code/volumn_codes/vol8-labs/ai/tiny_ml/`.
