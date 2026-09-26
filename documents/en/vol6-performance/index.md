---
title: "Volume 6: Performance Optimization"
description: "From measurement methodology to CPU microarchitecture, from tuning by bottleneck site to the performance cost of C++ abstractions"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
translation:
  source: documents/vol6-performance/index.md
  source_hash: e38c35474b5a4c7d6a11391e72f553f2834cb602b85182564328208401ba71c2
  translated_at: '2026-09-26T07:15:12+00:00'
  engine: anthropic
  token_count: 400
---

# Volume 6: Performance Optimization

Performance is the one area of C++ engineering where it is easiest to be confidently wrong — the complexity of microarchitecture runs far ahead of human intuition. The spine of this volume is a single chain: **correctness first (the correctness foundation) → measure first (the benchmark methodology anchor) → attribute and optimize by bottleneck site (the four TMA buckets) → land on the performance cost of C++ abstractions**. Every topic walks the same loop: cut in with C++ code → drop down to the hardware or the methodology → come back to how to change the C++.

One thesis runs through the whole volume: **efficiency (algorithmic complexity) ≠ performance (real behavior on hardware).** Don't stare at big-O alone — watch how the data actually flows through the hardware.

> This volume has settled into a stable eight-chapter structure (2026-07-03: the legacy standalone articles 02-inline / avx were deleted, 06-evaluating was moved out as embedded material, and the three sanitizer articles were relocated into ch00 as 03/04/05).

## Chapter navigation

<ChapterNav variant="sub">
  <ChapterLink href="ch00-performance-mindset">ch00 · Performance mindset and correctness first</ChapterLink>
  <ChapterLink href="ch01-benchmark-methodology">ch01 · Benchmark methodology (the volume's anchor)</ChapterLink>
  <ChapterLink href="ch02-cpu-microarchitecture">ch02 · CPU microarchitecture and the memory hierarchy</ChapterLink>
  <ChapterLink href="ch03-attribution-methodology">ch03 · Attribution methodology: from measurement to bottleneck</ChapterLink>
  <ChapterLink href="ch04-tuning-by-bottleneck">ch04 · Tuning by bottleneck site (the technical core)</ChapterLink>
  <ChapterLink href="ch05-multicore-performance">ch05 · Multicore performance (picking up from vol5)</ChapterLink>
  <ChapterLink href="ch06-cpp-abstraction-cost">ch06 · The performance cost of C++ abstractions</ChapterLink>
  <ChapterLink href="ch07-compiler-and-size">ch07 · Compiler optimization boundaries and binary size</ChapterLink>
</ChapterNav>
