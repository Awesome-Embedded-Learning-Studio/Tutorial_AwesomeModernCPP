---
title: "Multicore performance"
description: "ch05 picks up where vol5 left off (the correctness of synchronization primitives and lock-free code belongs to vol5) and focuses only on the performance tax multicore brings and how to measure and fix it: false sharing (one cacheline dragging many cores back to single-core speed, measured at one order of magnitude, about 18x in a single run with large variance), NUMA and the scalability curve (1 to 8 threads measured at a sublinear 2.53x), and lock cost versus the 'lock-free is not a silver bullet' reality (uncontended mutex about 3.6x an atomic)"
---

# Multicore performance

vol5 thoroughly covers **the correctness of synchronization primitives, memory ordering, and lock-free data structures**. This chapter doesn't repeat any of that mechanism; it answers one performance question: **how do you measure and fix the performance decay multicore brings, and how many nanoseconds does each synchronization style cost.**

Three articles:

- **05-01 False sharing**: two cores frequently writing different variables sitting on the same 64-byte cacheline trigger MESI coherence invalidate round-trips, measured at **about an order of magnitude** slower (about 18x in a single run, multiplier varies a lot between runs). The fix is `alignas(64)`.
- **05-02 NUMA and the scalability curve**: cross-socket memory access latency doubles to quadruples; the scalability curve (1 to 8 threads measured at a sublinear 2.53x) diagnoses "how much performance buying more cores actually buys you"; Amdahl vs Gustafson; thread affinity; thread creation and stack cost.
- **05-03 Lock versus lock-free cost**: uncontended mutex is nanosecond-level (about 3.6x an atomic), but blows up under contention; lock-free is not a silver bullet (ABA, retry storms, memory reclamation), and sharded locks routinely beat lock-free.

Boundary: **"how to write correct synchronization, atomic memory ordering, and lock-free implementations" belongs to vol5**; vol6 only answers "how much each costs and which to pick when."

> This machine is WSL2 on a single-socket single-NUMA laptop, so NUMA cross-node penalty can't be measured here (05-02 marks this honestly, content is drawn from multi-socket server practice). False sharing, scalability, and lock cost are all measured on this machine.
