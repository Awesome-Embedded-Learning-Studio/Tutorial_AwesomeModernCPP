---
title: "Why an RTOS: From the Super Loop to ZerOS"
description: "The three structural problems of a foreground/background system, a concept map for the whole series, why this kernel keeps C++23, and the division of labor among four verification environments"
chapter: 0
order: 0
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - 入门
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
translation:
  source: documents/vol8-domains/embedded/zeros/00-why-rtos/index.md
  source_hash: d19cfd351f026ed77316f4c398a0ccefa1270986060918757e0b83979c333a6d
  translated_at: '2026-09-25T07:59:53+00:00'
  engine: anthropic
  token_count: 300
---

# Why an RTOS: From the Super Loop to ZerOS

> Status: rolling out progressively

## Overview

If you are arriving from the F103 series, you can already write bare-metal code and drive Renode — but you have never touched a kernel. This station first lays out the three structural problems of a foreground/background system (no guaranteed response time, no task priorities, synchronization managed entirely by hand), then presents a concept map for the whole hand-rolled series, with every concept tagged by the ZerOS commit where it will land; after that it explains why this kernel keeps C++23 on bare metal, and what each of the four verification environments — host, Renode, QEMU, and real hardware — is responsible for.

## Articles in This Station

<ChapterNav variant="sub">
  <ChapterLink href="01-rtos-concept-map">From the Super Loop to an RTOS: why you need one, how to verify it</ChapterLink>
  <ChapterLink href="02-bringup-and-linker-bans">Project Bring-up: from an empty repository to the first line of Renode output</ChapterLink>
</ChapterNav>

The first station, *The World Without a Heap*, is already live: head into [01-heapless-memory/](../01-heapless-memory/) and let's hand-roll the memory pool.
