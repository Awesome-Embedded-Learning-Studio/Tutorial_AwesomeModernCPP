---
title: "A World Without a Heap: Getting Memory Allocation Right First"
description: "Why memory is the first real code in a heapless kernel: first think through the ways of handing out memory and the bookkeeping intuition of bitmaps, then write the bitmap, the fixed-size block pool, and the Make/Destroy facade one article at a time, push the defenses into compile time, and finally move onto a -nostdlib target and run into three gates in a row"
chapter: 1
order: 0
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - 内存管理
  - expected
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
translation:
  source: documents/vol8-domains/embedded/zeros/01-heapless-memory/index.md
  source_hash: 33c9257c368e1670edc3ee5e7b6b6d916a251d6d1f4918f24514f3eb325c8ff5
  translated_at: '2026-11-22T16:45:44+00:00'
  engine: glm-5.3-refresh
  token_count: 975
---

# A World Without a Heap: Getting Memory Allocation Right First

> Status: rolling out progressively

## Overview

In a kernel with no heap, no exceptions, and no RTTI, where do the task control blocks and the messages live? That is the first real question the smoke test left behind. At this station we first think things through: what ways there are to hand out memory, why the fixed-size block pool is the pick, and what makes a bitmap a good bookkeeping scheme. Then we get hands-on, one article at a time — the bitmap, the pool, the typed facade — with each article bringing one new mechanism and one test gate, and the defenses written into compile time as negative compilation tests. Finally we haul the pool onto a `-nostdlib` target and run into three gates in a row. The closing article leaves behind a question — "even adding `constinit` won't compile" — and we settle it on the spot, right there, with a pair of braces.

## Articles at This Station

<ChapterNav variant="sub">
  <ChapterLink href="01-why-pool-and-bitmap">A World Without a Heap: How Memory Gets Handed Out, and What a Bitmap Is</ChapterLink>
  <ChapterLink href="02-write-the-bitmap">Writing the Bitmap: A Fixed-Capacity Bitmap</ChapterLink>
  <ChapterLink href="03-block-pool">Fixed-Size Block Pool: An Allocator with One Bit per Block</ChapterLink>
  <ChapterLink href="04-make-destroy-and-guards">Make/Destroy: Typed Birth and Death, and the Compile-Time Line of Defense</ChapterLink>
</ChapterNav>

The next article hits the -nostdlib wall: memset goes missing, nobody picks up the bill for global constructors, and `__cxa_guard_*` has nowhere to call home — three gates, taken one at a time. Rolling out progressively.
