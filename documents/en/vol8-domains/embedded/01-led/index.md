---
title: 'LED Blinking: Evolution from C to C++'
description: Using lighting up the LED on PC13 as a guide, we refactor from C macro
  drivers all the way to C++23 templates and zero-overhead abstractions.
platform: stm32f1
tags:
- cpp-modern
- intermediate
- stm32f1
translation:
  source: documents/vol8-domains/embedded/01-led/index.md
  source_hash: ea0b504052416704b980771813e50a7783088482f6225fcc85d7a8490e1ebd6b
  translated_at: '2026-06-13T11:53:39.937557+00:00'
  engine: anthropic
  token_count: 296
---
# LED Blinking: The Evolution from C to C++

> One LED, a complete path of modern C++ refactoring — from HAL register operations to compile-time optimization with templates and `constexpr`.

## Motivation

- [Part 6: Starting with the First LED](01-motivation-and-overview.md) — Why we use modern C++ for STM32

## Hardware Fundamentals

- [Part 7: What Exactly is GPIO](02-what-is-gpio.md) — The history and principles of General-Purpose I/O
- [Part 8: Push-Pull, Open-Drain, and PC13](03-output-modes-and-pc13.md) — The hardware secrets behind lighting an LED

## HAL Operations

- [Part 9: HAL Clock Enable](04-hal-gpio-clock.md) — Without a clock, a peripheral is just a sleeping piece of silicon
- [Part 10: HAL_GPIO_Init](05-hal-gpio-init.md) — The ritual of telling the chip the pin configuration
- [Part 11: HAL_GPIO_WritePin and TogglePin](06-hal-gpio-output.md) — Making the pins move

## The C Macro Era

- [Part 12: LED Driver in the C Macro Era](07-c-macro-led-implementation.md) — It works, but it isn't elegant

## C++ Refactoring Evolution

- [Part 13: First Refactor — enum class](08-cpp-enum-class-revolution.md) — Replacing macros, the start of type safety
- [Part 14: Second Refactor — Templates](09-cpp-template-gpio.md) — Compile-time binding of ports and pins
- [Part 15: Third Refactor — if constexpr](10-cpp-if-constexpr-clock.md) — Making clock enable selection automatic at compile time
- [Part 16: Fourth Refactor — LED Templates](11-cpp-led-template.md) — From generic GPIO to specific abstractions
- [Part 17: Finishing with C++23 Features](12-cpp23-attributes-and-features.md) — Attributes, linkage, and the final proof of zero-overhead abstraction

## Summary

- [Part 18: Common Pitfalls and Practical Exercises](13-pitfalls-and-exercises.md) — Doing more with the LED
