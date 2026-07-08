---
title: "AI & TinyML"
description: "Build a toy neural-network inference engine from scratch in modern C++, and understand how TinyML inference actually runs on an MCU"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
---

# AI & TinyML

This subdomain collects TAMCPP's AI / TinyML hands-on labs. Like [Embedded Development](../embedded/) and [Networking](../networking/), this isn't the place for concept reference cards — it's for "build a working thing from scratch" projects. The goal is to express neural-network inference in modern C++ in a way that's clear, controllable, and explainable, while pulling `std::array` / `std::span` / `constexpr` / template dimension constraints / exception-free error handling / no heap allocation / static weights into one coherent piece.

## Projects

- [TinyInferCpp-Lab](./tiny_ml/) — Build a toy neural-network inference engine from scratch in C++23 (Input → Dense → ReLU → Dense → Argmax): float32, fixed structure, no heap / no exceptions / no RTTI on the core path. PC closure first, STM32 deployment as a follow-up.
