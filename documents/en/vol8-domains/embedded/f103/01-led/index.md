---
title: "LED: bare registers under the floor tiles, modern C++ above the HAL"
description: "First the essence of an operation, spelled out: every line of peripheral code we write is a single write to a fixed address — MMIO, bus routing, struct overlays, cross-checked against a stretch of modern C++ pseudocode; then doing it by that essence: clock gating, the four-bits-per-pin configuration in CRL/CRH, atomic toggling with BSRR, measured register by register under Renode and GDB; hardware circuitry comes as an interlude at the back, then the climb from C-macro wrappers and the HAL's wrapping paper up to estdx's Gpio template, with zero overhead reconciled on the spot by disassembly"
chapter: 1
order: 0
tags:
  - stm32f1
  - beginner
  - 嵌入式
  - 寄存器
difficulty: beginner
platform: stm32f1
translation:
  source: documents/vol8-domains/embedded/f103/01-led/index.md
  source_hash: 0d9d7d6554adab18a55e788f5a23c590fc6cb54522993f2f0b7d498c5c9edb54
  translated_at: '2026-09-27T05:35:20+00:00'
  engine: anthropic
  token_count: 750
---

# LED: see the machine clearly, take the macros apart, climb to the types

Blinking an LED is the hello world of the microcontroller world, but this stop is in no hurry to blink. The first piece looks at the Cortex-M3 inside the BluePill from the inside out: how the two manuals split their jobs now that ARM sells blueprints but fabricates no chips, the core's whole estate of a handful of registers plus a three-stage pipeline, and the 4GB address map the architecture has fixed in stone — then the Renode routing logs bring the gavel down on "the CPU offers the outside world nothing but reads and writes". The suspense of a full-firmware instruction census is planted in that piece too, to be cashed in by disassembly in the second — every claim carries a manual citation, and every key step actually runs and reproduces.

With the machine in plain sight, we light the lamp from scratch and answer a more fundamental question: when we type `GPIOC->CRH = x`, what essentially happens? The answer: a single write to a fixed memory address, which the bus routes to a bank of switching circuits in the peripheral. That one sentence is the foundation of all embedded peripheral programming, and the second piece takes it apart and grinds it fine, with a stretch of modern C++ pseudocode as the cross-check.

With the foundation laid, we do it by that essence: RCC's clock gating answers "is this peripheral powered on", the four bits per pin in CRL/CRH answer "which gear is this piece of circuit twisted to", and ODR and BSRR answer "how do we flip a pin over" — we touch every one of those registers by hand with Renode and GDB, including one genuine crash: the configuration written into the wrong half of a register, and the LED dies on you out of spite. Then a hardware interlude makes the circuit inside a pin clear: push-pull, open-drain, Schmitt triggers, pull-up and pull-down — look back at those configuration bits and they are all knobs for choosing gears on real transistors. Finally, the staircase: what hidden wounds the C-macro-era wrappers left behind, what is rolled up inside the HAL's wrapping paper, all the way up to the Gpio template in `estdx` — port, pin, direction, and polarity all written into types, so misconfigured code is pinned down by the compiler the very second you hit Enter. Every layer's bill is settled on the spot with `size` and disassembly: zero overhead is not a slogan, it is an instruction count.

## Chapter Navigation

<ChapterNav variant="sub">
  <ChapterLink href="01-cortex-m3-anatomy">Before the first blink: what the Cortex-M3 machine looks like</ChapterLink>
  <ChapterLink href="02-mmio-essence">The essence of an operation: what we write is code, what we touch is addresses</ChapterLink>
  <ChapterLink href="03-rcc-clock-gate">Clock gating: when the LED will not light, start from the clock</ChapterLink>
  <ChapterLink href="04-gpio-registers">From configuration to toggling: four bits per pin, and atomic set/reset</ChapterLink>
  <ChapterLink href="05-gpio-circuit">Interlude: a pin is a circuit — push-pull, open-drain, and Schmitt triggers</ChapterLink>
  <ChapterLink href="06-c-macro-led">An LED driver from the C-macro era: it runs, but let's put the costs on the table</ChapterLink>
  <ChapterLink href="07-hal-unwrapped">Unwrapping the HAL: which bits GPIO_Init writes on our behalf</ChapterLink>
  <ChapterLink href="08-gpio-template">Putting configuration into types: the Gpio template and compile-time verification</ChapterLink>
  <ChapterLink href="09-wrap-up">From blinking to using it well: the subtleties of toggle, and the door to the next stop</ChapterLink>
</ChapterNav>
