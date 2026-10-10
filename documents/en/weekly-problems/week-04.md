---
title: "Week 4 · Old-Book Foundations and va_list"
description: "A foundations week built on Kenneth Reek's *Pointers on C*: a recursive gcd warm-up, case conversion with getchar/putchar, reading a variable number of arguments with va_list, a signed char checksum, and a closing problem where you implement a mini sprintf yourself. All five are online-judge problems; difficulty runs from one to three stars."
chapter: 16
order: 5
dateRange: "2026-10-05 ~ 10-11"
difficulty: intermediate
platform: host
weeklyThanks:
  - github: owollz4
    role: This week's problem provider · Contributed the first four problems
  - github: Charliechen114514
    role: Self-composed the closing mini sprintf problem · Problem finalization and judging
tags:
  - host
  - cpp-modern
  - intermediate
translation:
  source: documents/weekly-problems/week-04.md
  source_hash: 9bb290b51ce0a590910becdf6179dfc59eefc06b805d6d3b953cbb290943874d7
  translated_at: '2026-10-10T12:00:00+00:00'
  engine: anthropic
  token_count: 500
---

# Week 4 · Old-Book Foundations and va_list

The first three rounds took you from recursion through language details into move semantics; this one comes back to the foundations of C itself. The first four problems come from owollz4's contributions, and the closer was composed by us around the same book. That book is Kenneth Reek's *Pointers on C*, an old book that lays the foundations of pointers, functions, and bytes down good and solid. All five problems share one thread: how arguments get passed in C, and how bytes get counted. `gcd` works out the greatest common divisor recursively; lower-to-upper walks the IO one character at a time through `getchar` and `putchar`; `max_list` has you read a varying number of arguments with `va_list`; checksum takes the running sum down to the byte level of `signed char`; and the closing mini sprintf pulls those threads into a single problem: this time you don't call `printf`, you implement one yourself.

Difficulty runs from one star to three, and the card order is the suggested tackling order: `gcd` as the warm-up, a pass over character IO, a first hands-on with `va_list`, the checksum down at the byte level, and a closer that moves you from the person calling `va_arg` to the person setting the rules. No format mixing this week. The free-response and bug-hunt problems that showed up in the past two rounds take the week off; all five are online-judge problems, and every one of them asks you to actually type the code in and run it.

<QuizProblem src="code/volumn_codes/weekly-problems/week-04/01-gcd" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-04/02-lower-to-upper" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-04/03-max-list" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-04/04-checksum" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-04/05-mini-sprintf" />

---

**Where the problems come from**: The first four problems this week were contributed by [owollz4](https://github.com/owollz4); the problem statements and reference solutions are his original drafts, and the judge integration was our work. The closer was composed by [us](https://github.com/Charliechen114514) around Chapter 7 of the same book. `gcd`, `max_list`, and the mini sprintf all trace back to the after-chapter programming exercises in Chapter 7 of *Pointers on C*; in the chapter7 folder of the public solutions repository DragScorpio/Pointers-On-C-Solutions you can find the matching implementations, gcd.c, maxlist.c, and implementPrintf. `gcd` also happens to be a classic that has been making the rounds forever, so this one problem carries two pedigrees. Lower-to-upper and checksum were both flagged by the contributor as coming from the same book. One honest note from when we landed the problems: the original checksum draft omitted the "copy the input to the output character by character" that the problem statement asks for; the `putchar(ch)` line in the judge version was added by us. The closer's footing is in Chapter 7 too — that chapter's exercises already include "implement a printf yourself"; we trimmed it to the four conversions `%d`, `%s`, `%c`, and `%%`, and retooled the judging into an online buffer comparison. Widening the lens for a moment, C++ has since replaced both jobs: formatting went to [`std::format`](../../vol3-standard-library/strings/52-format.md) and [`std::print`](../../vol3-standard-library/strings/53-print.md), and variable arguments went to [variadic templates](../../cpp-reference/templates/02-variadic-templates.md). Every judge case was verified by local testing on two compilers (GCC 16 / Clang 22).
