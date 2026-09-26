---
title: "STM32F103 + Renode"
description: "Each station puts modern C++ to work on one peripheral, with the Renode simulator going first so you can follow along without a board; disassembly is in charge of proving zero overhead"
platform: stm32f1
tags:
  - cpp-modern
  - intermediate
  - stm32f1
translation:
  source: documents/vol8-domains/embedded/f103/index.md
  source_hash: 4da9532e584344d04389a707519032a99a57cc64cff17cdf910c617bce8a69fe
  translated_at: '2026-09-26T04:23:24+00:00'
  engine: anthropic
  token_count: 600
---

# STM32F103 + Renode

This entire tutorial runs inside the Renode simulator. You don't need to buy any hardware — a computer with the toolchain installed is enough. If you have a Blue Pill (STM32F103C8T6) on hand, even better: the end-of-station verification on a real board is there for you to follow along. It's a nice bonus, and not having one won't block your way.

Each station centers on one peripheral, and the main storyline is writing the modern C++ application layer above the HAL: for wheels the library already provides, we only explain, never reinvent; for what the library lacks (debounce state machines, ring buffers, command parsing, and the like), we write it ourselves. The LED station also takes us down under the floor tiles to see what the bare registers and the official library are actually doing. The simulator is in charge of verifying functionality; disassembly is in charge of verifying zero overhead.

## Chapter Navigation

Content is coming online progressively in this order — the Getting Started station is already up, and the rest will follow:

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
