---
title: Distributed Bridging
description: From single-machine concurrency to distributed systems — understanding
  the fundamentals of consistency, consensus, and distributed communication
translation:
  source: documents/vol5-concurrency/ch09-distributed-bridge/index.md
  source_hash: 6ad198c5b7a447e0a12e99cda9bb1a6ff9e222a96cd74be53834b15b23cf8ae8
  translated_at: '2026-05-20T04:50:26.296575+00:00'
  engine: anthropic
  token_count: 144
---
# Distributed Bridging

In previous chapters, we focused on concurrency on a single machine: threads, locks, atomic operations, lock-free data structures, coroutines, and Actor/Channel models. These form the foundation of concurrent programming. In reality, however, when a single machine is no longer enough, we must face distributed environments—where networks are unreliable, clocks are inaccurate, and partial failures are inevitable.

In this chapter, we shift our perspective from intra-process to network-level, exploring how single-machine concurrency knowledge transfers to distributed scenarios. We discuss the fundamental differences of distributed systems, the spectrum of consistency models, the core ideas behind consensus protocols, and practical approaches to distributed communication using gRPC combined with C++20 coroutines. The goal here isn't to make you a distributed systems expert—rather, it's to help you build a cognitive bridge from "single-machine concurrency" to "distributed systems," so you know where the boundaries lie, which past experiences still apply, and which ones require rethinking.

## Chapter Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-from-concurrent-to-distributed">From Single-Machine Concurrency to Distributed Systems</ChapterLink>
  <ChapterLink href="02-distributed-primitives">A First Look at Distributed Consistency Primitives</ChapterLink>
</ChapterNav>
