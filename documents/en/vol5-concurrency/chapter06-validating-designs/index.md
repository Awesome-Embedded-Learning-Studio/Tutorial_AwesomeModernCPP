---
title: "Unit 6: How to Tell Whether a Concurrent Design Works"
description: "From being able to write it in the first five units, to proving it is written right: finding concurrency bugs, measuring concurrency's payoff, and returning from measurement to design"
translation:
  source: documents/vol5-concurrency/chapter06-validating-designs/index.md
  source_hash: 9af263a34322af12cd67aac691d97cec2a26f3b5ae39c0a88020480950b5a0ae
  translated_at: '2026-11-22T13:53:00+00:00'
  engine: glm-5.3
  token_count: 663
---

# Unit 6: How to Tell Whether a Concurrent Design Works

The first five units put threads, sharing, waiting, tasks, and atomic operations in place, and you already have the tools well in hand. This unit takes a step back and asks the acceptance question: once a concurrent design is written, how do you prove it can find its bugs, measure its payoff, and move its design? The unit's content is split into one article per section; the five main articles are complete.

## What This Unit Covers

<ChapterNav variant="sub">
  <ChapterLink href="01-finding-concurrency-bugs">Catching the Races: Repeated Testing and TSan</ChapterLink>
  <ChapterLink href="02-stop-path-and-diagnostics">Can It Stop When Asked: Stop-Path Testing and Three Diagnostic Tools</ChapterLink>
  <ChapterLink href="03-measuring-benefits">Before Measuring, Pin the Conditions Down: The Serial Baseline and the Traps of Measurement</ChapterLink>
  <ChapterLink href="04-benchmark-and-variables">Benchmark Tools and Three Variables: Benchmark, Data Size, Granularity, Thread Count</ChapterLink>
  <ChapterLink href="05-adjusting-design">How to Adjust the Design</ChapterLink>
</ChapterNav>
