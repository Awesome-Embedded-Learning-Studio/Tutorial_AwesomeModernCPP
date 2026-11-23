---
title: "Who Owns the CPU: From Ready Bitmaps to Interrupt Wakeups"
description: "The Heart of the Line: A 32-level Ready Bitmap with FIFOs at Each Level Selects the Most Urgent Task, fabricate_frame Forges a Birth Certificate at the Stack Top, svc 0 Completes the First Switch, and the Race Condition and Type State Defense Are Addressed in Later Chapters; the Same Scheduler Code Runs Green 42 Assertions on the Host, and Makes Tasks A/B Print Alternately at 1000/700ms Intervals in Renode"
chapter: 3
order: 0
tags:
  - stm32f1
  - advanced
  - 嵌入式
  - 寄存器
  - concepts
difficulty: advanced
platform: stm32f1
cpp_standard: [23]
translation:
  source: documents/vol8-domains/embedded/zeros/03-first-scheduler/index.md
  source_hash: 0947a27408de2b6b67035b9740bf60ce8ef8e2a1200e49e459509e6e8704ee44
  translated_at: '2026-11-22T13:37:36+00:00'
  engine: qwen3:14b
  token_count: 1283
---
# Who Owns the CPU: From Ready Bitmaps to Interrupt Wakeups

> We publish one article at a time, and all four chapters of this section are now online.

## Overview

Time may pass, but the CPU is still dozing in WFI inside `main`. At this moment, who owns the CPU, and who decides, is exactly the question this site aims to answer. We split the largest commit in our repository (`0bf9461`) into two parts to explain. We clarify the arbitration aspect: a 32-level ready bitmap with FIFOs at each level, where the enqueue direction of `pick_next` and `yield` is intentionally asymmetric. Preempted tasks go to the front of the queue, while yielded tasks go to the end. The selection of the next task is achieved in a single cycle using RBIT and CLZ. The mechanism aspect centers around `fabricate_frame`, which forges a birth certificate at the stack top. The Thumb bit in xPSR must be correct, and `svc 0` ignites the switch, allowing the kernel to actually perform the context switch. You will witness all 42 assertions pass on the host, and see tasks A/B print alternately at 1000/700ms intervals in the Renode serial window. At that moment, ZerOS transitions from a clock library to a real RTOS.

## Articles on the Site

<ChapterNav variant="sub">
  <ChapterLink href="01-ready-bitmap-pick-next">Who is the Most Urgent, Who Takes the Stage: Ready Bitmaps, Task Three States, and Selection Arbitration</ChapterLink>
  <ChapterLink href="02-pendsv-first-switch">Forging a Birth Certificate at the Stack Top: PendSV, SVC, and the First Switch</ChapterLink>
  <ChapterLink href="03-lost-wakeup-and-typestate">That Bug That Sleeps: Lost Wakeups, park_for, and Builders Chains That Prevent Compile-Time Errors</ChapterLink>
  <ChapterLink href="04-thistask-and-irq-wakeup">Balancing Interrupt Priorities and Letting Interrupts Knock: ThisTask and the First Wakeup Path</ChapterLink>
</ChapterNav>

The scheduler can now perform context switches, the race condition of lost wakeups has been reproduced and fixed, and interrupts have successfully awakened sleeping tasks. Our ZerOS has now evolved from a clock library into a true RTOS. The next stop is to equip it with the correct waiting mechanism: counting semaphores and priority inheritance.
