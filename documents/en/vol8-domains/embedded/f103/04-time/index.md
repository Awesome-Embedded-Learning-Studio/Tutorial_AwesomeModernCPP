---
title: "Time: SysTick, timers, PWM"
description: "From the SysTick heartbeat into general-purpose timers and on to PWM output, turning time into a programmable resource"
chapter: 4
order: 0
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - 外设管理
difficulty: intermediate
platform: stm32f1
translation:
  source: documents/vol8-domains/embedded/f103/04-time/index.md
  source_hash: 9431bd55e1d56191fe0f01e0e8f4777089feb8c4867764f410a79452f9b8815d
  translated_at: '2026-11-01T03:20:13+00:00'
  engine: anthropic
  token_count: 400
---

# Time: SysTick, timers, PWM

## Overview

This station has so far landed the first five pieces of the time theme. Piece one takes the TIM family as its main line: tim.hpp's three-piece set (TimBase period beats, TimOneShot one-shot windows, TimPwm duty-cycle output) has a consteval solver derive PSC and ARR from the period, the APB1×2 rule for timclk and the defense against the spurious UIF are wrapped up in two examples, 09_tim_heartbeat pairs a 1Hz heartbeat with a WFI main loop, and 10_button_confirm uses an EXTI edge to start a 20ms one-shot confirmation window, checked head-to-head against the polling debounce chain. Piece two puts configuration into the type: designated initializers used directly as template arguments, typos reported at compile time, the configuration folded into immediates in the instruction stream, viewed side by side with the HAL's handle route. Piece three computes the range: at 64MHz, a 16-bit PSC times a 16-bit ARR covers periods from 1µs to 67.108864s, the approximate path's error is capped at 15ppm, and what the 0.1% precision gate actually stops are configurations outside the range. Piece four dissects NVIC preemption priorities: tim.hpp's CMSIS single-argument form and the HAL two-argument form of exti and uart coexist in one firmware, with the library's stance giving all 16 levels to preemption; the Renode three-act probe pastes higher-interrupts nesting, same-level suspended catch-up, IRQ-number ordering, and an out-of-bounds 16 rolling back into the highest level, the grouping ladder is recomputed group by group from the NVIC_EncodePriority source, and SysTick parked by the HAL at the lowest 0x0F leads into that wipeout of an ISR busy-waiting on the beat and locking itself up. Piece five dissects the SysTick time base: the HAL's 1ms has HAL_InitTick compute the reload value on the spot from SystemCoreClock (64000@64MHz on this board, CLKSOURCE=1 on the processor clock), and not one place in the whole HAL Src tree reads SysTick->CALIB; CALIB's TENMS is by architecture definition a 10ms reload value, yet the 9000 ST loads on F1 computes to 1ms at 9MHz, and the Renode four-act probe pastes four log segments — the boot snapshot's TENMS=640000, a write of CLKSOURCE=0 bounced back to a read of 7, and 9000 carried to 64MHz where one beat shrinks to 0.140625ms — with the hand computation closing out the real-hardware deduction of 1.125ms and the computed right answer. The PWM breathing light waits until the material matures.

## Chapter Navigation

<ChapterNav variant="sub">
  <ChapterLink href="01-tim-family-confirm-debounce">The TIM family and confirmation debouncing: from a 1Hz heartbeat to a 20ms confirmation window</ChapterLink>
  <ChapterLink href="02-config-in-type">Configuration goes into the type: designated initializers as template arguments</ChapterLink>
  <ChapterLink href="03-period-range-boundary">The edge of the range: how long a period 16 bits times 16 bits can cover</ChapterLink>
  <ChapterLink href="04-nvic-preempt-priorities">Who interrupts whom: how NVIC preemption priorities are arranged</ChapterLink>
  <ChapterLink href="05-systick-timebase">Who counts the millisecond: the SysTick time base</ChapterLink>
</ChapterNav>
