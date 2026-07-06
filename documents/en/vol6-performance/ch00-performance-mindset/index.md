---
title: "Performance mindset and correctness first"
description: "Establishing the mindset for performance work: the difference between efficiency and performance, two iron rules, the Amdahl ceiling, and sanitizers as the correctness foundation"
---

# Performance mindset and correctness first

I'd argue performance is the one area in C++ engineering where it's easiest to be confidently wrong. Microarchitecture complexity runs far ahead of human intuition — change code by feel, and nine times out of ten you're optimizing the 5% while the real bottleneck lies in the other 95%. So the first thing this volume does isn't teach you any single trick; it sets the mindset first: **correctness first, then speed; measure first, then optimize**.

This chapter does three things. With a piece of $O(\log n)$ lookup code, it pins down **why efficiency (algorithmic complexity) and performance (real behavior on hardware) are not the same thing**. It lays down the two iron rules and the Amdahl ceiling that run through the whole volume. And it settles the sanitizer toolchain as the "correctness foundation" — any performance number without correctness backing it is not to be trusted.

This chapter is the volume's thesis entry point. ch01's benchmark methodology picks up from here, swapping "I feel like" for "I measured it".

## In this chapter

<ChapterNav variant="sub">
  <ChapterLink href="01-efficiency-vs-performance">Performance mindset: efficiency and performance are not the same thing</ChapterLink>
  <ChapterLink href="02-from-correctness-to-performance">From "correct first" to "fast next": why sanitizers are the foundation of the performance volume</ChapterLink>
  <ChapterLink href="03-asan-family-and-memory-safety">The ASan family and memory safety: shadow memory and sanitizer selection</ChapterLink>
  <ChapterLink href="04-memory-safety-asan-valgrind">Valgrind vs ASan: JIT interpretation vs compile-time instrumentation</ChapterLink>
  <ChapterLink href="05-sanitizer-toolchain-and-memory-safety">The sanitizer toolchain landscape: from -fsanitize to in-kernel KASAN/KFENCE</ChapterLink>
</ChapterNav>
