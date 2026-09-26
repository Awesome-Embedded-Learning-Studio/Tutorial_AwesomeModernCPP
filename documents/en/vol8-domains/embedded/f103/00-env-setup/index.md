---
title: "Getting Started"
description: "Before installing anything, first answer one question with four firmwares that differ only in how they blink an LED: what right does C++ have to a microcontroller — every number reproducible; then from Renode observation, the working environment, and a first firmware through debugging, real hardware, and clangd, the seven environment pieces all fall into place"
chapter: 0
order: 0
tags:
  - stm32f1
  - beginner
  - 入门
  - 工具链
difficulty: beginner
platform: stm32f1
translation:
  source: documents/vol8-domains/embedded/f103/00-env-setup/index.md
  source_hash: 85fd815512ede1547bb743999585a48d57916aa9699307513fc8b6a30f9c04bd
  translated_at: '2026-09-26T04:19:22+00:00'
  engine: anthropic
  token_count: 1000
---

# Getting Started

Before you install a single thing, let's answer one question first: what right does C++ have to be on a microcontroller? The opening piece takes the two old impressions — "C++ is bloated" and "C++ is just OOP" — and puts them on the scale with four firmwares that differ only in how they blink an LED: binary size, instructions, and when errors surface, all three dimensions are measured numbers, and you can reproduce every one of them. The second piece then answers "where did these bad impressions come from": from C with Classes in 1979 to the 1990s Embedded C++ feature-cutting affair, every historical claim carries a source.

On the environment side, the road starts at the Renode observatory, runs through the anatomy of the toolchain, a first firmware of your own, debugging, and flashing on real hardware, and ends with clangd making sense of this cross-compiled codebase.

## The Opening

<ChapterNav variant="sub">
  <ChapterLink href="00-why-cpp">Why C++, and by what right?</ChapterLink>
  <ChapterLink href="01-cpp-history">A short history of C++: where the bad reputation came from</ChapterLink>
  <ChapterLink href="02-renode-observatory">Renode observatory: no board, so who gets the final say?</ChapterLink>
  <ChapterLink href="03-toolchain-anatomy">Working environment: the four things you installed — what exactly are they?</ChapterLink>
  <ChapterLink href="04-first-firmware">Your very own firmware: adding a target to the repo</ChapterLink>
  <ChapterLink href="05-debugging">Debugging: from sampling through the glass to stopping to inspect the scene</ChapterLink>
  <ChapterLink href="06-flashing">Real hardware: flashing your first real board, getting UART up and running</ChapterLink>
  <ChapterLink href="07-clangd">clangd: teaching the editor to understand cross-compiled code</ChapterLink>
</ChapterNav>
