---
title: "Embedded Development"
description: "The modern C++ embedded mainline: the Renode simulator goes first, from the STM32F103 up to the STM32F407 — follow from start to finish without buying a board"
translation:
  source: documents/vol8-domains/embedded/index.md
  source_hash: 185b0cd944fb2e892650eaac3a4c308d1074f686bce686a48f686a2142f0b41b
  translated_at: '2026-09-26T04:27:56+00:00'
  engine: anthropic
  token_count: 650
---

# Embedded Development

TAMCPP set out to do embedded C++ development in the first place, and here we finally are. Now take a deep breath — our journey is about to begin!

This track answers one question: once modern C++ lands on a microcontroller, how exactly should it be used, and how good can it get? C++ is the protagonist; the chip is a prop. So we start from the lowest-barrier STM32F103 and work our way up to the resource-roomy STM32F407, driving the same C++ playbook to its end on both chips. Once the F103 is done, one deeper line follows: ZerOS — the RTOS the F103 track consumes as a dependency — implemented by hand in commit order, from an empty repository all the way to v0.1.0.

The other difference from most embedded tutorials: **the Renode simulator goes first**. With no microcontroller at hand, you can still run every piece of code and verify it is right. Verification on a real board comes at the very end of each station — follow along if you have a board, and never be held up if you don't.

## Chapter Navigation

<ChapterNav variant="sub">
  <ChapterLink href="f103/">STM32F103 + Renode</ChapterLink>
  <ChapterLink href="zeros/">Hand-Rolling ZerOS: From Bare Metal to an RTOS, One Commit at a Time</ChapterLink>
  <ChapterLink href="f407/">STM32F407 Advanced — planned</ChapterLink>
</ChapterNav>

::: tip Under reconstruction
The content is being progressively refurbished along the new structure — start following from the getting-started article and the rest will come online in due course. The old STM32F103 tutorial (which started from the HAL library) has been archived; its knowledge core will be folded into the corresponding stations of the new tutorial, and the getting-started article has already landed anew on top of the companion peripheral library libestdx.
:::
