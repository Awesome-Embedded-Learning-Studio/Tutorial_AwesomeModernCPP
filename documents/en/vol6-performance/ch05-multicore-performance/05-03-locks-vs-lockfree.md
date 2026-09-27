---
chapter: 5
cpp_standard:
- 17
description: 'This article looks at concurrent synchronization through the cost lens:
  an uncontended mutex fast path is nanosecond-level, about 3.6x an atomic, and blows
  up under contention; lock-free is not a silver bullet (ABA, retry storms, memory
  reclamation headaches), and in many scenarios sharded locks are both faster and
  simpler than lock-free. "Whether to go lock-free" is a cost/complexity tradeoff
  that belongs to vol6; "how to write lock-free" belongs to vol5'
difficulty: advanced
order: 3
platform: host
prerequisites:
- NUMA, affinity, and the scalability curve
- 'False sharing: one cacheline dragging many cores back to single-core'
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
title: 'Lock overhead and "lock-free is not a silver bullet"'
translation:
  source: documents/vol6-performance/ch05-multicore-performance/05-03-locks-vs-lockfree.md
  source_hash: ad43f054bd5abd94261d2b649a3ee0c505884d852335e1357b66e90f25d4154a
  translated_at: '2026-09-26T06:40:37+00:00'
  engine: anthropic
  token_count: 3900
---
# Lock overhead and "lock-free is not a silver bullet"

## Synchronization through the cost lens

The first two articles of ch05 covered the physical bottlenecks of multicore (false sharing, NUMA, scalability). This one switches to the cost lens on **synchronization primitives**: `std::mutex`, `std::atomic`, lock-free data structures — how much each costs, and when it is worth it.

Let me draw the boundary first (this is the most important line): **"how to write correct synchronization, the memory-ordering semantics of atomic operations, and how to implement lock-free data structures" belongs to vol5**. vol6 answers exactly one question: **"how many nanoseconds does each of these synchronization styles cost, and which one fits which scenario"**. This is the cost lens, not a mechanisms tutorial.

## Uncontended locks: nanosecond-level, but 3-4x the cost of an atomic

Many people's impression of `std::mutex` is "slow". **Uncontended, it is actually not slow** — the fast path of a modern mutex is a userspace CAS (spin a few times), and only if it still cannot get the lock does it fall into the kernel. We measured a single thread (fully uncontended) incrementing 100 million times:

```text
===== 同步开销(单线程自增 1 亿次,ns/op)=====
  非原子 int(基线):           0.00 ns
  atomic relaxed(无锁,弱序):  1.86 ns
  atomic seq_cst(默认强序):    1.84 ns
  mutex 无竞争(加解锁):       6.60 ns
  mutex/atomic_relaxed = 3.6x
```

A few things to read out of this:

- **Non-atomic int baseline: 0.00 ns** — the compiler optimized `++plain` into a register increment with no memory write; that is the limit of "no synchronization".
- **`atomic` at roughly 1.8-1.9 ns/op** — this is the cost of a lock-free atomic operation (when the return value of `fetch_add` is discarded, GCC often optimizes it into `lock addq` (an atomic add of a constant); when the return value is used, it becomes `lock xadd`; both carry the LOCK prefix, and with the cacheline aligned it takes a cache lock rather than a bus lock). **`memory_order_relaxed` and the default `seq_cst` are nearly identical single-threaded** — their difference is in cross-thread ordering, and a single-threaded increment has no cross-thread visibility requirement, so the difference never shows.
- **`mutex` uncontended: 6.6 ns/op** — **about 3.6x an atomic**. This multiplier is the typical overhead of an uncontended mutex: the fast path is a few instructions (a CAS to take the lock, a CAS to give it back), several more than the single `lock xadd`.

So **uncontended, a mutex is 3-4x slower than an atomic — but both are nanosecond-level**. If your critical section is a single atomic increment, using a mutex really is a waste (a plain atomic is faster); but if the critical section holds dozens of instructions, the mutex's 6 nanoseconds are negligible against the section itself, and **readability and correctness matter far more than that sliver of overhead**.

## Under contention: the mutex cost explodes

That was the **uncontended** case. Where a `mutex` gets genuinely expensive is **contention**: several threads grab it at the same time, the fast-path CAS fails, spinning still does not get it, and the thread drops into the kernel (a `futex` syscall); the thread gets suspended and woken up, with context switches in the mix (a few microseconds apiece). A heavily contended mutex can degrade a program into "the kernel scheduler switching threads while the actual work barely runs".

