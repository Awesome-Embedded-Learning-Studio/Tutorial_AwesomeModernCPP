---
title: "Correct Waiting: Counting Semaphores and Priority Inheritance"
description: "The eight operation seams of SystemContext allow the same semaphore to run once on the host mock and once on the actual development board, ensuring that events are not lost even if they arrive before the task goes to sleep; mutexes add ownership on top of the count, arranging the waiting chain by priority, and unlocking transfers ownership directly to the waiting task at the head of the queue without returning to competition; the 300 milliseconds of silence from task M in the Renode serial port theater is the cost of inheritance in action."
chapter: 4
order: 0
tags:
  - stm32f1
  - advanced
  - embedded
  - mutex
difficulty: advanced
platform: stm32f1
cpp_standard: [23]
translation:
  source: documents/vol8-domains/embedded/zeros/04-sync-and-inheritance/index.md
  source_hash: d4c8ef5cc350c0f66b6d2a1f01f77410da9e02c639b40fd35e345e729b400283
  translated_at: '2026-11-22T13:41:03+00:00'
  engine: qwen3:14b
  token_count: 1074
---
# Correct Waiting: Counting Semaphores and Priority Inheritance

> We publish one article at a time, and both articles in this chapter are now online.

## Overview

Interrupts can wake up tasks, but when events arrive before the task goes to sleep, a single byte mailbox can't hold them all, and the old problem of losing events returns. On this site, we equip the kernel with correct waiting mechanisms, and we do it in two steps. The first step is to implement counting semaphores, which record the occurrence of events as a non-lossy count. What allows the same synchronization code to run once on the desktop and once on the chip is the eight operation seams of `SystemContext`, a list agreed upon between the application and the kernel. On the host, we hand it over to a stand-in, and on the board, it is handed over to CortexM3System. The second step is to implement mutexes, which add ownership on top of the count: the waiting chain is arranged by priority, unlocking transfers ownership directly to the waiting task at the head of the queue without returning to competition, and single-level priority inheritance temporarily raises the level of the lower-priority task. The conclusion is the sudden silence of task M in the Renode serial port theater for 300 milliseconds, where you can witness priority inversion firsthand, and then see how inheritance cures it.

## Articles on the Site

<ChapterNav variant="sub">
  <ChapterLink href="01-semaphore-and-system-context">Events Arrive Early, But They Must Not Be Lost: Counting Semaphores and the Eight-Operation SystemContext Seam</ChapterLink>
  <ChapterLink href="02-mutex-priority-inheritance">Watch Task M Fall Silent for Three Seconds: Mutexes, Priority Inheritance, and Ownership Transfer</ChapterLink>
</ChapterNav>

Both legs of synchronization are now in place, but the kernel still lacks some services: what if the chip dies, who takes over the heavy work after the interrupt is removed, and should a dedicated task be created to wait for 300 milliseconds. Our next stop is to follow the outline of "which context the code runs on" and complete them one by one.
