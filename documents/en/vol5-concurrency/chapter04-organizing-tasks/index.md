---
title: "Unit 4: From Managing Threads to Organizing Tasks"
description: "From launching with std::async and taking results, to launch policies and destructor waits, then passing results yourself and wrapping dispatchable tasks"
translation:
  source: documents/vol5-concurrency/chapter04-organizing-tasks/index.md
  source_hash: b52a7551837d2b55d2c557aa852015bfa568d68abdfd8a6961e2e9197f086b10
  translated_at: '2026-11-22T13:29:16+00:00'
  engine: glm-5.3
  token_count: 786
---

# Unit 4: From Managing Threads to Organizing Tasks

Over the first three units we held the threads in our own hands: starting them, waiting for them, stopping them, with data sharing and wait endings all our own arrangements. Unit 4 raises the viewpoint one level — the unit of management changes from thread to task, and who executes, how results come back, and how failure travels are all expressed by task objects.

`std::async` is the unit's entrance: it starts the thread, carries the result, and passes the exception for us, and we merely hold the future and wait. When a task starts work is decided by the launch policy — the easiest thing to misread in the unit, worth an article of its own. The self-supplying promise arrives in the third article, opening the proper road for passing failure upward. What remains is the task-wrapping packaged_task. The thread pool goes into the cookbook electives, out of the main text. The unit's content is one article per section; the settled articles are listed below.

## In This Unit

<ChapterNav variant="sub">
  <ChapterLink href="01-launch-with-async">Running a Computation with std::async</ChapterLink>
  <ChapterLink href="02-launch-policies">When Does the Task Run: Launch Policies and Waiting Behavior</ChapterLink>
  <ChapterLink href="03-promise-and-future">Passing Results Yourself: `std::promise` and `std::future`</ChapterLink>
  <ChapterLink href="04-packaged-task">Wrapping a Computation into a Storable, Dispatchable Task</ChapterLink>
</ChapterNav>
