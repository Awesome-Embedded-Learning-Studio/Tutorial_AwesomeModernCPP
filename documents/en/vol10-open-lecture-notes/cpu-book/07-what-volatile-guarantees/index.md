---
title: "What volatile Actually Governs: It Holds Back the Compiler, Not Data Races"
description: "volatile may be the most misunderstood keyword in C++. This topic covers only what it actually governs: restraining the compiler from merging, deleting, or reordering memory accesses. Data races are none of its business, and disassembly draws the boundary."
sidebar_order: 7
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/07-what-volatile-guarantees/index.md
  source_hash: 35c4bac8547f270aa24189e57b896094ea5d79f779edd63408d4739308072ac4
  translated_at: '2026-10-14'
  engine: manual
---

# What volatile Actually Governs: It Holds Back the Compiler, Not Data Races

volatile may be the most misunderstood keyword in C++. This topic covers only what it actually governs: restraining the compiler from merging, deleting, or reordering memory accesses. Data races are none of its business, and disassembly draws the boundary.

<ChapterNav variant="sub">
  <ChapterLink href="01-volatile-bans-in-one-thread">The Three Things volatile Bans in a Single Thread: Merging, Elimination, and Reordering</ChapterLink>
  <ChapterLink href="02-volatile-race-and-lock-prefix">volatile Doesn't Stop Data Races: Lost Updates Up Close and the Line Drawn by the lock Prefix</ChapterLink>
  <ChapterLink href="03-standard-deprecation-mmio-and-verdict">volatile's Standard Boundary: The Deprecation List, Device Registers, and a Decision Rule</ChapterLink>
</ChapterNav>
