---
title: "Fairness and Convergence: Time Slices, Event Groups, and Direct Notifications"
description: "Two overdue debts from the service layer are settled: the scheduler adds peer-level time slice rotation (slice debt not inherited), the synchronization family converges on event group ANY/AND masks and single-slot direct notifications (overwriting with the latest value), full-ring backpressure makes the sender sleep and wait for an empty slot; broadcast wake-up and wake-up each recheck the truth, solidifying into a family invariant spanning seven primitives."
chapter: 6
order: 0
tags:
  - stm32f1
  - advanced
  - 嵌入式
  - cpp-modern
difficulty: advanced
platform: stm32f1
cpp_standard: [23]
translation:
  source: documents/vol8-domains/embedded/zeros/06-fairness-and-closure/index.md
  source_hash: 06d7c4e426d95a7e5cd21bb7c077fc7918b606517848a72f2191527324036165
  translated_at: '2026-11-22T13:52:43+00:00'
  engine: qwen3:14b
  token_count: 974
---
# Fairness and Convergence: Time Slices, Event Groups, and Direct Notifications

> We publish one article at a time, and both articles in this chapter are now online.

## Overview

With the three-piece set of the service layer complete, our two overdue debts have also come due. One is on the scheduler: tasks that are awake and never sleep are starving each other, so we need to add fairness. When the count is full, we switch to another task, and the slice debt is not inherited. When it comes back, how many slices remain is purely a matter of luck. The other debt is on the synchronization family: semaphores count occurrences, mutexes recognize their owners, but multi-party waiting and fastest paths are still empty. The former requires two keys to open the door, and the latter is a path that directly passes the value to the task without providing an event structure. We use the ANY/AND masks of event groups and the single-slot direct notification embedded in the task control block to complete the family. We also use full-ring backpressure to complete the path where the sender sleeps and waits for an empty slot. In conclusion, we refine "broadcast wake-up and wake-up each recheck the truth" into a family invariant spanning seven primitives. Although it is not written into any header file, every implementation adheres to it.

## Articles on Site

<ChapterNav variant="sub">
  <ChapterLink href="01-time-slice-round-robin">Switch When Counting to Ten: Peer-Level Time Slices and Non-Inherited Slice Debt</ChapterLink>
  <ChapterLink href="02-event-group-and-notify">Wake Everyone Up, Each Rechecks the Truth: Event Groups, Direct Notifications, and Full-Ring Backpressure</ChapterLink>
</ChapterNav>

The kernel's capabilities are now complete, and it's time to tidy up the home: the example has already grown to over 300 lines, with eight demos crammed into one room. The next stop is to split it into one scene per room, set up several CI checks, and let the verification for each stop run automatically.
