---
title: "Bridging to Distributed Systems"
description: "From standalone concurrency to distributed systems — understanding the foundational directions of consistency, consensus, and distributed communication"
translation:
  source: documents/vol5-concurrency/ch09-distributed-bridge/index.md
  source_hash: 6ad198c5b7a447e0a12e99cda9bb1a6ff9e222a96cd74be53834b15b23cf8ae8
  translated_at: '2026-09-26T08:44:45+00:00'
  engine: anthropic
  token_count: 650
---

# Bridging to Distributed Systems

In the preceding chapters we have been discussing concurrency on a single machine: threads, locks, atomic operations, lock-free data structures, coroutines, Actor/Channel. These are the foundation of concurrent programming. But in the real world, when one machine is no longer enough, you have to face the distributed environment — the network is unreliable, clocks are inaccurate, and partial failures are inevitable.

In this chapter we widen our view from within a single process to across the network, to see how knowledge from standalone concurrency carries over to distributed settings. We will discuss the fundamental differences of distributed systems, the spectrum of consistency models, the core ideas behind consensus protocols, and the practical direction of gRPC + C++20 coroutines in distributed communication. The goal is not to make you a distributed-systems expert — it is to help you build the cognitive bridge from "standalone concurrency" to "distributed systems", so you know where the boundary lies, which of the old lessons still apply, and which ones you have to rethink.

## In This Chapter

<ChapterNav variant="sub">
  <ChapterLink href="01-from-concurrent-to-distributed">From Standalone Concurrency to Distributed Systems</ChapterLink>
  <ChapterLink href="02-distributed-primitives">A First Look at Distributed Consistency Primitives</ChapterLink>
</ChapterNav>
