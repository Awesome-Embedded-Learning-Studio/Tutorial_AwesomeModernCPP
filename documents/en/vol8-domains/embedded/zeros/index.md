---
title: "Hand-Rolling ZerOS: From Bare Metal to an RTOS, One Commit at a Time"
description: "Take a real, existing C++23 bare-metal RTOS — one whose every commit you can check out — and follow it from an empty repository all the way to the v0.1.0 release; the articles anchor to the repository's actual 26 commits"
platform: stm32f1
tags:
  - stm32f1
  - advanced
  - 嵌入式
  - cpp-modern
difficulty: advanced
cpp_standard: [23]
translation:
  source: documents/vol8-domains/embedded/zeros/index.md
  source_hash: 981e5c4af27e39fea9f6411077a392ce5a43d9d4a0dec632c26c79796c58b0d6
  translated_at: '2026-11-22T16:46:41+00:00'
  engine: glm-5.3-refresh
  token_count: 2298
---

# Hand-Rolling ZerOS: From Bare Metal to an RTOS, One Commit at a Time

Coming out of the earlier F103 tutorial, you can already write bare-metal applications fluently on top of libestdx, and ZerOS sits in `third_party` as a ready-made dependency to reach for. What we are going to do is implement its internals with our own hands. ZerOS is a C++23 bare-metal RTOS that keeps the heap, RTTI, exceptions, and the C ABI outside the door. And the real development board is simply the Blue Pill (STM32F103C8T6) at your side.

We do not maintain a separate teaching edition of the code; we teach along the repository's actual 26 commits. Each article anchors to one or a few of those states: you reproduce the code by hand in your own directory, and the files you end up writing match the reference answer character for character. The repository steps down to being the reference answer — whenever you get stuck or want to compare a diff, check out the corresponding state and read it. The same kernel code we verify in four environments — host desktop unit tests, the Renode simulator, QEMU mps2-an385, and the real development board. Each article is accepted in at least one of them, and the Renode serial console is the primary acceptance tool along the whole way.

Our route advances in commit order: we first get the repository running (project setup, heapless memory, time and critical sections), then come the scheduler and the first context switch, then the leg on synchronization primitives and kernel services (semaphores, mutexes, queues, timers, event groups), and finally performance measurements on real silicon, a second board, and the v0.1.0 release. We publish each station as soon as it is written; a station not yet written gets no link.

## How to Follow Along

```shell
git clone https://github.com/Awesome-Embedded-Learning-Studio/ZerOS.git
cd ZerOS
git submodule update --init
# 每篇开头会给这一篇的 commit,例如:
# git checkout fe5a0e8
```

A few things we had better make clear up front, so you don't start suspecting midway that your clone is broken. A few intermediate commits do not compile on host — interface first, implementation later is the real rhythm — and the corresponding articles spell out exactly which commit to check out for the run to go green; there is even an exercise left for you, filling in two empty functions. The performance station, second to last, needs real silicon (Blue Pill + ST-Link/OpenOCD); still, the three environments — host, Renode, and QEMU — are enough for you to follow the entire track, and for numbers taken on real silicon, we will flag in the text that they were verified on a real chip only.

If you have been through the getting-started stations of the earlier F103 tutorial, entering our hand-rolling track will be the smoothest path: the toolchain, Renode, and cross-builds are all old friends of yours, and the relevant articles cover only what ZerOS does differently. Coming in without that background works too — keep the getting-started stations as a reference and consult them as you go.

## Chapter Navigation

<ChapterNav>
  <ChapterLink num="0" href="00-why-rtos/">Why an RTOS: From the Superloop to ZerOS</ChapterLink>
  <ChapterLink num="1" href="01-heapless-memory/">A World Without a Heap: Handing Out Memory Correctly First</ChapterLink>
  <ChapterLink num="2" href="02-time-kernel/">Give the Kernel a Second: Time and Critical Sections</ChapterLink>
  <ChapterLink num="3" href="03-first-scheduler/">Who Owns the CPU: From Ready Bitmaps to Interrupt Wakeups</ChapterLink>
  <ChapterLink num="4" href="04-sync-and-inheritance/">Correct Waiting: Counting Semaphores and Priority Inheritance</ChapterLink>
  <ChapterLink num="5" href="05-context-economics/">Whose Context: Last Words, Bottom Halves, and Timers</ChapterLink>
  <ChapterLink num="6" href="06-fairness-and-closure/">Fairness and Convergence: Time Slices, Event Groups, and Direct Notifications</ChapterLink>
  <ChapterLink num="7" href="07-api-and-ci/">Tidy-up and Gatekeeping: Demo Registry and CI</ChapterLink>
  <ChapterLink num="8" href="08-performance-on-silicon/">True Silicon Stopwatch: How Much Does a Context Switch Cost?</ChapterLink>
  <ChapterLink num="9" href="09-second-board-and-release/">The Second Board and Departure: QEMU, Tickless, v0.1.0</ChapterLink>
</ChapterNav>

All ten stations are now online — we wrote our way from the superloop all the way to hanging out the release shingle. You can follow from the very beginning, or pick whichever station interests you and start reading there.
