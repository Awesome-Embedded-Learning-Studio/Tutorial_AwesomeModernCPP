---
title: 'Button Input: Debouncing, State Machines, and Type Safety'
description: From GPIO input circuits to a seven-state debounce state machine, then
  refactoring the button code into a type-safe form using variant and concepts
platform: stm32f1
tags:
- cpp-modern
- intermediate
- stm32f1
translation:
  source: documents/vol8-domains/embedded/02-button/index.md
  source_hash: 2ba3433f22f954e1152266fc36c46816b901593f4282398ff10af67dc8387e5e
  translated_at: '2026-06-13T11:53:47.143962+00:00'
  engine: anthropic
  token_count: 269
---
# Button Input: Debouncing, State Machines, and Type Safety

> Buttons are harder than LEDs—hardware bounce, non-blocking debouncing, state machines, plus type-safe refactoring in C++.

## Motivation

- [Part 19: From Output to Input](01-from-output-to-input.md) — Why buttons are harder than LEDs

## Hardware Fundamentals

- [Part 20: GPIO Input Mode Internal Circuits](02-gpio-input-circuits.md) — How the chip "hears" external signals
- [Part 21: Button Circuits and Mechanical Bounce](03-button-hardware-and-bounce.md) — What real-world signals look like

## HAL and Polling

- [Part 22: HAL GPIO Input API](04-hal-gpio-input.md) — How to read button states in code
- [Part 23: Polling Buttons in C](05-c-polling-button.md) — First hands-on: controlling an LED with a button

## Debouncing

- [Part 24: Non-blocking Debounce](06-non-blocking-debounce.md) — Don't make the CPU wait
- [Part 25: 7-State Debounce State Machine](07-debounce-state-machine.md) — The core of this series

## C++ Refactoring Evolution

- [Part 26: Refactoring Button Code with `enum class`](08-cpp-enum-class-button.md) — Type-safe input
- [Part 27: Events + `std::variant` Dispatch](09-cpp-variant-and-visit.md) — Type-safe "what happened"
- [Part 28: Button Template Class Design](10-cpp-template-button.md) — Let the compiler do the work
- [Part 29: Constraining Callbacks with Concepts](11-cpp-concepts-callback.md) — Complete code walkthrough

## Interrupts and Summary

- [Part 30: EXTI Interrupts](12-exti-interrupt-and-exercises.md) — Pitfalls and exercises
