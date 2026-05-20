---
title: Mutexes, Condition Variables, and Synchronization Primitives
description: From `mutex` to `condition_variable` to `shared_mutex`, systematically
  master C++ wait-notify mechanisms and the reader-writer lock pattern.
translation:
  source: documents/vol5-concurrency/ch02-mutex-condition-sync/index.md
  source_hash: 0a9f166aca385f4ef92493ea129f0cb28eaf2386c7744e615b12cf321774c87e
  translated_at: '2026-05-20T04:36:50.895732+00:00'
  engine: anthropic
  token_count: 237
---
# Mutexes, Condition Variables, and Synchronization Primitives

In the previous chapter, we explored the thread lifecycle and RAII management—learning how to create threads and safely wait for them to finish. But threads alone are not enough; when multiple threads access the same data, we need a coordination mechanism. This chapter focuses on the most fundamental synchronization primitives in the C++ standard library: the mutex for protecting critical sections, the condition_variable for wait-notify coordination between threads, and the shared_mutex for concurrency optimization in read-heavy, write-light scenarios.

We will start with the basic usage of mutexes and RAII locks, understanding the differences between `lock_guard` and `unique_lock`. Then we will dive into the wait semantics of condition_variable, clarifying essential pitfalls like spurious wakeups and lost wakeups. Finally, we will introduce the C++17 shared_mutex, analyzing its applicable scenarios and performance boundaries. Every step is accompanied by compilable code examples and practical exercises.

## Chapter Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-mutex-and-raii-guards">mutex 与 RAII 锁</ChapterLink>
  <ChapterLink href="02-deadlock-and-lock-ordering">死锁与锁顺序</ChapterLink>
  <ChapterLink href="03-condition-variable">condition_variable 与等待语义</ChapterLink>
  <ChapterLink href="04-shared-mutex">读写锁与 shared_mutex</ChapterLink>
  <ChapterLink href="05-latch-barrier-semaphore">latch、barrier 与 semaphore</ChapterLink>
</ChapterNav>
