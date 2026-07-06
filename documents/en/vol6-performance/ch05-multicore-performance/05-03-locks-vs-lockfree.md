---
chapter: 5
cpp_standard:
- 17
description: This article looks at concurrent synchronization through the cost lens. The fast path of an uncontended mutex is nanosecond-level, about 3.6x an atomic, and blows up under contention; lock-free is not a silver bullet (ABA, retry storms, memory reclamation headaches), and in many scenarios sharded locks are both faster and simpler than lock-free. "Whether to go lock-free" is a cost/complexity tradeoff that belongs to vol6; "how to write lock-free" belongs to vol5
difficulty: advanced
order: 3
platform: host
prerequisites:
- NUMA, affinity, and the scalability curve
- False sharing - one cacheline dragging many cores back to single-core
reading_time_minutes: 6
related:
- std::atomic and memory ordering (vol5)
tags:
- host
- cpp-modern
- advanced
- 优化
- 并发
- 无锁
title: 'Lock cost and "lock-free is not a silver bullet"'
translation:
  source: documents/vol6-performance/ch05-multicore-performance/05-03-locks-vs-lockfree.md
  source_hash: ad43f054bd5abd94261d2b649a3ee0c505884d852335e1357b66e90f25d4154a
  translated_at: '2026-07-06T00:00:00+00:00'
  engine: manual
  token_count: 3400
---
# Lock cost and "lock-free is not a silver bullet"

## Synchronization from the cost lens

The first two ch05 articles covered the physical bottlenecks of multicore (false sharing, NUMA, scalability). This one switches to the cost lens of **synchronization primitives**: `std::mutex`, `std::atomic`, lock-free data structures, how much each costs and when each is worth it.

First, the boundary (this one matters most): **"how to write correct synchronization, atomic-operation memory ordering, and lock-free data-structure implementations" belongs to vol5**. vol6 only answers one question: **"how many nanoseconds does each of these synchronization styles cost, and which should you pick when"**. This is the cost lens, not mechanism teaching.

## Uncontended locks: nanosecond-level, but 3-4x an atomic

A lot of people's impression of `std::mutex` is "slow". **Uncontended, it really isn't slow**: the fast path of a modern mutex is a user-space CAS (spin a few times), and only falls into the kernel when it can't get it. We measured a single thread (completely uncontended) incrementing a hundred million times:

```text
===== Synchronization cost (single thread incrementing 100M times, ns/op) =====
  non-atomic int (baseline):       0.00 ns
  atomic relaxed (lock-free, weak ordering):  1.86 ns
  atomic seq_cst (default strong ordering):    1.84 ns
  mutex uncontended (lock/unlock):       6.60 ns
  mutex/atomic_relaxed = 3.6x
```

A few things to read off:

