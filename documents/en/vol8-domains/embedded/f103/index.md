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

## Chapter Navigation

Content is being published progressively in this order:

<ChapterNav>
  <ChapterLink num="0" href="00-env-setup/">Getting Started: Why C++, and by what right?</ChapterLink>
  <ChapterLink num="1" href="01-led/">LED: bare registers under the floor tiles, modern C++ above the HAL</ChapterLink>
  <ChapterLink num="2" href="02-button/">Buttons: debouncing, state machines, variant</ChapterLink>
  <ChapterLink num="3" href="03-uart/">UART: interrupt-driven, ring buffer, expected</ChapterLink>
  <ChapterLink num="4" href="04-time/">Time: SysTick, timers, PWM</ChapterLink>
  <ChapterLink num="5" href="05-i2c/">I2C: sensor driver design</ChapterLink>
  <ChapterLink num="6" href="06-patterns/">Patterns: object pools, intrusive containers, interrupt safety</ChapterLink>
  <ChapterLink num="7" href="07-f103-to-f407/">From the F103 to the F407: new chip, same code</ChapterLink>
</ChapterNav>
