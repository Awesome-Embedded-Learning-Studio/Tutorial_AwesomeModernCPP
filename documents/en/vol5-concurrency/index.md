---
title: 'Volume 5: Concurrent Programming'
description: From thread primitives to coroutine asynchrony
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
translation:
  source: documents/vol5-concurrency/index.md
  source_hash: a4df0d2cc1934cde1cb38c54f74f0b1f004136a02ac3509c1bf39c5a3e892b3c
  translated_at: '2026-09-29T14:40:17+00:00'
  engine: anthropic
  token_count: 3600
---

# Volume 5: Concurrent Programming

From thread primitives to coroutine asynchrony, from locks to lock-free, from synchronization to tasks — Volume 5 helps you build complete judgment for concurrency. Our principle: **correctness first, then performance; locks first, then lock-free; synchronization first, then tasks**.

## Chapter Navigation

<ChapterNav variant="sub">
  <ChapterLink href="ch00-concurrency-fundamentals">Chapter 0 · Concurrency Worldview and the First Tool</ChapterLink>
  <ChapterLink href="ch01-thread-lifecycle-raii">Chapter 1 · Threads: The First Execution Flow</ChapterLink>
  <ChapterLink href="ch02-mutex-condition-sync">Chapter 2 · Sharing and Synchronization</ChapterLink>
  <ChapterLink href="ch03-atomic-memory-model">Chapter 3 · Atomics and the Memory Model</ChapterLink>
  <ChapterLink href="ch04-concurrent-data-structures">Chapter 4 · Lock-Free and Measured Performance</ChapterLink>
  <ChapterLink href="ch05-future-task-threadpool">Chapter 5 · From Threads to Tasks</ChapterLink>
  <ChapterLink href="ch06-async-io-coroutine">Chapter 6 · Coroutines</ChapterLink>
  <ChapterLink href="ch07-actor-channel">Chapter 7 · Composition and the Finale</ChapterLink>
  <ChapterLink href="bridge-distributed">Interlude · The Distributed Bridge</ChapterLink>
  <ChapterLink href="exercises">Hands-On Labs</ChapterLink>
</ChapterNav>

## How This Volume Unfolds

Eight chapters of main text, one interlude, and a set of labs — that adds up to the whole estate of Volume 5. We lay out what each chapter is for below, and every link opens the door of its chapter:

| Chapter                                                              | What This Chapter Covers                                                                                                                                                                             |
| -------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [Chapter 0 · Concurrency Worldview and the First Tool](./ch00-concurrency-fundamentals/) | Why concurrency, and the first lesson on data races and TSan — all in this chapter                                                                                          |
| [Chapter 1 · Threads: The First Execution Flow](./ch01-thread-lifecycle-raii/)        | The lifecycle and RAII of `std::thread`, and the canonical source on cancellation with jthread and stop_token                                                                                       |
| [Chapter 2 · Sharing and Synchronization](./ch02-mutex-condition-sync/)                 | mutex, deadlock diagnosis, condition_variable — the canonical source on the blocking queue lives here too                                                                                           |
| [Chapter 3 · Atomics and the Memory Model](./ch03-atomic-memory-model/)              | The cache foundation, the formal definition of happens-before, and the pairing shapes of memory orders                                                                                              |
| [Chapter 4 · Lock-Free and Measured Performance](./ch04-concurrent-data-structures/)       | The price tag on OS threads plus a first taste of perf stat, perf's main classroom: lock-free stacks, memory reclamation, false sharing (each thread writing its own data, yet all crammed onto the same cache line) and SPSC/MPSC queues (single-producer single-consumer / multi-producer single-consumer) |
| [Chapter 5 · From Threads to Tasks](./ch05-future-task-threadpool/)             | async, future, promise, and the canonical source on a production-grade thread pool                                                                                                                  |
| [Chapter 6 · Coroutines](./ch06-async-io-coroutine/)                         | `task<T>`, coroutine cancellation, the event loop, and timers                                                                                                                                        |
| [Chapter 7 · Composition and the Finale](./ch07-actor-channel/)                        | Actor and Channel/CSP, a walkthrough of assembling the Echo service, and the volume closing its doors                                                                                                |
| [Interlude · The Distributed Bridge](./bridge-distributed/)                             | A thought experiment on consistency and Raft, a signpost on the road to Volume 8                                                                                                                     |
| [Hands-On Labs](./exercises/)                                             | Lab 00 through Lab 07, from your first race to the Capstone                                                                                                                                          |

A table is cold, so let's warm up the three threads buried inside it. **How the components flow**: the blocking queue built in Chapter 2 becomes the task queue of the thread pool in Chapter 5, and the thread pool from Chapter 5 together with `task<T>` from Chapter 6 get assembled in Chapter 7 into finished Actor and Channel artifacts. Whenever you lose your way in some chapter, come back to this page, see where the components have flowed, and you'll find your position again.

