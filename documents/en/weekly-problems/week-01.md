---
title: "Week 1 · A Recursion Triple"
description: "Counting coins, digit distance, and hailstone sequences. Three recursion drills: break big problems apart, turn ideas into code."
chapter: 16
order: 1
dateRange: "2026-09-14 ~ 09-20"
difficulty: intermediate
platform: host
weeklyThanks:
  - github: owollz4
    role: This week's problem provider · Lead creative originator of the column
tags:
  - host
  - cpp-modern
  - intermediate
translation:
  source: documents/weekly-problems/week-01.md
  source_hash: df420048d60bd6d4eb0f28eea7b07ce2ec5d9b84c2a07a85b62e6fc69c75285d
  translated_at: '2026-09-25T09:24:28+00:00'
  engine: anthropic
  token_count: 400
---

# Week 1 · A Recursion Triple

This round is all about recursion!

The three problems really share one theme — the illustrious **recursion**.

Even though engineering practice treats recursion with caution, it genuinely is an important way of thinking about problems. The three problems this week come from CS61A.

The first two train the instinct for "breaking a big problem into subproblems" — counting coins splits by denomination, digit distance splits by digit.

For the third, we pack recursion into C++23's `std::generator` and feel the one-two punch of "recursion + coroutines".

<QuizProblem src="code/volumn_codes/weekly-problems/week-01/01-count-coins" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-01/02-digit-distance" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-01/03-hailstone" />

---

**Where the problems come from**: All three problems this week are drawn from UC Berkeley's CS61A homework and lab exercises (Count Coins / Digit Distance / Hailstone). They were proposed and hand-picked by the great [owollz4](https://github.com/owollz4), and we gave them a light C++-flavored adaptation — the same problem, tackled the Python-course way and the Modern C++ way, reads delightfully different when you compare the two side by side.
