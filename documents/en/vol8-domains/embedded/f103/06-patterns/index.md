---
title: "Patterns: object pools, intrusive containers, interrupt safety"
description: "The complete record of one line of work on the logging component: zero-overhead design, Formatter dispatch selection, measured stack budgets and sizes, and the map-file verdict; the seventh piece collects the usage side — how the two calling styles and two viewing surfaces work"
chapter: 6
order: 0
tags:
  - stm32f1
  - intermediate
  - 对象池
  - 侵入式容器
difficulty: intermediate
platform: stm32f1
translation:
  source: documents/vol8-domains/embedded/f103/06-patterns/index.md
  source_hash: c1bba6fa63a021c870d931a74b3f7788a1e9db95155d8a40a3da85a313b64610
  translated_at: '2026-11-22T16:44:30+00:00'
  engine: glm-5.3-refresh
  token_count: 1124
---

# Patterns: object pools, intrusive containers, interrupt safety

## Overview

What has landed at this stop so far is one line of work on the logging component: the first six pieces run the complete record from hitting the source_location wall, through Formatter dispatch selection, measured stack budgets, and two compiler flags cutting the firmware, all the way to the map-file verdict; the seventh piece collects the usage side — how the logger's two calling styles and two viewing surfaces work. The idiom pieces on object pools, intrusive containers, and interrupt safety will be added once the material matures.

## Chapter Navigation

<ChapterNav variant="sub">
  <ChapterLink href="01-logger-zero-overhead">A zero-overhead logger: the complete pitfall log, from hitting the source_location wall to acceptance by disassembly</ChapterLink>
  <ChapterLink href="02-dispatch-overload-to-formatter">From overload sets to Formatter specializations: choosing the dispatch mechanism for an embedded logger</ChapterLink>
  <ChapterLink href="03-logger-stack-budget">The logger's stack budget, measured: can a function-local LineBuffer blow the stack?</ChapterLink>
  <ChapterLink href="04-two-flags-size-cut">Two compiler flags cut the firmware by 64%: a field record of function-granularity reclamation with -ffunction-sections</ChapterLink>
  <ChapterLink href="05-map-is-the-verdict">The final verdict sits in the map file: the complete evidence chain that, at this trim level, not even the strings make it into the firmware</ChapterLink>
  <ChapterLink href="06-zero-overhead-price-list">The price list of zero overhead: dissecting the 18,200-byte logger firmware</ChapterLink>
  <ChapterLink href="07-logger-dual-syntax-usage">Putting the logger to work: two calling styles and two viewing surfaces</ChapterLink>
</ChapterNav>
