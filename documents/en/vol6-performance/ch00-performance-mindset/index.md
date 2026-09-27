---
title: "Performance mindset and correctness first"
description: "Building the right mindset for performance work: the difference between efficiency and performance, two iron rules, the Amdahl ceiling, and sanitizers as the correctness foundation"
translation:
  source: documents/vol6-performance/ch00-performance-mindset/index.md
  source_hash: af2b7f566ded70c8ce8df0208d1386483065cf34481de03d6af03f4940908455
  translated_at: '2026-09-26T05:36:20+00:00'
  engine: anthropic
  token_count: 450
---

# Performance mindset and correctness first

If you ask us, performance is the area of C++ engineering where it is easiest to be confidently wrong. Microarchitecture complexity runs far ahead of human intuition: change code by feel, and nine times out of ten you are optimizing the 5% while the real bottleneck lies dormant in the other 95%. So the first thing this volume does is not teach you any single optimization trick — it sets the mindset first: **correct first, then fast; measure first, then optimize**.

This chapter does three things. With a piece of lookup code where both variants are $O(\log n)$, we spell out **why efficiency (algorithmic complexity) and performance (real behavior on hardware) are not the same thing**; we lay down the two iron rules and the Amdahl ceiling that run through the whole volume; and we settle the sanitizer toolchain in place as the "correctness foundation" — a performance number without correctness backing it is not to be trusted, period.

This chapter is the volume's thesis entry point. ch01's benchmark methodology picks up from here, swapping "I feel like" for "I measured it".

## In this chapter

<ChapterNav variant="sub">
  <ChapterLink href="01-efficiency-vs-performance">Performance Mindset: efficiency is not performance</ChapterLink>
  <ChapterLink href="02-from-correctness-to-performance">From "correct first" to "then fast": why sanitizers are the foundation of the performance volume</ChapterLink>
  <ChapterLink href="03-asan-family-and-memory-safety">The ASan tool family and memory safety: shadow memory and sanitizer selection</ChapterLink>
  <ChapterLink href="04-memory-safety-asan-valgrind">Valgrind vs ASan: JIT interpretation vs compile-time instrumentation</ChapterLink>
  <ChapterLink href="05-sanitizer-toolchain-and-memory-safety">The sanitizer toolchain landscape: from -fsanitize to in-kernel KASAN/KFENCE</ChapterLink>
</ChapterNav>
