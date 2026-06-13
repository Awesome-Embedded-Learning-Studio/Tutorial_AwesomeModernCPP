---
title: Embedded Development
description: Practical application of modern C++ in embedded systems
platform: stm32f1
tags:
- cpp-modern
- intermediate
- stm32f1
translation:
  source: documents/vol8-domains/embedded/index.md
  source_hash: fdc087cb31a2b391c8496105cc738efeabcf166d5d50db41709627f7ed1d988a
  translated_at: '2026-06-13T11:54:01.076003+00:00'
  engine: anthropic
  token_count: 442
---
# Embedded Development

> What modern C++ can and cannot do in resource-constrained embedded systems—from zero-overhead abstractions and memory management to peripheral programming, interrupt concurrency, and finally STM32 practice and RTOS.

## STM32F1 Hands-on Series

This is a complete roadmap for writing STM32 code in modern C++ from scratch. It follows the sequence "Environment → LED → Button → UART", refactoring each peripheral from C all the way to C++23:

- [Development Environment Setup](00-env-setup/index.md) — Toolchain, project structure, CMake, WSL2 USB passthrough, GDB debugging.
- [LED Blinking: Evolution from C to C++](01-led/index.md) — From HAL registers to templates and `constexpr`.
- [Button Input: Debouncing, State Machines, and Type Safety](02-button/index.md) — From polling to interrupts and state machines.
- [UART Serial Communication](03-uart/index.md) — From protocols to interrupt-driven, `std::expected` error handling.

## Embedded Special Topics

These are special topic articles migrated from an older tutorial, covering zero-overhead abstractions, memory management, register access, and interrupt safety. They serve as supplementary reading for the hands-on series:

<ChapterNav variant="sub">
  <ChapterLink href="01-zero-overhead-abstraction">Modern C++ for Embedded—Zero-Overhead Abstraction</ChapterLink>
  <ChapterLink href="01-resource-and-realtime-constraints">Resource and Real-Time Constraints in Embedded Systems</ChapterLink>
  <ChapterLink href="01-dynamic-allocation-issues">The Cost of Dynamic Memory: Fragmentation and Uncertainty</ChapterLink>
  <ChapterLink href="02-static-and-stack-allocation">Embedded C++ Tutorial—Static Storage and Stack Allocation Strategies</ChapterLink>
  <ChapterLink href="03-object-pool-pattern">Embedded C++ Tutorial: Object Pool Pattern</ChapterLink>
  <ChapterLink href="04-crtp-vs-runtime-polymorphism">Compile-Time Polymorphism vs Runtime Polymorphism</ChapterLink>
  <ChapterLink href="04-placement-new">Embedded C++ Tutorial: Placement New</ChapterLink>
  <ChapterLink href="05-fixed-pool-allocation">Embedded C++ Tutorial: Slab / Arena Implementation and Comparison</ChapterLink>
  <ChapterLink href="05-etl">Embedded C++ Tutorial—ETL</ChapterLink>
  <ChapterLink href="05-interrupt-safe-coding">Writing Interrupt-Safe Code</ChapterLink>
  <ChapterLink href="03-circular-buffer">Circular Buffer</ChapterLink>
  <ChapterLink href="04-intrusive-containers">Intrusive Container Design</ChapterLink>
  <ChapterLink href="02-type-safe-register-access">Type-Safe Register Access</ChapterLink>
  <ChapterLink href="core-embedded-cpp-index">Table of Contents</ChapterLink>
</ChapterNav>
