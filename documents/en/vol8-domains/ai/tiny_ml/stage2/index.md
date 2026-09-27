---
title: "Stage 2 · Dense and Weight Layout"
description: "The inference engine's first layer that actually computes something. A Dense formula and weight-layout intro (01-02) plus the implementation main doc (03-dense.md)"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
translation:
  source: documents/vol8-domains/ai/tiny_ml/stage2/index.md
  source_hash: f701439488d46182ee98f9c98322190117faaf7935c6cef9cfe2b88f2d0cf1ec
  translated_at: '2026-09-25T08:48:22+00:00'
  engine: anthropic
  token_count: 600
---

# Stage 2 · Dense and Weight Layout

This stage builds the inference engine's first layer that truly "computes something": `Dense<In, Out>` — it takes in an input vector of length In and spits out an output vector of length Out, computing `y = W·x + b`. The activation (ReLU) is left for Stage 3; here we only do the affine part.

If you're still unfamiliar with what Dense actually computes or why the weights are `[Out, In]`, read the two intro pieces (01-02) first — they take the formula and the layout apart clearly. If you already know Dense and just want the engineering, jump straight to [03-dense.md](./03-dense.md).

## Intro: what Dense computes, and how the weights are laid out

1. [What Dense computes — one multiply-add, broken down per output](./01-what-is-dense.md) — Breaks `y=W·x+b` into Out weighted sums; the activation is left for Stage 3
2. [Why weights are [Out, In] — the cache ledger under row-major](./02-weight-shape.md) — Cache-friendly + aligned with PyTorch, the foundation for the Stage 5 cross-check

## Implementation: writing the Dense layer

- [The Dense layer — span views and weight layout](./03-dense.md) — span-view storage, the Stage 2→Stage 5 interface contract, CMake, verification tests, common pitfalls

Once the two intro pieces are behind you, the storage trade-offs in 03-dense.md won't feel like they're hanging in mid-air.
