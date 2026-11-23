---
title: "Unit 7: Expressing Pausable Work with Coroutines"
description: "From the thread-level tools of the first six units to work that can pause: why suspend and resume are needed, how a minimal coroutine comes together, how co_await joins the wait, wrapping results into a teaching-edition Task, getting coroutines into a queue, plus a selected reading serving an Echo-Server"
translation:
  source: documents/vol5-concurrency/chapter07-coroutines/index.md
  source_hash: 8c0001efdabc6d3ba4d6762697fe0e804d4e7e65996720eeec03b2e7fba31a17
  translated_at: '2026-11-22T14:29:00+00:00'
  engine: glm-5.3
  token_count: 978
---

# Unit 7: Expressing Pausable Work with Coroutines

From Unit 1 through Unit 6, we arranged threads, sharing, waiting, tasks, atomics, and acceptance along the way, with all the tools pressing down on threads. Unit 7 invites coroutines in: work can stop itself; while stopped, it hands the thread back, and when the event arrives it continues. The unit splits into one article per section, starting from why pausable work is needed, then entering the machinery; the fourth article wraps results into a Task, the fifth queues the coroutines up, and a selected reading serves an Echo-Server with the scheduler.

## What This Unit Covers

<ChapterNav variant="sub">
  <ChapterLink href="01-why-suspend-resume">Why Suspend and Resume Are Needed</ChapterLink>
  <ChapterLink href="02-minimal-coroutine">How a Minimal Coroutine Comes Together</ChapterLink>
  <ChapterLink href="03-co-await-and-awaiters">How `co_await` Joins the Wait</ChapterLink>
  <ChapterLink href="04-task-type">Wrapping Results: A Teaching-Edition Task</ChapterLink>
  <ChapterLink href="05-task-scheduler">Getting Coroutines to Queue Up: Singletons, spawn, and Sleeping</ChapterLink>
  <ChapterLink href="06-resumption-routes">Two Resumption Routes: Wiring Task into a Scheduler</ChapterLink>
  <ChapterLink href="07-echo-io-manager">Selected Reading: Wiring IO into the Scheduler — Epoll and IOManager</ChapterLink>
  <ChapterLink href="08-echo-server">Selected Reading: Three Blocks Assembling an Echo-Server</ChapterLink>
</ChapterNav>
