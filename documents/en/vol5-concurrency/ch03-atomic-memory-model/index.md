---
title: Atomic Operations and Memory Models
description: From the operation set of `std::atomic` to a complete breakdown of the
  six memory orders, building the theoretical foundation for lock-free programming.
translation:
  source: documents/vol5-concurrency/ch03-atomic-memory-model/index.md
  source_hash: e76a705266ead7ca4baa67780a358af0c38310cfc49ce24769fdbc07f7a6a5be
  translated_at: '2026-05-20T04:39:19.735788+00:00'
  engine: anthropic
  token_count: 207
---
# Atomic Operations and Memory Models

In the previous two chapters, we discussed thread lifecycles and mutex synchronization mechanisms—solving the fundamental problem of "how to make multiple threads cooperate safely." However, mutexes come with an inherent cost: even for a simple increment of a single variable in a critical section, we must go through the full lock → modify → unlock sequence. When performance requirements are higher and critical sections are smaller, we need lighter-weight tools.

In this chapter, we dive into the world of `std::atomic` and the C++ memory model. `std::atomic` leverage CPU atomic instructions to guarantee the indivisibility of operations without locking. Memory order controls instruction reordering behavior by the compiler and CPU, allowing us to make precise trade-offs between performance and predictability. Together, these two form the theoretical foundation of lock-free programming—prerequisite knowledge for the lock-free data structures and atomic operation patterns discussed in subsequent chapters.

## Chapter Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-atomic-operations">atomic 操作</ChapterLink>
  <ChapterLink href="02-memory-ordering">内存序详解</ChapterLink>
  <ChapterLink href="03-fence-and-barrier">fence 与编译器屏障</ChapterLink>
  <ChapterLink href="04-atomic-wait-and-ref">atomic_wait 与 atomic_ref</ChapterLink>
  <ChapterLink href="05-atomic-patterns">原子操作模式</ChapterLink>
</ChapterNav>
