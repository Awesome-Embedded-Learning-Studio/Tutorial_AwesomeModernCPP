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
  source_hash: 0b02cd957e0165b9b2d5521b95cd5e292f08b50f7b82266395fe0ba2abc9de58
  translated_at: '2026-09-25T08:00:14+00:00'
  engine: anthropic
  token_count: 700
---

# Hand-Rolling ZerOS: From Bare Metal to an RTOS, One Commit at a Time

Coming out of the earlier F103 tutorial, you can already write bare-metal applications fluently on top of libestdx, and ZerOS sits in `third_party` as a ready-made dependency. What this track sets out to do is implement the internals of that dependency with your own hands: ZerOS is a C++23 bare-metal RTOS — no heap, no RTTI, no exceptions, no C ABI — and the real machine is simply the Blue Pill (STM32F103C8T6) sitting next to you.

The tutorial does not maintain a separate teaching edition of the code; it teaches along the repository's actual 26 commits. Each article anchors to one or a few of those states: you reproduce the code by hand in your own directory, and the files you end up writing match the reference answer character for character. The repository steps down to being the reference answer — whenever you get stuck or want to compare a diff, check out the corresponding state and read it. The same kernel code is verified in four environments — host desktop unit tests, the Renode simulator, QEMU mps2-an385, and real hardware. Each article is accepted in at least one of them, with the Renode serial console as the primary acceptance tool along the whole way.

The route advances in commit order: first we get the repository running (project setup, heapless memory, time and critical sections), then come the scheduler and the first context switch, then synchronization primitives and kernel services (semaphores, mutexes, queues, timers, event groups), and finally real-chip performance measurements, a second board, and the v0.1.0 release. Stations go live one at a time as they are written; a station not yet written gets no link.

## How to Follow Along

```shell
git clone https://github.com/Charliechen114514/ZerOS.git
cd ZerOS
git submodule update --init
# Each article opens with the commit it anchors to, for example:
# git checkout fe5a0e8
```

A few things need to be made clear up front, so you don't start suspecting midway that your clone is broken. First, a few intermediate commits do not compile on host — interface first, implementation later is how a real project moves; the corresponding articles spell out exactly which commit to check out for the run to go green, and there is even an exercise that has you fill in two empty functions. Second, the performance station needs real silicon (Blue Pill + ST-Link/OpenOCD); without a board, the three environments — host, Renode, and QEMU — are enough to follow the entire track, and any numbers tied to real hardware will be flagged in the text as verified on a real chip only.

The smoothest entry is from the getting-started stations of the earlier F103 tutorial: the toolchain, Renode, and cross-builds are all old friends by then, so the relevant articles cover only what ZerOS does differently. Coming in without that background works too — keep the getting-started stations as a reference and consult them as you go.

## Chapter Navigation

<ChapterNav>
  <ChapterLink num="0" href="00-why-rtos/">Why an RTOS: From the Superloop to ZerOS</ChapterLink>
  <ChapterLink num="1" href="01-heapless-memory/">A World Without a Heap: Handing Out Memory Correctly First</ChapterLink>
</ChapterNav>

The later stations (time and critical sections, the scheduler, synchronization primitives, kernel services, demo and CI, on-hardware performance, the second board and the release) will come online progressively; you can start from what is already published.
