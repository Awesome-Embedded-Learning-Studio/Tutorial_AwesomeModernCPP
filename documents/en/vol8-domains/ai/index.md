---
title: "AI & TinyML"
description: "Build a toy neural-network inference engine from scratch in modern C++, and understand how TinyML inference actually runs on an MCU"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
translation:
  source: documents/vol8-domains/ai/index.md
  source_hash: 69c93d82266f35304418692826957a28988fedd4b02aa0c941b5de393b771243
  translated_at: '2026-09-26T03:51:44+00:00'
  engine: anthropic
  token_count: 500
---

# AI & TinyML

This subdomain collects TAMCPP's AI / TinyML engineering labs. Like [Embedded Development](../embedded/) and [Networking](../networking/), you won't find concept quick-reference cards here — only hands-on projects of the "build a working thing from scratch" kind. The goal is to express neural-network inference in modern C++ in a way that's clear, controllable, and explainable, and along the way to weld `std::array` / `std::span` / `constexpr` / template dimension constraints / exception-free error handling / no heap allocation / static weights into one coherent piece.

## Projects

- [TinyInferCpp-Lab](./tiny_ml/) — Build a toy neural-network inference engine from scratch in C++23 (Input → Dense → ReLU → Dense → Argmax): float32, fixed structure, and a core path with no heap allocation, no exceptions, and no RTTI. PC closed loop first, with STM32 deployment as a follow-up stage.
