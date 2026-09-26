---
title: "LED: bare registers under the floor tiles, modern C++ above the HAL"
description: "First light the LED with bare registers to see what the official library does, then relight it in modern C++ above the HAL, with disassembly proving zero overhead"
chapter: 1
order: 0
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - 寄存器
difficulty: intermediate
platform: stm32f1
translation:
  source: documents/vol8-domains/embedded/f103/01-led/index.md
  source_hash: 2ddeed9776561b8645a14f6c93e5375424a3a83830359174dc841d45dee0fe42
  translated_at: '2026-09-26T04:18:06+00:00'
  engine: anthropic
  token_count: 200
---

# LED: bare registers under the floor tiles, modern C++ above the HAL

> Status: planned

## Overview

The LED is the simplest peripheral, which makes it the perfect probe: first go under the floor tiles and light the lamp with bare registers to see what the official library is actually doing, then come back above the HAL and relight it in modern C++. The simulator verifies the behavior; disassembly verifies zero overhead.

## Chapter Navigation

> Content in progress — stay tuned.
