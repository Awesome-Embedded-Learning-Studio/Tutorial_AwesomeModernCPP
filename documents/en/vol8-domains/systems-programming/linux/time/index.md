---
title: "Linux Time and Timers"
sidebar_order: 50
description: "Clock sources and the POSIX time API, C++20 chrono calendars and time zones, and a panorama of four generations of timer APIs with a drift comparison"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
  - 系统编程
  - POSIX
translation:
  source: documents/vol8-domains/systems-programming/linux/time/index.md
  source_hash: a07894fcd1f094ce27d2cfb4766066960f6f7ccfaca021ee510679e30bd367dc
  translated_at: '2026-11-22T15:31:37+00:00'
  engine: glm-5.3
  token_count: 1882
---

# Linux Time and Timers

This time chapter starts from the question "how many clocks live inside the machine, and where do they differ": the first article measures all nine clockids one by one, keeping resolution and precision apart, and adds real measurements for the vDSO fast path. The second moves into the standard library to see how std::chrono's clocks land on OS clocks and what the leap-second and time-zone pitfalls look like in the type system. The third lays out the four generations of timer APIs and closes with a drift comparison of three ways to write a periodic task. The common toolkit used across the series follows the definitions from the two [Thinking Foundations](../../thinking/) articles.

<ChapterNav variant="sub">
  <ChapterLink num="1" href="01-clock-sources" desc="How many rulers hide behind the CLOCK_MONOTONIC family of constants, where they differ, and how to choose: a census of nine clockids (TAI−UTC=37s of accumulated leap seconds, CLK_TCK=100 versus kernel HZ=250 as two separate scales, COARSE resolution of 4ms), resolution ≠ precision (getres reports 1ns while back-to-back reads show a 20ns median, COARSE yields only 3 distinct values across a million reads, read cost 16.2ns versus 2.0ns), continuity measured (2 million reads with zero regressions, RAW−MONOTONIC frozen at −106.4ms of historical frequency adjustments), vDSO delivering on the overview's ticket-free promise (16.9ns versus 168.9ns for a real syscall — ten times; strace showing zero syscalls versus two million calls), CPU clocks reconciled across three scales (nanosecond 298.0 / getrusage 298.1 / tick 290 granularity gaps), and the wall-clock presentation chain (TZ swaps rules, not time) paving the way for the chrono article">Clock Sources and the POSIX Time API</ChapterLink>
  <ChapterLink num="2" href="02-chrono-calendar" desc="How std::chrono's clocks and calendar land on the OS, and what the leap-second and time-zone pitfalls look like in the type system: clock placement checked side by side (system/steady measured back to back, the three offsets utc−system=+27s / TAI−UTC=+37s / GPS−UTC=+18s — the same leap-second data as the kernel's 37s, read two ways), the leap-second timeline (2016-12-31's 23:59:60 really sitting on the axis, get_leap_second_info, a 27-entry table), hard time-zone cases (New York ambiguity and nonexistent instants throwing exceptions, China's 1988 daylight saving fully queryable in tzdata), tzdb landing on the OS (strace proving openat on tzdata.zi, first lookup 1.751ms versus a later same-reference call), ok() validation on calendar types, and four-column instrumentation wrapping up the division of labor among measurement, persistence, auditing, and presentation">C++20 std::chrono in Depth</ChapterLink>
  <ChapterLink num="3" href="03-timer-family" desc="How to choose among the four generations of timer APIs, and why periodic tasks must not be fudged with sleep: alarm's whole-second granularity and its remainder rounded up (the three-point discrimination of 1.70s reporting 2 / 0.40s reporting 1), setitimer's three accounting modes (REAL 200 / VIRTUAL 101 / PROF 96 ticks) and the Linux-private misfeature of passing NULL as new_value to cancel, timer_create's four notification forms (SIGEV_THREAD proven to spawn a fresh thread with a new tid each time, THREAD_ID targeting with zero disturbance) together with getoverrun's expiration merging, the survival matrix (all three signal forms zeroed by fork, alarm and itimer surviving exec per both documentation and measurement, dispositions reset, timerfd living through fork+exec), and the headline drift comparison: a relative sleep loop drifting by about 78ms versus absolute expiry below 0.08ms versus timerfd at 0.03ms, plus the right way to write a periodic scheduler">A Panorama of Timers</ChapterLink>
</ChapterNav>

With these three, the time chapter is complete. The next stop is terminals, following the learning route laid out in the [overview](../../00-overview.md).
