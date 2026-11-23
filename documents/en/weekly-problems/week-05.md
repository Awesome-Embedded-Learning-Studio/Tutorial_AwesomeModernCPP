---
title: "Week 5 · The Feel of Containers"
description: "Topic: The feel of containers: range-for doesn't modify elements, set deduplication, find returns end(), accumulate truncation with initial value, map query-and-write, vector<bool> proxy, erase-remove as the finale. Seven online judge problems, difficulty from one to three stars."
chapter: 16
order: 6
dateRange: "2026-10-26 ~ 11-01"
difficulty: intermediate
platform: host
weeklyThanks:
  - github: Charliechen114514
    role: Self-proposed seven problems for this week · Problem and judge data tested in practice
tags:
  - host
  - cpp-modern
  - intermediate
  - 容器
translation:
  source: documents/weekly-problems/week-05.md
  source_hash: 87052f04443a28d38d835c3dbcfeedcc7e9766a953f1c4d633eade38d9dab48e
  translated_at: '2026-11-22T14:14:39+00:00'
  engine: qwen3:14b
  token_count: 1550
---
# Week 5 · The Feel of Containers

This week's theme is called "The Feel of Containers," focusing on the parts of STL that are easiest to mess up when getting started: those that everyone reads once in the documentation, but still get wrong when writing real code. The main focus is on containers, with a brief mention of two general conventions of algorithms. You write C++ every day, so treat each problem as a quick arithmetic exercise. If you're just learning containers, these seven problems will help you step on the most common pitfalls first.

There's a thread throughout the week: from "modifying one element" to "deleting a category of elements." The difficulty increases gradually: the first three days are one star, helping you get familiar with the basic behavior of containers; the middle three days are two stars, focusing on counterintuitive behaviors; and Sunday is the three-star finale. Each problem is small, and our only hope is that you personally step on each one, which will help you remember better than reading the documentation ten times.

<QuizProblem src="code/volumn_codes/weekly-problems/week-05/01-double-all" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-05/02-dedupe-sort" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-05/03-find-miss" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-05/04-accumulate-init" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-05/05-map-bracket" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-05/06-vector-bool" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-05/07-erase-remove" />

---

**Source of Problems**: All seven problems in this week are self-proposed. The problem proposer is GitHub user [Charliechen114514](https://github.com/Charliechen114514), who is also the author of this column. The seven problems revolve around "The Feel of Containers": the difference between auto and auto& in range-for, set deduplication and sorted traversal upon insertion, find returning end() on a miss, accumulate's initial value determining the accumulation type, map's operator[] writing even for read-only queries, vector<bool>'s index proxy, and finally, the erase-remove idiom on Sunday. All judge data (assertions, input/output test cases, multiple-choice answers, fill-in-the-blank answers, and line numbers for error detection) have been locally tested and verified using GCC 16.2.1 (-std=c++20 -Wall -Wextra).