That is the motivation for lock-free data structures: **stay out of the kernel, avoid context switches**. `std::atomic`'s `lock xadd` always stays in userspace and is always lock-free (never enters the kernel), no matter how heavy the contention. For **extreme contention + a tiny critical section** (a global counter, a simple queue), lock-free genuinely wins.

But —

## Lock-free is not a silver bullet

"Lock-free" sounds like a silver bullet; in practice the pitfalls run deep:

1. **The ABA problem**: in a lock-free compare-and-swap (CAS) loop, the value goes A→B→A, and the CAS succeeds believing "it never changed" even though it was modified in between. The classic lock-free stack `pop` CASes in a loop, and a node that gets freed and then reused triggers exactly this. The remedies (tagged pointers, hazard pointers, epoch-based reclamation) are all complicated and easy to get wrong.
2. **Retry storms**: many threads CAS the same variable simultaneously; exactly one wins and all the others fail and retry. Under heavy contention the CPUs burn entirely on retries — ending up worse than a mutex (known as "thundering herd" or "live-lock").
3. **Memory reclamation is hard**: in a lock-free data structure, "can this node be freed yet" is itself a concurrency problem (another thread may still be holding the pointer). This is the most hardcore part of lock-free programming.
4. **Getting it right is brutally hard**: the correctness of lock-free code rests on the fine-grained interplay of memory ordering (`acquire/release/seq_cst`); one mistake and you have a data race UB — and even TSan will not necessarily catch them all.

**The conclusion: lock-free is a high-bar, high-complexity tool, not a synonym for "faster"**. In many scenarios, **sharded locks** are faster and simpler than lock-free: split one shared structure into N shards with one lock each, and threads most likely operate on different shards and never contend. Take a sharded hash table — N bucket groups, one mutex per group — and the real contention thins out to nearly uncontended. This "sharding + locks" approach routinely beats lock-free in real engineering, because the uncontended mutex fast path is extremely fast (nanosecond-level, as we measured above), sharding drops contention back down to uncontended levels where the fast path applies, and the code stays simple and easy to keep correct.

## A decision framework: what to use when

| Scenario | Recommendation | Why |
|---|---|---|
| A plain counter / simple statistics | `std::atomic` | One `lock xadd`, no critical section, the cheapest |
| A complex shared structure with weak contention | `std::mutex` + RAII | Readable, correct, and the uncontended fast path is fast enough |
| A heavily contended shared structure | **Sharded locks** | Thins out contention, simple and reliable, often beats lock-free |
| Extreme concurrency + a tiny critical section + a team that can handle it | Lock-free data structures | The niche exists, but you pay the complexity tax |
| Read-only sharing across threads | `std::shared_ptr` / share `const` directly | Reads do not contend, no synchronization needed |

**"Whether to go lock-free" is a cost/complexity tradeoff and belongs to vol6; "how to write correct lock-free code" belongs to vol5.** Ask "is mutex + sharding enough" first — most of the time it is; only go lock-free when it truly is not, and pair that with rigorous TSan validation.

Compressed into one sentence: an uncontended mutex is nanosecond-level, about 3.6x an atomic, and explodes under contention (kernel entry, context switches); a single atomic operation always stays in userspace and is always lock-free, fitting simple counters; lock-free is not a silver bullet (ABA, retry storms, memory reclamation, and extreme difficulty of getting it right), and sharded locks often beat lock-free by thinning contention while staying simple and reliable — consider sharding first, lock-free second; vol6 only answers "how much each costs and which one to pick", while "how to write it correctly" belongs to vol5.

That completes ch05 multicore performance: false sharing, NUMA/scalability, and synchronization cost, all three pieces collected. Next up is ch06, where we look through the lens of "the performance cost of C++ abstractions" at how much each of those C++-specific features (virtual functions, exceptions, `std::function`, optional/variant) costs.

## References

- Bakhvalov, *Performance Analysis and Tuning on Modern CPUs*, Chapter 11, *Multithreaded Apps*
- Pikus, *The Art of Writing Efficient Programs* — concurrency performance, sharding vs lock-free tradeoffs (local)
- `std::mutex` / `std::atomic` / memory ordering: cppreference; the depth belongs to vol5
- The measurement code for this article: `code/volumn_codes/vol6-performance/ch05/lock_cost.cpp`
