---
title: "Embedded Development"
description: "Modern C++ embedded tutorial: Renode simulator first, from the STM32F103 up to the STM32F407, no microcontroller required"
---

# Embedded Development

This tutorial answers one question: how should modern C++ actually be used on a microcontroller, and how good can it get? C++ is the protagonist; the chip is a prop. It starts on the cheapest entry point, the STM32F103, and works its way up to the roomier STM32F407 — the same C++ playbook, driven to the end on both chips.

The other difference from most embedded tutorials: **the Renode simulator comes first**. With no microcontroller at hand you can still run and verify every piece of code. Verification on an actual board closes each station at the end: nice to follow along with a board, never a blocker without one.

## Chapter Navigation

<ChapterNav variant="sub">
  <ChapterLink href="f103/">STM32F103 + Renode</ChapterLink>
  <ChapterLink href="f407/">STM32F407 Advanced — Planned</ChapterLink>
</ChapterNav>

::: tip Under reconstruction
This series is being rebuilt along a new structure; content will go live progressively, starting with the getting-started station of the F103 tutorial — now restated on top of the companion peripheral library libestdx. The previous STM32F103 tutorial (the classic HAL-first edition) has been archived, and its knowledge core will be folded into the matching stations of the new one.
:::
