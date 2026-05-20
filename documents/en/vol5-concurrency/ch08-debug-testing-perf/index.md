---
title: Debugging, Testing, and Performance
description: Using toolchains to ensure the correctness and performance of concurrent
  programs — from ThreadSanitizer to Google Benchmark
translation:
  source: documents/vol5-concurrency/ch08-debug-testing-perf/index.md
  source_hash: 60f2c89a29b4c8172717553e6665c5022579800634ea6fdada8b537313557964
  translated_at: '2026-05-20T04:48:54.405862+00:00'
  engine: anthropic
  token_count: 130
---
# Debugging, Testing, and Performance

Writing concurrent code does not mean the job is done—we have to verify that it is both correct and efficient. Concurrent bugs have a particularly insidious nature: they might run ten thousand times without issue on your development machine, only to blow up intermittently in production. Furthermore, "good performance" in a concurrent context is an engineering problem that demands scientific measurement, not gut feeling.

In this chapter, we tackle two ultimate questions: first, how to systematically discover and diagnose concurrent bugs (data races, dead locks, livelocks, and dangling references) using tools; second, how to scientifically measure the performance of concurrent programs while avoiding the various pitfalls of benchmarking.

## Chapter Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-debugging-concurrency">Concurrent Program Debugging Techniques</ChapterLink>
  <ChapterLink href="02-concurrency-benchmarks">Concurrent Performance Testing and Benchmarking</ChapterLink>
</ChapterNav>
