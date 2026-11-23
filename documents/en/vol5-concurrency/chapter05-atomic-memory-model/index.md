---
title: "Unit 5: Understanding Atomic Operations and the Memory Model"
description: "From the capability boundary of atomic operations to happens-before, then choosing a memory order for every atomic operation"
translation:
  source: documents/vol5-concurrency/chapter05-atomic-memory-model/index.md
  source_hash: 9b3a2315bb28487dee9364eb49f4e41e25ea807c7c8c513b642036e6a6e6a4ae
  translated_at: '2026-11-22T13:29:16+00:00'
  engine: glm-5.3
  token_count: 690
---

# Unit 5: Understanding Atomic Operations and the Memory Model

The earlier units put threads, sharing, waiting, and tasks all in place, and you have used the tools fluently. This unit steps back half a pace and asks what lies beneath the tools: how far atomic operations actually guarantee, how the order between two operations is reckoned, and what to go by when choosing a memory order for each atomic operation. The unit's content is one article per section; all five are in place.

## In This Unit

<ChapterNav variant="sub">
  <ChapterLink href="01-what-atomics-solve">How Far Atomic Operations Take You: Types, Operations, and CAS</ChapterLink>
  <ChapterLink href="02-atomic-flag-and-boundary">atomic_flag and the Boundary of Atomicity: One Action Is Not One Transaction</ChapterLink>
  <ChapterLink href="03-happens-before">How One Thread Observes Another's Modifications</ChapterLink>
  <ChapterLink href="04-choosing-memory-orders">The Four Tiers of Memory Order: From Sequential Consistency to relaxed</ChapterLink>
  <ChapterLink href="05-memory-order-patterns">A Practice Ground for the Criteria: Stop Flags, Reference Counting, and DCLP</ChapterLink>
</ChapterNav>
