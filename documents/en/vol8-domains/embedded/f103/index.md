---
title: "STM32F103 + Renode (Act I)"
description: "Every peripheral refactored from C to C++23 one rung at a time, simulator first — a Blue Pill is nice to have, never required"
platform: stm32f1
tags:
  - cpp-modern
  - intermediate
  - stm32f1
---

# STM32F103 + Renode (Act I)

Everything in this line runs inside the Renode simulator. You don't need to buy any hardware — a computer with the toolchain installed is enough. If you happen to have a Blue Pill (STM32F103C8T6), the on-board verification at the end of each station is there for you to follow; it's a bonus, never a gate.

Every station centers on one peripheral and walks the same refactoring ladder: start from bare-metal C registers, then climb one rung at a time to type-safe wrappers, bitfields, templates, and C++23. Behavior stays identical at every rung while the code gets better; the simulator verifies the behavior, disassembly verifies the zero cost.

## Roadmap

Content is being published progressively in this order:

1. **Getting started** — toolchain, first blink in Renode
2. **LED refactoring ladder** — mmio wrappers → bitfields → templates → C++23
3. **Buttons** — debouncing, state machines, variant
4. **UART** — interrupt-driven, ring buffer, expected
5. **Time** — SysTick, timers, PWM
6. **I2C** — sensor driver design
7. **Patterns** — object pools, intrusive containers, interrupt safety
8. **The bridge** — from the F103 to the F407: your C++ layer, unchanged
