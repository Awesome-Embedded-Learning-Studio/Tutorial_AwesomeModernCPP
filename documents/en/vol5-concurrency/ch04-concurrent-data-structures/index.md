---
title: Concurrent Data Structures
description: From thread-safe queues to concurrent containers, master lock-based concurrent
  data structure design strategies.
translation:
  source: documents/vol5-concurrency/ch04-concurrent-data-structures/index.md
  source_hash: a14ec2edba40c33ca7c8caec9a8a57ca94344cbcf67f97eac70d264050d5b7d3
  translated_at: '2026-05-20T04:41:26.882048+00:00'
  engine: anthropic
  token_count: 202
---
# Concurrent Data Structures

In the previous two chapters, we covered synchronization primitives (`mutex`, `condition_variable`, `shared_mutex`) and atomic operations (`atomic`, memory order). Now it is time to put these tools to use — in this chapter, we focus on the design and implementation of concurrent data structures. Concurrent data structures are core components of multithreaded programs: task queues in thread pools, routing caches in servers, and buffers in messaging systems all rely on thread-safe data structures behind the scenes.

We start with the most practical thread-safe queue — it is the cornerstone of the producer-consumer pattern and the best case study for understanding "how to build correct concurrent components using `mutex` + `condition_variable`." We then expand to more general concurrent container design, discussing the design and trade-offs of four strategies: coarse-grained locking, fine-grained locking, sharded locking, and copy-on-write. Finally, we enter the realm of lock-free programming — from CAS loops and the ABA problem to SPSC ring buffers and the Michael-Scott MPMC queue, building our ability to design and evaluate lock-free concurrent data structures.

## Chapter Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-thread-safe-queue">Thread-Safe Queue</ChapterLink>
  <ChapterLink href="02-thread-safe-containers">Thread-Safe Container Design</ChapterLink>
  <ChapterLink href="03-lock-free-basics">Lock-Free Programming Basics</ChapterLink>
  <ChapterLink href="04-lock-free-queues">SPSC and MPMC Queues</ChapterLink>
</ChapterNav>
