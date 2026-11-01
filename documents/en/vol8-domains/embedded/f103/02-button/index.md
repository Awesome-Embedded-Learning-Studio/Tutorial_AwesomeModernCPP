---
title: "Buttons: debouncing, state machines, variant"
description: "Read GPIO input and debounce it in software, manage the button lifecycle with a state machine, and express button events as variant — the type system watching every branch"
chapter: 2
order: 0
tags:
  - stm32f1
  - intermediate
  - 状态机
  - variant
difficulty: intermediate
platform: stm32f1
translation:
  source: documents/vol8-domains/embedded/f103/02-button/index.md
  source_hash: dbee1322ceacc0499593e3a0938ef6a9531674b505aab36ec316dddade08c2e1
  translated_at: '2026-11-01T03:18:31+00:00'
  engine: anthropic
  token_count: 150
---

# Buttons: debouncing, state machines, variant

## Overview

The first article on button input has landed at this stop: the same PA0 pin is wired into two chains — a polling debounce chain reports stable-state events through Debouncer and ButtonFsm, an EXTI edge chain grabs a timestamp inside the interrupt, and Renode injects bounce for the head-to-head comparison, with the trade-offs between the two chains laid out in the open. More articles will follow once the material matures.

## Chapter Navigation

<ChapterNav variant="sub">
  <ChapterLink href="01-polling-debounce-vs-async-exti">One burst of bounce, two chains: polling debounce vs. the EXTI edge chain</ChapterLink>
</ChapterNav>
