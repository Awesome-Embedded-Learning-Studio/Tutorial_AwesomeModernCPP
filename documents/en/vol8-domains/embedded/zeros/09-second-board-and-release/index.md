---
title: "The Second Board and Departure: QEMU, Tickless, v0.1.0"
description: "A single board proves it can run, but four testbeds make the case: QEMU mps2-an385 kernel with zero modifications fills the third slot, stack watermark answers how much is used, tickless answers how much is saved, and finally checkout v0.1.0 for a full end-to-end verification, ready to depart."
chapter: 9
order: 0
tags:
  - stm32f1
  - intermediate
  - embedded
  - toolchain
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
translation:
  source: documents/vol8-domains/embedded/zeros/09-second-board-and-release/index.md
  source_hash: f07e8251e96e56af4e29481064c271b056629f99346854bdbdede451f65d5300
  translated_at: '2026-11-22T14:11:17+00:00'
  engine: qwen3:14b
  token_count: 1111
---
# The Second Board and Departure: QEMU, Tickless, v0.1.0

> We publish one article at a time, and all four articles in this chapter are now online. This marks the completion of our entire handcrafted journey.

## Overview

Being able to run on a single board is just the first step; passing all four testbeds is the real proof. In this chapter, we port the same RTOS to QEMU's mps2-an385, relying on the thin port we achieved by removing CMSIS earlier. Not a single line of the kernel was modified. We also split a large commit into two articles: one on stack watermark answers the question of how much stack is used, determining stack depth based on evidence rather than intuition. The tickless article answers how much is saved, by slowing down the heartbeat while sleeping and waking up precisely at the next due time. Finally, we checked out v0.1.0 and performed a full end-to-end verification, running through the entire pipeline on host, Renode, QEMU, and the actual development board, completing the entire handcrafted journey and officially departing.

## Articles on Site

<ChapterNav variant="sub">
  <ChapterLink href="01-qemu-mps2-second-board">Same heartbeat in QEMU: Second board with zero modifications, fulfilling the thin port</ChapterLink>
  <ChapterLink href="02-stack-watermark">Filling the stack with magic numbers: Upgrading canary to watermark, determining stack depth based on evidence</ChapterLink>
  <ChapterLink href="03-tickless-idle">Slowing the heartbeat while sleeping: Tickless, resolving race conditions, and 24-bit分期续杯</ChapterLink>
  <ChapterLink href="04-release-v010">Listing v0.1.0: README is the gate, walking through all four testbeds one by one</ChapterLink>
</ChapterNav>

We've journeyed from the super loop all the way to v0.1.0, completing the entire handcrafted journey. Every line of code you reproduce is from the same tree as the reference solution. As you go deeper, the kernel will be in your own hands.
