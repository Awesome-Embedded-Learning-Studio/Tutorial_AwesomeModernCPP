---
title: "Week 6 · Types Take Charge"
description: "Theme: Types Take Charge: enum class disables implicit conversion, signed vs unsigned collision, integer promotion and left shift produce two answers, line-by-line type checking. The second half of the week focuses on optional and variant managing 'existence' and 'identity at this moment'. On Sunday, optional<bool> concludes the week, with all seven problems available for online judging."
chapter: 16
order: 7
dateRange: "2026-11-02 ~ 11-08"
difficulty: intermediate
platform: host
weeklyThanks:
  - github: Charliechen114514
    role: Self-proposed seven problems for this week · Problem and judging data tested in practice
tags:
  - host
  - cpp-modern
  - intermediate
  - 类型安全
translation:
  source: documents/weekly-problems/week-06.md
  source_hash: 560bde5b7c1aafabf939d4d16a275c66f6585844a46f5d1a6592145b4f3d2f43
  translated_at: '2026-11-22T14:18:30+00:00'
  engine: qwen3:14b
  token_count: 1551
---
# Week 6 · Types Take Charge

The pace is the same as last week: one problem per day from Monday to Sunday. Monday starts with an easy problem to warm up, and the overall difficulty increases. On Friday, a special one-star problem is provided for you to take a break. Sunday concludes with a three-star problem. The problems are not large, with each focusing on a specific concept. Once you complete the problem of the day, you can consider that concept fully understood — our advice is not to accumulate problems, but to complete the daily problem on the same day.

This week's theme is "Types Take Charge," following a thread from "how numbers convert" to "how values are stored." The first half of the week focuses on conversion: who can quietly convert to integers between traditional enum and enum class, who gets converted first when a negative number collides with an unsigned number, and why shifting one byte left by one bit produces two answers. On Thursday, the idea of "checking type first, then value" is applied to line-by-line type checking. The second half of the week looks at value storage: optional manages "existence," variant manages "who is it at this moment," and on Sunday, optional<bool> brings these two layers together. With just three lines of code, the answer is counterintuitive — we'll let you answer it first before revealing the details. The difficulty distribution is 1-2-2-2-1-2-3, with three two-star problems on Tuesday through Thursday.

<QuizProblem src="code/volumn_codes/weekly-problems/week-06/01-which-fails" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-06/02-unsigned-compare" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-06/03-promote-shift" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-06/04-int-or-text" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-06/05-first-even" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-06/06-variant-ask" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-06/07-optional-bool" />

---

**Problem Sources**: All seven problems in this week are self-proposed by [Charliechen](https://github.com/Charliechen114514), written as a series around the type system: the first three problems focus on how numbers convert (enum class disables implicit conversion, signed vs unsigned arithmetic conversion, integer promotion and narrow type storage), the fourth problem applies the idea of "checking type first, then value" to line-by-line type checking, and the last three problems focus on how values are stored. All judging data (assertions and input/output comparisons) have been tested locally with GCC 16.2.1 and -std=c++20.