- **Non-atomic int baseline 0.00 ns**: the compiler optimizes `++plain` into a register self-add, no memory write; that's the limit of "no synchronization".
- **`atomic` about 1.8-1.9 ns/op**: this is the cost of a lock-free atomic operation (`fetch_add`, when the return value is discarded, is often optimized by GCC into `lock addq` (a constant atomic add); when the return value is used it's `lock xadd`; both carry the LOCK prefix, and with a cacheline-aligned target they use a cache lock rather than a bus lock). **`memory_order_relaxed` and the default `seq_cst` are nearly identical on a single thread**: their difference is in cross-thread ordering, and a single-threaded increment has no cross-thread visibility requirement, so the difference doesn't show.
- **`mutex` uncontended 6.6 ns/op**: **about 3.6x an atomic**. This multiplier is the typical cost of an uncontended mutex: the fast path is a handful of instructions (CAS to grab the lock, CAS to release), a few more than a single `lock xadd`.

So **under no contention, mutex is 3-4x slower than atomic, but both are nanosecond-level**. If your critical section is just one atomic increment, using a mutex is indeed wasteful (a direct atomic is faster); but if the critical section has dozens of instructions, the 6-nanosecond mutex cost is negligible against the critical section itself, **and readability and correctness matter far more than that overhead**.

## Under contention: the mutex cost blows up

The above is **uncontended**. What `mutex` really punishes you on is **contended**: multiple threads grab at once, the fast-path CAS fails, spins, still can't get it, falls into the kernel (`futex` syscall), the thread gets suspended and woken, with context switches involved (a few microseconds each). A highly contended mutex can degenerate the program into "the kernel scheduler is switching threads and the business logic barely runs".

That's the motivation for lock-free data structures: **avoid falling into the kernel, avoid context switches**. `std::atomic`'s `lock xadd` stays in user space and is always lock-free (never falls into the kernel), no matter the contention. For **extremely high contention plus a tiny critical section** (say a global counter, a simple queue), lock-free genuinely wins.

But—

## Lock-free is not a silver bullet

"Lock-free" sounds like a silver bullet, but the pits run deep:

1. **The ABA problem**: in a lock-free compare-and-swap (CAS) loop, the value goes A→B→A, and CAS thinks "nothing changed" and succeeds, even though it was modified in between. The classic lock-free stack `pop` CASes repeatedly, and once a node is freed and reused you're hit. The fixes (tagged pointer, hazard pointer, epoch-based reclamation) are all complex and error-prone.
2. **Retry storms**: multiple threads CAS the same variable at once, only one wins, the rest all fail and retry; under high contention the CPU burns entirely on retries, worse than a mutex (called "thundering herd" or "live-lock").
3. **Memory reclamation is hard**: in a lock-free data structure, "can this node be freed yet" is itself a concurrent puzzle (another thread may still hold a pointer to it). This is the hardest part of lock-free programming.
4. **Writing it correctly is extremely hard**: the correctness of lock-free code relies on the careful cooperation of memory ordering (`acquire/release/seq_cst`); get one wrong and it's a data race UB, and even TSan might not catch all of it.

**Conclusion: lock-free is a high-bar, high-complexity tool, not a synonym for "faster"**. In many scenarios, **sharded locks** are both faster and simpler than lock-free: slice a shared structure into N pieces, one lock per piece, and threads most likely operate on different pieces and don't contend. For example a sharded hash table: N bucket groups, one mutex per group, the actual contention gets diluted to nearly uncontended. This "shard plus lock" pattern routinely beats lock-free in engineering, because the uncontended mutex fast path is extremely fast (nanosecond-level, as measured earlier), sharding drops the contention to uncontended and enjoys the fast path, and the code is simple with easy correctness.

## Decision framework: what to use when

| Scenario | Recommendation | Reason |
|---|---|---|
| Simple counter / simple statistics | `std::atomic` | One `lock xadd`, no critical section, cheapest |
| Complex shared structure, low contention | `std::mutex` + RAII | Readable, correct, uncontended fast path is fast enough |
| Highly contended shared structure | **Sharded locks** | Dilutes contention, simple and reliable, routinely beats lock-free |
| Extreme high concurrency + tiny critical section + a team that can handle it | Lock-free data structure | Has its scenarios, but you pay the complexity tax |
| Cross-thread read-only sharing | `std::shared_ptr` / share const directly | Reads don't contend, no sync needed |

**"Whether to go lock-free" is a cost/complexity tradeoff that belongs to vol6; "how to write correct lock-free" belongs to vol5.** First ask "is mutex plus sharding enough"; most of the time it is. Only reach for lock-free when it genuinely isn't, and pair it with rigorous TSan validation.

In one sentence: the uncontended mutex is nanosecond-level, about 3.6x an atomic, while under contention it blows up (kernel falls, context switches); a single atomic operation stays in user space and stays lock-free, suitable for simple counters; lock-free is not a silver bullet (ABA, retry storms, memory reclamation, extremely hard to write correctly), and sharded locks routinely beat it by diluting contention plus being simple and reliable; first consider sharding, then consider lock-free; vol6 only answers "how much each costs and which to pick", and "how to write it correctly" belongs to vol5.

That wraps ch05 multicore performance: false sharing, NUMA/scalability, and synchronization cost all collected. The next article switches to ch06, looking at "the performance cost of C++ abstractions": how much each of those C++-specific features (virtual functions, exceptions, std::function, optional/variant) costs.

## References

- Bakhvalov, *Performance Analysis and Tuning on Modern CPUs* Chapter 11 *Multithreaded Apps*
- Pikus, *The Art of Writing Efficient Programs* — concurrent performance, sharded-vs-lock-free tradeoffs (local)
- `std::mutex` / `std::atomic` / memory ordering: cppreference; depth belongs to vol5
- The measurement code for this article: `code/volumn_codes/vol6-performance/ch05/lock_cost.cpp`
