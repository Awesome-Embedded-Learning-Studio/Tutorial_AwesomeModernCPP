---
title: "Multicore performance"
description: "ch05 picks up where vol5 left off (the correctness of synchronization primitives and lock-free code belongs to vol5) and covers only the performance decay and cost multicore brings: false sharing (one cacheline dragging many cores back to single-core, measured at one order of magnitude, about 18x in a single run with large run-to-run variance), NUMA and the scalability curve (1→8 threads measured at a sublinear 2.53x), and lock overhead with the 'lock-free is not a silver bullet' reality (an uncontended mutex is about 3.6x an atomic)"
translation:
  source: documents/vol6-performance/ch05-multicore-performance/index.md
  source_hash: f7f398d5cf199617b36f924a348d1bb872b2bbc2d7fd10e7b3f3505d9b1f9d46
  translated_at: '2026-09-26T06:41:07+00:00'
  engine: anthropic
  token_count: 1250
---

# Multicore performance

vol5 covered **the correctness of synchronization primitives, memory ordering, and lock-free data structures** thoroughly. This chapter doesn't repeat any of that machinery; it answers one performance question: **how to measure and fix the performance decay multicore brings, and how many nanoseconds each synchronization style costs.**

Three articles:

- **05-01 False sharing**: two cores frequently writing different variables sitting on the same 64-byte cacheline trigger MESI coherence invalidate round-trips, measured **an order of magnitude** slower (about 18x in a single run, and the multiplier swings a lot between runs). The fix is `alignas(64)`.
- **05-02 NUMA and the scalability curve**: on multi-socket machines, cross-node memory access latency is 2-4x higher; the scalability curve (measured 1→8 threads at a sublinear 2.53x) diagnoses "how much performance more cores actually buy you"; Amdahl vs Gustafson; thread affinity (core pinning); thread creation and stack cost.
- **05-03 Lock vs lock-free cost**: an uncontended mutex is nanosecond-level (about 3.6x an atomic), but the cost explodes under contention; lock-free is not a silver bullet (ABA, retry storms, memory reclamation), and sharded locks routinely beat lock-free.

Boundary: **"how to write correct synchronization, atomic-operation memory ordering, and lock-free implementations" belongs to vol5**; vol6 only answers "how much each costs and which to pick in which scenario."

> This machine is WSL2 on a single socket with a single NUMA node, so the cross-node NUMA penalty can't be measured here (05-02 marks this honestly; that content is drawn from multi-socket server practice). False sharing, scalability, and lock overhead were all measured on this machine.

## In this chapter

<ChapterNav variant="sub">
  <ChapterLink href="05-01-false-sharing">False sharing: one cacheline dragging many cores back to single-core</ChapterLink>
  <ChapterLink href="05-02-numa-scaling">NUMA, affinity, and the scalability curve</ChapterLink>
  <ChapterLink href="05-03-locks-vs-lockfree">Lock overhead and "lock-free is not a silver bullet"</ChapterLink>
</ChapterNav>
