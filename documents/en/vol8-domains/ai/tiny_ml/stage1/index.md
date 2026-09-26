---
title: "Stage 1 · Fixed-dimension Tensor"
description: "The inference engine's data foundation. Tensor concept intro (01-05) + the main implementation doc (06-tensor.md)"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
translation:
  source: documents/vol8-domains/ai/tiny_ml/stage1/index.md
  source_hash: f08c61117583027ee622d0e666955570e49f74fcd07eceea94ccdcb93066e0bf
  translated_at: '2026-09-26T03:56:53+00:00'
  engine: anthropic
  token_count: 1100
---

# Stage 1 · Fixed-dimension Tensor

In this stage we build the data foundation for the entire inference engine: a `Tensor<Rows, Cols, StorageType>` with compile-time fixed dimensions, row-major layout, and `std::array` storage.

If the concept of a Tensor itself is still new to you, read the five intro articles (01-05) first — they walk from "what is a Tensor" all the way to "why it is designed this way". If you already know Tensors and only want the engineering, jump straight to [06-tensor.md](./06-tensor.md).

## Introduction: what a Tensor is, and why we build it this way

1. [What is a Tensor — take the name off its pedestal](./01-what-is-tensor.md) — demystified: it is just a fixed-size two-dimensional table of numbers
2. [What a Tensor holds in a neural network — four kinds of data, one container](./02-tensor-in-neural-network.md) — inputs / weights / biases / outputs, all of them are Tensors
3. [Why not use what's already there — three suspects on trial](./03-why-not-built-in.md) — where vector, raw arrays, and nested arrays each fall over
4. [Row-major — how a 2D coordinate lands in 1D memory](./04-row-major.md) — `i*Cols + j`, aligned with NumPy
5. [Shape baked into the type — why dimensions are template parameters](./05-shape-in-type.md) — the type system as a free shape checker

## Implementation: writing the Tensor ourselves

- [The fixed-dimension Tensor — the inference engine's data foundation](./06-tensor.md) — a full interface sketch, `std::expected` error handling, CMake, verification tests, and common pitfalls

Once you have read the five intro articles, the design trade-offs in 06-tensor.md will no longer feel like they come out of nowhere.
