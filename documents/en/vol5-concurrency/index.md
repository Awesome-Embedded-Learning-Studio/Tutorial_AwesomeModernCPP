---
title: "Volume 5: Concurrent Programming"
description: "From thread primitives to coroutine-based asynchrony"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
translation:
  source: documents/vol5-concurrency/index.md
  source_hash: efd4905f79dd93609060e79086e7fd009e5b50039c1c344107a3f1941034ef79
  translated_at: '2026-11-22T16:22:55+00:00'
  engine: glm-5.3-parent
  token_count: 1990
---

# Volume 5: Concurrent Programming

In this volume, we talk about the part that is hard to get around in almost every domain of programming: concurrent programming.

The author has already restructured this volume once, but quite a few articles are still tagged as LLM first drafts — and the plan here is to mark that honestly! If you are willing, bring your LLM along and give these pieces your sharpest review.

## Experiments and Verification

The experiment programs for this volume live in `code/examples/vol5/` and in the inline examples on each page — click and run. The original experiments were verified on GCC 16.2.1, WSL2 (16 logical cores, 2026-11-14), and re-verified when extracted into standalone files. Behavior involving the C++ standard library was checked against cppreference and the current draft on eel.is; clause links are collected in each article's reference section. Output order may vary between runs, and timings drift with the machine — the magnitudes, the numbering, and the mechanisms stay the same.

## Chapter Navigation

This is the new version of the concurrent programming volume: eight stages at present, plus a cookbook for readers who already know concurrency and would rather skip the author's rambling.

<ChapterNav variant="sub">
  <ChapterLink href="chapter01-start-concurrency">Stage 1 · Getting a Program to Run Concurrently</ChapterLink>
  <ChapterLink href="chapter02-sharing-data">Stage 2 · How Multiple Tasks Use Data Together</ChapterLink>
  <ChapterLink href="chapter03-wait-and-coordinate">Stage 3 · Waiting and Coordinating Between Tasks</ChapterLink>
  <ChapterLink href="chapter04-organizing-tasks">Stage 4 · From Managing Threads to Organizing Tasks</ChapterLink>
  <ChapterLink href="chapter05-atomic-memory-model">Stage 5 · Understanding Atomic Operations and the Memory Model</ChapterLink>
  <ChapterLink href="chapter06-validating-designs">Stage 6 · Judging Whether a Concurrent Design Works</ChapterLink>
  <ChapterLink href="chapter07-coroutines">Stage 7 · Expressing Suspendable Work with Coroutines</ChapterLink>
  <ChapterLink href="cookbook">Cookbook · The Volume 5 Quick Reference</ChapterLink>
</ChapterNav>
