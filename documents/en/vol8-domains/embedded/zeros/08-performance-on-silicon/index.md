---
title: "True Silicon Stopwatch: How Much Does a Context Switch Cost?"
description: "Putting it all on real silicon: the clock tree climbs to 72MHz, the DWT cycle counter acts as a stopwatch, measuring the first cut of a switch; ramfunc moves to RAM, but is debunked by a four-cell experimental matrix and rolled back to a macro switch, total account from 489 to 333 cycles; removing the heartbeat's variability test uncovers two latent bugs; the methodology of measurement itself becomes a deliverable — probes are patched with the library, and if you forget to apply, it loudly reports an error."
chapter: 8
order: 0
tags:
  - stm32f1
  - advanced
  - 嵌入式
  - 优化
difficulty: advanced
platform: stm32f1
cpp_standard: [23]
translation:
  source: documents/vol8-domains/embedded/zeros/08-performance-on-silicon/index.md
  source_hash: 936a2d9d5365cf79243813a9da1b5582e31e1357698de558908ea5007fb42521
  translated_at: '2026-11-22T14:05:55+00:00'
  engine: qwen3:14b
  token_count: 1365
---
# True Silicon Stopwatch: How Much Does a Context Switch Cost?

> We publish one article at a time, and all three articles in this chapter are now online.

::: tip Requires a real development board
The cycle counts in this chapter have only been verified on a real development board (Blue Pill + ST-Link/OpenOCD). The author's practical test records are in the experimental notebook in the documents folder of the repository. Readers without a board can still read, with each number marked with its source and methodology, but you can't reproduce it for now. Simulators can't provide cycle counts, and we've also verified this in the main text.
:::

## Overview

With the several checks from CI, we are now qualified to answer honestly the question that has been hanging since the beginning: how expensive is a context switch? In this chapter, we've fully applied real silicon. In the first article, we manually climbed the clock tree (from HSE 8MHz to PLL×9), and on the spot, we confronted the fake RCC in the simulator. We also installed the DWT cycle counter as a stopwatch, measuring the report of the first cut of a switch. The second article discusses a reversal: the statement that "RAM is fast" was debunked by a four-cell experimental matrix, and .ramfunc rolled back to a macro switch. The total count for the entire chapter went from 489 cycles to 333, and we also learned how to write an honest experimental notebook. In the third article, we removed the heartbeat for variability testing, uncovering a long-latent bug: after a task returns, the Trap occupies the CPU on-site, and the unsigned wrap-around of the deadline turns an approximate forever into an immediate wake-up. After the three articles, the methodology of measurement itself has also become part of the deliverable: probes are patched with the library, and if you forget to apply, it will loudly report an error.

## Articles on the Site

<ChapterNav variant="sub">
  <ChapterLink href="01-clock-tree-dwt-first-cut">Welding the Stopwatch into Silicon: Clock Tree Climbs to 72MHz, Measuring the First Cut of a Switch</ChapterLink>
  <ChapterLink href="02-ramfunc-rollback-experiment">The Textbook Loses to the Prefetch Buffer: .ramfunc Reversal and an Honest Experimental Notebook</ChapterLink>
  <ChapterLink href="03-notick-variants-and-probe-patch">Removing the Heartbeat for Experimentation: NO_TICK, Zombie Tasks, and Probes Patched with the Library</ChapterLink>
</ChapterNav>

The cost of cycle counting is done, and now we raise our sights one level higher: a single board being able to run is just the first step; running through all four exam rooms is the real claim. Our next stop is to move the same RTOS onto QEMU's second board, setting the stack depth according to evidence, and letting the sleeping heartbeat slow down on its own. Finally, we will hang the v0.1.0 sign.
