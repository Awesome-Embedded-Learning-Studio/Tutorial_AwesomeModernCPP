---
title: "Futures, Tasks, and Thread Pools"
description: "From std::async to promise/packaged_task: building flexible asynchronous task channels and thread pool infrastructure"
translation:
  source: documents/vol5-concurrency/ch05-future-task-threadpool/index.md
  source_hash: 9ab6af6303383bc38e7c506c7c5890021e60b1d1b148e42b988310632eb30b75
  translated_at: '2026-09-26T08:29:06+00:00'
  engine: anthropic
  token_count: 320
---

# Futures, Tasks, and Thread Pools

Over the previous chapters we have been living with the low-level primitives: `thread`, `mutex`, `atomic`, condition variables. They give us precise control, but they also bring a heavy manual management burden — you have to design the synchronization mechanism yourself, pass results around yourself, and handle errors yourself. The C++ standard library offers a set of higher-level asynchronous abstractions to lighten that load: `future` is the result container, `promise` is the writing end of a value, `packaged_task` is the task wrapper, and `async` is the most convenient way to launch. Combined, they form the infrastructure of thread pools and task queues.

In this chapter we start with `std::async` and `std::future`, to understand the launch strategies for asynchronous tasks and the mechanism for retrieving results; then we dig into `std::promise` and `std::packaged_task`, learning to control value setting and task execution by hand; finally we discuss `std::shared_future` and thread pool design patterns, stringing together all the components we have picked up along the way.

## In This Chapter

<ChapterNav variant="sub">
  <ChapterLink href="01-std-async-and-future">std::async and future</ChapterLink>
  <ChapterLink href="02-promise-and-packaged-task">promise and packaged_task</ChapterLink>
  <ChapterLink href="03-jthread-and-stop-token">jthread and Stop Tokens</ChapterLink>
  <ChapterLink href="04-thread-pool">Thread Pool Design</ChapterLink>
</ChapterNav>
