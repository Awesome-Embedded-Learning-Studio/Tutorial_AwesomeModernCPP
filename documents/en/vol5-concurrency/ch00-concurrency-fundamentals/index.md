---
title: "Concurrent Thinking and Fundamentals"
description: "Building sound judgment for concurrency: understanding why we need concurrency, what problems it can cause, and how hardware and the OS support multithreading"
translation:
  source: documents/vol5-concurrency/ch00-concurrency-fundamentals/index.md
  source_hash: db7f230c5115c9b8ecc6a129401c1476c99ab15f3e97da2897882a4360532094
  translated_at: '2026-09-26T06:15:08+00:00'
  engine: anthropic
  token_count: 300
---

# Concurrent Thinking and Fundamentals

In our view, concurrency is a watershed in C++ engineering ability. Once the growth in single-core frequency ran into the power wall, nearly all further gains in modern software performance came to depend on two directions: better algorithms, and better parallelization. And the foundation of parallelization is concurrency — letting multiple flows of execution work in concert, without breaking correctness, to squeeze the full compute out of multi-core hardware. Frankly, if the program you write always runs single-threaded and sequential, then no matter how beautiful the code is, you are wasting most of the transistors on that CPU of yours.

Before we write any multithreaded code, though, we must first answer three questions: why do we need concurrency? What exactly goes wrong when concurrency enters the picture? And how do the CPU and the operating system support multiple threads? Most tutorials would open by teaching you `std::thread` right away; we would rather not. We want to build the mindset of the concurrency field first — correctness before performance. That is the principle for this entire volume.

## In This Chapter

<ChapterNav variant="sub">
  <ChapterLink href="01-why-concurrency">Why We Need Concurrency</ChapterLink>
  <ChapterLink href="02-concurrency-problems">Fundamental Concurrency Problems</ChapterLink>
  <ChapterLink href="03-cpu-cache-and-os-threads">CPU Cache and OS Threads</ChapterLink>
</ChapterNav>