**The tools also keep a class schedule**: TSan's first lesson is placed in Chapter 0, gdb's three signature moves in the deadlock article of Chapter 2, and perf's big set piece is held back for Chapter 4. Each tool handles a particular class of problem; when we reach a chapter, we cover the tools that make their entrance in that chapter. The tools always follow the problem.

**The lab numbers line up one-to-one with the chapter numbers**: Lab 00 is the toolchain and your first race, Lab 04 corresponds to measuring false sharing with perf, Lab 05 builds a production-grade thread pool, and Lab 07 is the closing Capstone; the remaining Lab 01, 02, 03, and 06 map in order to the thread lifecycle, synchronization and the blocking queue, the memory-order laboratory, and the coroutine scheduler with cancellation. The whole set of labs runs about 53 to 75 hours; if you're short on time, the minimum completion set is Lab 00, 02, 05, and 07, roughly 28 to 41 hours. The entry point for all of them is on the [hands-on labs](./exercises/) page.

The volume can also be read as **three acts**. The first two chapters are the foundation, where you build your feel for execution flows and synchronization; once you're through them, concurrency is no longer black magic to you. Chapters 3 and 4 are the two deep-water chapters, covering the memory model and lock-free work — the hardest bones in the volume, where slow and careful work is exactly what it takes. The last three chapters move up to the upper layers and assembly, with tasks, coroutines, and Actors taking the lead, wrapping the abilities built on the foundation into handy tools. The interlude is the bridge toward Volume 8, connecting the single-machine story to the distributed world.

What this volume **does not teach** is stated just as clearly: inter-process topics are not in the book, distributed consistency is only brushed against in the bridging interlude, and GPU and SIMD vector parallelism is a job for someone else. Once the circle of what isn't taught is drawn, the part that is taught can stand firm.

## Which Path to Take In

The straight-through reading follows the chapter numbers all the way down, doing the labs along the way — that's the steadiest route. Those who pick and choose come carrying a problem in hand: suppose you're chasing down an intermittent crash, then head straight for [the data race and TSan article of Chapter 0](./ch00-concurrency-fundamentals/02-data-race-and-tsan.md), patch in [the deadlock article of Chapter 2](./ch02-mutex-condition-sync/03-deadlock-and-gdb.md), and then loop back to fill in the foundation. Teachers leading a group can use this page as a tour: schedule one discussion for each act, and let the homework land on Lab 00 and Lab 02. Whichever route you take, we welcome it — the map is always waiting for you right here.

For readers coming from other languages, here's a heads-up too (if you've never touched these languages, skipping this paragraph changes nothing): C++ has no built-in runtime nanny — thread scheduling, synchronization, and lifetimes are all placed squarely in your own hands. Java's memory model, Go's scheduler, Rust's ownership checks — every language has its own patron saint, and C++'s patron saint is the very chapters you read yourself.

## Reading Conventions for This Volume

We have three conventions with you; knowing them will make your reading go much more smoothly. The first is about numbers: the performance figures in the text come in two grades. Pure mathematical derivations hand you the number directly, while measured numbers always come with a reproducible command. Any spot that hasn't been measured yet gets a placeholder line of `experiment pending`, and until that placeholder is filled in, that sentence can't be taken for a real figure.

Another one is about code: complete, compilable code belongs to the `code/volumn_codes/vol5/` code repository, and the text keeps only the key fragments and the explanation. If you want to run it, run it from the repo.

The last one is about terminology: when a term shows up for the first time, we either write a quick sketch of it on the spot or give you a forward pointer telling you which article the formal definition lives in. When you can't find it by flipping back and don't want spoilers by flipping forward, one glance at the map on this page will do.

One more word on the weight of the labs: whether or not you do the exercises at the end of each article makes a big difference to what stays in your hands after reading. Concurrency is a hands-on craft; if you only look and never touch, three months later all you'll have left are the nouns. Even if your time is desperately tight, we suggest doing the first exercise of every article.

## What You Gain from Each Chapter

When you finish a chapter, check it against one row. If the row leaves you feeling unsure, we suggest you go through that chapter again.

| After Finishing | You Should Be Able To                                                    |
| --------------- | ------------------------------------------------------------------------ |
| Chapter 0       | Recognize a data race and run TSan                                                            |
| Chapter 1       | Start threads, pass arguments, and wrap up safely, and issue stop requests with jthread |
| Chapter 2       | Write correct shared structures with locks and condition variables        |
| Chapter 3       | Read memory orders and judge whether a cross-thread read/write is safe    |
| Chapter 4       | Measure the order of magnitude of thread overhead, locate bottlenecks with perf, and weigh the costs and payoffs of lock-free |
| Chapter 5       | Replace raw threads with tasks and thread pools                           |
| Chapter 6       | Organize highly concurrent I/O with coroutines                            |
| Chapter 7       | Assemble the whole kit into a service that can wind down with dignity     |
