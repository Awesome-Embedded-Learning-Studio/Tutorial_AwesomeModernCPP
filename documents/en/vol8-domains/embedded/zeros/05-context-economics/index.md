---
title: "Whose Context: Last Words, Bottom Halves, and Timers"
description: "The economics of kernel services is summarized by a single principle: in which context the code runs. HardFault's last words report and stack canary manage the fault context, the ring buffer processes the author thread for ISR unloading (interrupts only pass notes), software timers manage waiting without tasks; the host exam and Renode injection experiments are verified station by station."
chapter: 5
order: 0
tags:
  - stm32f1
  - intermediate
  - embedded
  - callback mechanism
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
translation:
  source: documents/vol8-domains/embedded/zeros/05-context-economics/index.md
  source_hash: a498eb5214f4305db0e0a415f4b7c60fc2a963d44f06f4816a7cc69274ca281c
  translated_at: '2026-11-22T13:46:49+00:00'
  engine: qwen3:14b
  token_count: 954
---
# Whose Context: Last Words, Bottom Halves, and Timers

> We publish one article after finishing one, and currently two have been published.

## Overview

Once the kernel is running, when we continue to write applications on it, there are three unavoidable costs: when the chip crashes, we need to see the cause of death; the interrupts received must be passed on to the workers; waiting for 300 milliseconds should not cost the entire task control block. On this site, we follow the principle of "in which context the code runs" to match the three components. Last words manage the fault context: we use a few lines of naked assembly to determine the MSP and PSP, CFSR, HFSR, BFAR are all decoded into reports, the stack canary is hung on the PendSV path, and the patrol only costs one comparison. Bottom halves manage the ISR unloading: a fixed-capacity ring buffer is paired with a counting semaphore, the worker thread is born with zero heap allocation, and the interrupt only passes notes while never sleeping itself. Timers manage waiting without tasks: when waiting for 300 milliseconds, you don't need to maintain a dedicated task. The three articles are independent of each other but support each other, and together they form the economics of kernel services.

## Articles on the Site

<ChapterNav variant="sub">
  <ChapterLink href="01-fault-canary-and-log">The Chip's Last Will: HardFault Scene, Stack Canary, and Zero-Allocated Logging</ChapterLink>
  <ChapterLink href="02-msg-queue-and-worker">Interrupts Only Pass Notes: Ring Buffer, Worker Thread, and Bottom Half Delay</ChapterLink>
</ChapterNav>

Software timers and the hard currency of fn plus void* are still waiting for you in the following articles. We publish one article after finishing one.
