---
title: "Stage 1 · Fixed-dimension Tensor"
description: "The data foundation of the inference engine. A concept intro (01-05) plus the implementation main doc (06-tensor.md)"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
---

# Stage 1 · Fixed-dimension Tensor

This stage builds the inference engine's data foundation: a compile-time fixed-dimension, row-major, `std::array`-backed `Tensor<Rows, Cols, StorageType>`.

If the Tensor concept itself is new to you, read the five intro pieces (01-05) first — they walk from "what is a Tensor" all the way to "why it's designed this way". If you already know Tensors and just want the engineering, jump straight to [06-tensor.md](./06-tensor.md).

## Intro: what a Tensor is, and why it's built this way

1. [What is a Tensor — take the name off its pedestal](./01-what-is-tensor.md) — Demystify: it's just a fixed-size 2D table of numbers.
2. [What a Tensor holds in a neural network — four kinds of data, one container](./02-tensor-in-neural-network.md) — Input / weights / bias / output — all Tensors.
3. [Why not use what's already there — three suspects on trial](./03-why-not-built-in.md) — Where `vector`, native arrays, and nested `array` each die.
4. [Row-major — how a 2D coordinate lands in 1D memory](./04-row-major.md) — `i*Cols + j`, aligned with NumPy.
5. [Shape baked into the type — why dimensions are template parameters](./05-shape-in-type.md) — The type system as a free shape-checker.

## Implementation: writing the Tensor

- [The fixed-dimension Tensor — the inference engine's data foundation](./06-tensor.md) — Full interface sketch, `std::expected` error handling, CMake, verification tests, common pitfalls.

Once the five intro pieces are behind you, the design trade-offs in 06-tensor.md won't feel like they're hanging in mid-air.
