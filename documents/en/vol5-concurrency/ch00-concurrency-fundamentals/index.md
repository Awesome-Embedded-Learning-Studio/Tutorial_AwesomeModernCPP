---
title: Concurrent Thinking and Fundamentals
description: 'Building concurrency judgment: understanding why we need concurrency,
  what problems it introduces, and how hardware and the OS support multithreading.'
translation:
  source: documents/vol5-concurrency/ch00-concurrency-fundamentals/index.md
  source_hash: db7f230c5115c9b8ecc6a129401c1476c99ab15f3e97da2897882a4360532094
  translated_at: '2026-05-20T04:32:04.066576+00:00'
  engine: anthropic
  token_count: 171
---
# Concurrent Thinking and Fundamentals

I believe that concurrency is a dividing line for C++ engineering skills. As single-core frequency gains hit the power wall, modern software performance improvements rely almost entirely on two directions: better algorithms, and better parallelization. The foundation of parallelization is concurrency—coordinating multiple flows of execution without breaking correctness, to fully leverage the computational power of multi-core hardware. Frankly, if the programs you write always execute sequentially in a single thread, no matter how beautiful the code is, you are wasting most of the transistors on the CPU in your hands.

However, before writing any multithreaded code, we must first answer three questions: why do we need concurrency? What problems can concurrency cause? And how do the CPU and operating system support multithreading? Most tutorials might start by teaching you `std::thread`, but I prefer not to do that. I want to establish the mindset of the concurrency domain first—correctness before performance, which is the principle of this entire volume.

## Chapter Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-why-concurrency">Why We Need Concurrency</ChapterLink>
  <ChapterLink href="02-concurrency-problems">Fundamental Concurrency Problems</ChapterLink>
  <ChapterLink href="03-cpu-cache-and-os-threads">CPU Cache and OS Threads</ChapterLink>
</ChapterNav>
