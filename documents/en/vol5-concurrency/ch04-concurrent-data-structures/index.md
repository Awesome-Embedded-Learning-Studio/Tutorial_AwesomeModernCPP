---
title: "Concurrent Data Structures"
description: "From thread-safe queues to concurrent containers: master the design strategies of lock-based concurrent data structures"
translation:
  source: documents/vol5-concurrency/ch04-concurrent-data-structures/index.md
  source_hash: 6aa5f31faf334fb42dc8a40618b9ebf9bae8d5c4df5e6f4ef83eccd1d310706c
  translated_at: '2026-09-26T08:13:28+00:00'
  engine: anthropic
  token_count: 350
---

# Concurrent Data Structures

Over the past two chapters we sorted out synchronization primitives (`mutex`, `condition_variable`, `shared_mutex`) and atomic operations (`atomic`, memory order). Now it's time to put those tools to work — this chapter focuses on designing and implementing concurrent data structures. They are the core components of multithreaded programs: the task queue inside a thread pool, the routing cache in a server, the buffer of a messaging system — behind all of them you need data structures that are safe for concurrent access.

We'll start with the most practical one, the thread-safe queue — it's the cornerstone of the producer-consumer pattern, and the best case study for understanding "how to build a correct concurrent component with `mutex` + `condition_variable`". Then we'll widen the scope to concurrent container design in general, discussing the design and trade-offs of four strategies: coarse-grained locking, fine-grained locking, sharded locking, and copy-on-write. Finally we step into the territory of lock-free programming — from CAS loops and the ABA problem to the SPSC ring buffer and the Michael-Scott MPMC queue — building up your ability to design and reason about lock-free concurrent data structures.

## In This Chapter

<ChapterNav variant="sub">
  <ChapterLink href="00-os-threads-and-cost">OS Threads and Their Cost</ChapterLink>
  <ChapterLink href="01-thread-safe-queue">Thread-Safe Queue</ChapterLink>
  <ChapterLink href="02-thread-safe-containers">Thread-Safe Container Design</ChapterLink>
  <ChapterLink href="03-lock-free-basics">Lock-Free Programming Fundamentals</ChapterLink>
  <ChapterLink href="04-lock-free-queues">SPSC and MPMC Queues</ChapterLink>
</ChapterNav>
