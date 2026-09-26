---
title: "Atomic Operations and the Memory Model"
description: "From the operation set of `std::atomic` to a complete breakdown of the six memory orders, building the theoretical foundation for lock-free programming"
translation:
  source: documents/vol5-concurrency/ch03-atomic-memory-model/index.md
  source_hash: e76a705266ead7ca4baa67780a358af0c38310cfc49ce24769fdbc07f7a6a5be
  translated_at: '2026-09-26T07:13:38+00:00'
  engine: anthropic
  token_count: 800
---

# Atomic Operations and the Memory Model

In the previous two chapters we discussed thread lifecycles and mutex-based synchronization — together they solved the fundamental problem of "how to make multiple threads cooperate safely." But a mutex carries an inherent cost: even when the critical section is nothing more than a simple increment of a single variable, you still have to go through the full lock → modify → unlock sequence. When performance requirements climb and critical sections shrink, we need lighter-weight tools.

In this chapter we enter the world of `std::atomic` and the C++ memory model. `std::atomic` taps the CPU's atomic instructions to guarantee that an operation is indivisible without taking a lock. Memory order, in turn, governs how the compiler and the CPU may reorder instructions, letting you make precise trade-offs between performance and predictability. Together the two form the theoretical foundation of lock-free programming — and the prerequisite for the lock-free data structures and atomic operation patterns discussed in later chapters.

## In This Chapter

<ChapterNav variant="sub">
  <ChapterLink href="01-atomic-operations">Atomic Operations</ChapterLink>
  <ChapterLink href="02-memory-ordering">A Deep Dive into Memory Ordering</ChapterLink>
  <ChapterLink href="03-fence-and-barrier">Fences and Compiler Barriers</ChapterLink>
  <ChapterLink href="04-atomic-wait-and-ref">atomic_wait and atomic_ref</ChapterLink>
  <ChapterLink href="05-atomic-patterns">Atomic Operation Patterns</ChapterLink>
</ChapterNav>
