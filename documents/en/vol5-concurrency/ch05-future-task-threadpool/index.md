---
title: Futures, Tasks, and Thread Pools
description: From `std::async` to `promise`/`packaged_task`, building flexible asynchronous
  task channels and thread pool infrastructure
translation:
  source: documents/vol5-concurrency/ch05-future-task-threadpool/index.md
  source_hash: 9ab6af6303383bc38e7c506c7c5890021e60b1d1b148e42b988310632eb30b75
  translated_at: '2026-05-20T04:43:25.143676+00:00'
  engine: anthropic
  token_count: 220
---
# Futures, Tasks, and Thread Pools

In previous chapters, we worked extensively with low-level primitives: `thread`, `mutex`, `atomic`, and `condition variable`. These give us precise control, but they also impose a significant manual management burden—we have to design synchronization mechanisms ourselves, pass results manually, and handle errors on our own. The C++ standard library provides a set of higher-level asynchronous abstractions to simplify this work: `future` is a result container, `promise` is the writing end for a value, `packaged_task` is a task wrapper, and `async` is the most convenient way to launch a task. Together, they form the infrastructure for thread pools and task queues.

In this chapter, we start with `std::async` and `std::future` to understand the launch strategies for asynchronous tasks and the mechanisms for retrieving results. We then dive into `std::promise` and `std::packaged_task` to learn how to manually control value setting and task execution. Finally, we discuss `std::shared_future` and thread pool design patterns, tying together all the components we have learned so far.

## Chapter Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-std-async-and-future">std::async 与 future</ChapterLink>
  <ChapterLink href="02-promise-and-packaged-task">promise 与 packaged_task</ChapterLink>
  <ChapterLink href="03-jthread-and-stop-token">jthread 与停止令牌</ChapterLink>
  <ChapterLink href="04-thread-pool">线程池设计</ChapterLink>
</ChapterNav>
