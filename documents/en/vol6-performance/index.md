---
title: "Volume 6: Performance Optimization"
description: "From measurement methodology to CPU microarchitecture, from optimize-by-bottleneck-site to the performance cost of C++ abstractions"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
translation:
  source: documents/vol6-performance/index.md
  source_hash: e38c35474b5a4c7d6a11391e72f553f2834cb602b85182564328208401ba71c2
  engine: manual
  token_count: 350
---

# Volume 6: Performance Optimization

Performance is the one area in C++ engineering where it's easiest to be confidently wrong — microarchitecture complexity runs far ahead of human intuition. The spine of this volume is a single chain: **correctness first (the correctness foundation) → measure first (the benchmark methodology anchor) → attribute and optimize by bottleneck site (the four TMA buckets) → land on the performance cost of C++ abstractions**. Every topic walks the loop "cut in with C++ code → drop down to the hardware or methodology → come back to how to change the C++".

One thesis runs through the whole volume: **efficiency (algorithmic complexity) ≠ performance (real behavior on hardware).** Don't stare at big-O — watch how the data actually flows on the hardware.

> Status: the rewritten eight-chapter CN volume is now translated in full (ch00–ch07). The legacy single-file articles (02-inline / 06-evaluating / avx) have been removed; the sanitizer articles (10/11/12) have been relocated into ch00 as 03/04/05 in both CN and EN.

## Chapter navigation

<ChapterNav variant="sub">
  <ChapterLink href="ch00-performance-mindset">ch00 · Performance mindset and correctness first</ChapterLink>
  <ChapterLink href="ch01-benchmark-methodology">ch01 · Benchmark methodology (volume anchor)</ChapterLink>
  <ChapterLink href="ch02-cpu-microarchitecture">ch02 · CPU microarchitecture and the memory hierarchy</ChapterLink>
  <ChapterLink href="ch03-attribution-methodology">ch03 · Attribution methodology: from measurement to bottleneck</ChapterLink>
  <ChapterLink href="ch04-tuning-by-bottleneck">ch04 · Tuning by bottleneck site (technical core)</ChapterLink>
  <ChapterLink href="ch05-multicore-performance">ch05 · Multicore performance (continuing vol5)</ChapterLink>
  <ChapterLink href="ch06-cpp-abstraction-cost">ch06 · The performance cost of C++ abstractions</ChapterLink>
  <ChapterLink href="ch07-compiler-and-size">ch07 · Compiler optimization bounds and size evaluation</ChapterLink>
</ChapterNav>
