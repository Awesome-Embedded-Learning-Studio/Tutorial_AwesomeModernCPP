---
title: "Week 2 · Language Details and Old-School Classics"
description: "Two spoken-answer problems — Chinese identifiers and the comma expression — plus three judge problems: copy_n, Eight Queens, and the Longest Plateau. Old-book classics, retooled for online judging."
chapter: 16
order: 3
dateRange: "2026-09-21 ~ 09-27"
difficulty: intermediate
platform: host
weeklyThanks:
  - github: owollz4
    role: This week's problem provider · Debut of the free-response and thought problems
tags:
  - host
  - cpp-modern
  - intermediate
translation:
  source: documents/weekly-problems/week-02.md
  source_hash: 40a2679dea6c26d02dc69ec635904fa65d9f5fe267851d02129480291e74f78f
  translated_at: '2026-09-25T09:33:48+00:00'
  engine: anthropic
  token_count: 400
---

# Week 2 · Language Details and Old-School Classics

This round the format lineup expands. Week 1 was all judge-submitted code; this week, "free-response" and "thought" problems join the field: two of them you solve without ever opening an editor — lay out your reasoning clearly and get the number right, and you pass. Thinking it through before you put pen to paper is exactly what these two formats are meant to train.

The first two probe language and engineering details: one asks why Chinese identifiers get no love in engineering — the compiler nods along just fine, so why does nobody write that way in real projects; the other works out what `x = (a++, b++)` actually evaluates to — with a side question that the problem's author himself got wrong at first. The last three switch back to writing code, all drawn from old books and classic problems: `copy_n` is picked from *Pointers on C*, "Longest Plateau" is the same breed of textbook exercise, and Eight Queens is the beginner's gate you can't get around when learning backtracking. Difficulty runs from one star to three, and the order on the cards is the suggested tackling order — work top to bottom and the problems get steadily meatier.

<QuizProblem src="code/volumn_codes/weekly-problems/week-02/01-chinese-identifiers" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-02/02-comma-expression" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-02/03-copy-n" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-02/04-eight-queens" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-02/05-longest-plateau" />

---

**Where the problems come from**: "Chinese identifiers" comes from [owollz4](https://github.com/owollz4)'s idle musings; "comma expression" is the original question a friend of his tossed out; `copy_n` is an after-chapter exercise from Kenneth Reek's *Pointers on C*; "Eight Queens" and "Longest Plateau" are classics that have been making the rounds forever. All five problems this week were curated and contributed by owollz4; we shaped them into the online-judge and spoken-answer formats, and every judge case was verified by local testing.
