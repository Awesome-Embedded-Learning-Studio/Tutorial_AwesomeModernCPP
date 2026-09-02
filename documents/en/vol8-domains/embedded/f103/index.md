---
title: "STM32F103 + Renode"
description: "Each station puts modern C++ to work on one peripheral, simulator first — a Blue Pill is nice to have, never required"
platform: stm32f1
tags:
  - cpp-modern
  - intermediate
  - stm32f1
---

# STM32F103 + Renode

Everything in this tutorial runs inside the Renode simulator. You don't need to buy any hardware — a computer with the toolchain installed is enough. If you happen to have a Blue Pill (STM32F103C8T6), the verification on an actual board at the end of each station is there for you to follow; it's a bonus, never a gate.

Every station centers on one peripheral, and the core of it is modern C++ above the HAL: wheels already in the official library are used and explained, never rebuilt, while what the library lacks (debounce state machines, ring buffers, command parsing) gets written by hand. The LED station also takes you under the floor tiles for a look at the bare registers. The simulator verifies the behavior; disassembly verifies the zero cost.

## Roadmap

Content is being published progressively in this order:

1. **Getting started** — toolchain, first blink in Renode
2. **LED** — one look under the floor tiles at bare registers, then modern C++ above the HAL
3. **Buttons** — debouncing, state machines, variant
4. **UART** — interrupt-driven, ring buffer, expected
5. **Time** — SysTick, timers, PWM
6. **I2C** — sensor driver design
7. **Patterns** — object pools, intrusive containers, interrupt safety
8. **From the F103 to the F407** — new chip, same code: your C++ application layer, unchanged
