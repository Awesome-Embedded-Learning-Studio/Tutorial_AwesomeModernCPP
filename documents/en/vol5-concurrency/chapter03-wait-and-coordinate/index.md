---
title: "Unit 3: How Tasks Wait and Coordinate"
description: "From condition variables taking over the waiting, to a capacity-bounded task queue and ending paths for waits, then comparing latch, barrier, and semaphores by what the coordination needs"
translation:
  source: documents/vol5-concurrency/chapter03-wait-and-coordinate/index.md
  source_hash: 9942e8606c9c35c28fc91bda2bb57e4e20f78f54a34363e5fd66ef00ed3039f4
  translated_at: '2026-11-22T13:29:16+00:00'
  engine: glm-5.3
  token_count: 894
---

# Unit 3: How Tasks Wait and Coordinate

Unit 2 answered "what happens when several flows of execution meet the same data". This unit swaps in a different question: how does one thread wait for something else to become true, then carry on.

We start the waiting story from the lowest-level tool. Condition variables handle "fall asleep, get woken, recheck the condition on waking", and every later article builds on them: 3.3 uses them to build a capacity-bounded task queue, 3.6 gives every wait an ending path, and only in 3.7 do latch, barrier, and semaphores — coordination tools one level up — get their turn. The unit's content is one article per section, and all seven are in place.

## In This Unit

<ChapterNav variant="sub">
  <ChapterLink href="01-wait-for-a-condition">Waiting for a Condition: From Polling to Condition Variables</ChapterLink>
  <ChapterLink href="02-notify-and-timeout">Whom to Wake, How Long to Wait: Choosing notify and Using Timeouts</ChapterLink>
  <ChapterLink href="03-bounded-queue">A Queue with Capacity: Getting It to Stand First</ChapterLink>
  <ChapterLink href="04-queue-close-and-backpressure">What Happens When a Queue Won't Close: Closing and Backpressure</ChapterLink>
  <ChapterLink href="05-queue-exception-safety">When a Move Throws Halfway, Who Pays: Exception Safety of the Queue</ChapterLink>
  <ChapterLink href="06-ending-a-wait">A Wait Also Needs a Way to End</ChapterLink>
  <ChapterLink href="07-choosing-tools">Choosing Tools by What the Coordination Needs</ChapterLink>
</ChapterNav>
