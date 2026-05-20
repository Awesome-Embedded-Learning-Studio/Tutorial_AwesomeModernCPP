---
title: 'Volume 5: Concurrent Programming'
description: From thread primitives to coroutine asynchrony
platform: host
tags:
- cpp-modern
- host
- intermediate
translation:
  source: documents/vol5-concurrency/index.md
  source_hash: 3427ea3f5f24fcfaedd35709cc0d45fdafd47e1ad39439cec530dcfaad4d92dc
  translated_at: '2026-05-20T04:50:39.501505+00:00'
  engine: anthropic
  token_count: 385
---
# Volume 5: Concurrent Programming

From thread primitives to asynchronous coroutines, from locks to lock-free, from synchronization to tasks — Volume 5 helps you build complete concurrency judgment. Our principle is: **correctness first, performance second; locks first, lock-free second; synchronous first, asynchronous second**.

## Chapter Navigation

<ChapterNav variant="sub">
  <ChapterLink href="ch00-concurrency-fundamentals">ch00 · Concurrency Thinking and Fundamentals</ChapterLink>
  <ChapterLink href="ch01-thread-lifecycle-raii">ch01 · Thread Lifecycle and RAII</ChapterLink>
  <ChapterLink href="ch02-mutex-condition-sync">ch02 · Mutexes, Condition Variables, and Synchronization Primitives</ChapterLink>
  <ChapterLink href="ch03-atomic-memory-model">ch03 · Atomic Operations and Memory Models</ChapterLink>
  <ChapterLink href="ch04-concurrent-data-structures">ch04 · Concurrent Data Structures</ChapterLink>
  <ChapterLink href="ch05-future-task-threadpool">ch05 · Futures, Tasks, and Thread Pools</ChapterLink>
  <ChapterLink href="ch06-async-io-coroutine">ch06 · Asynchronous I/O and Coroutines</ChapterLink>
  <ChapterLink href="ch07-actor-channel">ch07 · Actors and Channels</ChapterLink>
  <ChapterLink href="ch08-debug-testing-perf">ch08 · Debugging, Testing, and Performance</ChapterLink>
  <ChapterLink href="ch09-distributed-bridge">ch09 · Distributed Bridging Appendix</ChapterLink>
</ChapterNav>

## Legacy Articles (Being Gradually Rewritten)

The following articles belong to the legacy structure and will be archived and replaced as the new chapters are completed.

<ChapterNav variant="sub">
  <ChapterLink href="01-atomic">Atomic Operations</ChapterLink>
  <ChapterLink href="02-memory-order">Memory Order</ChapterLink>
  <ChapterLink href="03-lock-free-data-structures">Lock-Free Data Structures</ChapterLink>
  <ChapterLink href="04-mutex-and-raii-guards">Mutexes and RAII Guards</ChapterLink>
  <ChapterLink href="06-critical-section-protection">Critical Section Protection</ChapterLink>
  <ChapterLink href="03-coroutine-echo-server">Coroutine Echo Server</ChapterLink>
</ChapterNav>
