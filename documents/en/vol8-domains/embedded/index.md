---
title: "Embedded Development"
description: "Modern C++ embedded track: Renode simulator first, from the STM32F103 up to the STM32F407, no microcontroller required"
---

# Embedded Development

This track answers one question: how should modern C++ actually be used on a microcontroller, and how good can it get? C++ is the protagonist; the chip is a prop. The line starts on the cheapest entry point, the STM32F103, and works its way up to the roomier STM32F407 — the same C++ playbook, driven to the end on both chips.

The other difference from most embedded tutorials: **the Renode simulator comes first**. With no microcontroller at hand you can still run and verify every piece of code. On-board verification closes each station at the end: nice to follow along with a board, never a blocker without one.

## Two acts

- **[Act I: STM32F103 + Renode](f103/)** — starting from lighting a single LED, each peripheral begins in C and climbs one rung at a time up to C++23, with disassembly checked at every rung to prove the abstraction costs nothing.
- **Act II: STM32F407** — what comes after graduating from Act I: DMA, Ethernet, an RTOS, and a full capstone project that pulls together everything the line has built.

::: tip Under reconstruction
This track is being rebuilt along a new structure; content will go live progressively, starting with the getting-started station of Act I. The previous STM32F103 tutorial (the classic HAL-first edition) has been archived, and its knowledge core will be folded into the matching stations of the new line.
:::
