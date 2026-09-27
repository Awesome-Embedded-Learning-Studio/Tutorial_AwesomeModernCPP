---
title: "Debugging, Testing, and Performance"
description: "Safeguarding the correctness and performance of concurrent programs with the right tooling — from ThreadSanitizer to Google Benchmark"
translation:
  source: documents/vol5-concurrency/ch08-debug-testing-perf/index.md
  source_hash: 60f2c89a29b4c8172717553e6665c5022579800634ea6fdada8b537313557964
  translated_at: '2026-09-26T06:40:52+00:00'
  engine: anthropic
  token_count: 350
---

# Debugging, Testing, and Performance

Finishing your concurrent code is not the finish line — you still have to confirm that it is both correct and fast. Concurrency bugs have a particularly insidious quality: they may run flawlessly ten thousand times on your dev machine, then blow up in production every few days. And under concurrency, "good performance" becomes an engineering question that demands scientific measurement, not something you settle by gut feeling.

In this chapter we tackle two ultimate questions: first, how to use tooling to systematically detect and diagnose concurrency bugs (data races, deadlocks, livelocks, dangling references); second, how to scientifically measure the performance of a concurrent program, steering clear of the many traps that benchmarks lay for you.

## In This Chapter

<ChapterNav variant="sub">
  <ChapterLink href="01-debugging-concurrency">Debugging Techniques for Concurrent Programs</ChapterLink>
  <ChapterLink href="02-concurrency-benchmarks">Concurrency Performance Testing and Benchmarking</ChapterLink>
</ChapterNav>
