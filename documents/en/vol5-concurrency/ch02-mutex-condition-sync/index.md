---
title: "Mutexes, Condition Variables, and Synchronization Primitives"
description: "From mutex to condition_variable to shared_mutex: systematically master C++ wait-notify mechanisms and the reader-writer lock pattern"
translation:
  source: documents/vol5-concurrency/ch02-mutex-condition-sync/index.md
  source_hash: 0a9f166aca385f4ef92493ea129f0cb28eaf2386c7744e615b12cf321774c87e
  translated_at: '2026-09-26T06:59:04+00:00'
  engine: anthropic
  token_count: 340
---

# Mutexes, Condition Variables, and Synchronization Primitives

In the previous chapter we sorted out the thread lifecycle and RAII management — how to create threads and how to wait for them to finish safely. But threads alone are not enough: the moment several threads touch the same data, you need a coordination mechanism in place. This chapter focuses on the most fundamental synchronization primitives in the C++ standard library: the mutex for protecting critical sections, the condition_variable for wait-notify coordination between threads, and the reader-writer lock (shared_mutex) for squeezing extra concurrency out of read-heavy, write-light workloads.

We will start with the basic usage of mutexes and RAII locks and pin down the difference between `lock_guard` and `unique_lock`; then dig into the wait semantics of condition_variable and get clear on the pitfalls you simply must master — spurious wakeups, lost wakeups, and friends; finally we will introduce C++17's shared_mutex and analyze where it applies and where its performance boundaries lie. Every step comes with compilable code examples and hands-on exercises.

## In This Chapter

<ChapterNav variant="sub">
  <ChapterLink href="01-mutex-and-raii-guards">mutex and RAII Locks</ChapterLink>
  <ChapterLink href="02-deadlock-and-lock-ordering">Deadlock and Lock Ordering</ChapterLink>
  <ChapterLink href="03-condition-variable">condition_variable and Wait Semantics</ChapterLink>
  <ChapterLink href="04-shared-mutex">Reader-Writer Locks and shared_mutex</ChapterLink>
  <ChapterLink href="05-latch-barrier-semaphore">latch, barrier, and semaphore</ChapterLink>
</ChapterNav>
