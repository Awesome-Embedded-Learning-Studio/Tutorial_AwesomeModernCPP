---
title: "Give the Kernel a Second: Time and Critical Sections"
description: "Ticks value type uses signed differences to compare uint32 wrap-around times, SelfList intrusive linked list queues expired tasks, BASEPRI nested critical sections run the same code on host mock and Blue Pill; wrap up by removing CMSIS direct writes to SysTick three registers, and confront core_cm3.h in court to expose the priority shift trap: comments say Lowest, formula calculates 0x00, the real highest priority is actually the default value from SysTick_Config"
chapter: 2
order: 0
tags:
  - stm32f1
  - intermediate
  - embedded
  - registers
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
translation:
  source: documents/vol8-domains/embedded/zeros/02-time-kernel/index.md
  source_hash: a48c11a79a2be054c1d3f6672bc23d658a3431d19c7acafbd8ad025c1e3168b5
  translated_at: '2026-11-22T13:28:18+00:00'
  engine: qwen3:14b
  token_count: 1280
---
# Give the Kernel a Second: Time and Critical Sections

> We publish one article at a time, and today we've already published two.

## Overview

Once memory is allocated, the kernel's first real responsibility is time. We need to answer three questions: how many counts does a second consist of on the chip, how to compare early and late times when the counter wraps around, and in what order expired tasks are awakened. At the end of the previous article, we used a pair of square brackets to resolve the suspense around `constinit`. In this article, we install a time subsystem for the kernel. `Ticks` wraps time into a strong type, and with signed difference comparisons, our "early and late" judgment still gives the same answer even when the counter wraps around. `SelfList` is an intrusive linked list, and we use it to queue expired tasks based on their deadlines. We also ran the same `Kernel<Driver>` template on both the desktop mock and Blue Pill, fulfilling the promise of "one code for four targets" for the first time. At the end, we removed CMSIS from the arch layer, directly wrote to the three SysTick registers, and conveniently confronted the built-in `core_cm3.h` in the repository. The result of the confrontation is worth sitting through: the comment says "Lowest," but the shifted formula ends up in the register as 0x00, the real highest priority. Runtime has never had any issues, and we traced the cause through the source code: it's the default value from `SysTick_Config` that overwrites the incorrect value. We break down this cause and effect for readers in the main text. Time will pass, and only then will the scheduler take the stage.

## Articles on This Site

<ChapterNav variant="sub">
  <ChapterLink href="01-ticks-and-timer-queue">The Right to Define a Second: Tick, Wrap-Around Arithmetic, and Expired Queue</ChapterLink>
  <ChapterLink href="02-systick-priority-truth">Comments Say Lowest, Silicon Says Highest: Removing CMSIS's Priority Shift</ChapterLink>
</ChapterNav>

Time has passed, the CPU is still sleeping in `main`'s WFI, and our next stop will be the scheduler. The ready bitmap selects the most urgent task, we forge a birth certificate at the top of the stack, and then perform the first real context switch. If you've followed us here, we'll see you in the next article.
