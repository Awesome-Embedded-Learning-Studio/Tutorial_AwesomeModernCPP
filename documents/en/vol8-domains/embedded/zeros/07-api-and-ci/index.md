---
title: "Tidy-up and Gatekeeping: Demo Registry and CI"
description: "A 342-line monolithic example is split into one scene per room, with one line of registration automatically generating firmware and Renode targets; task creation converges into three paths; CI uses four checks to automate full green for each station — host unit tests, include firewall, size ceiling, and Renode smoke tests; with a safety net in place, the performance optimizations that follow are justified."
chapter: 7
order: 0
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - CMake
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
translation:
  source: documents/vol8-domains/embedded/zeros/07-api-and-ci/index.md
  source_hash: 6a7e12359637b16e3d1f1a9df50c3b5c09c0bc8fd828183a001dfa05ef6600c1
  translated_at: '2026-11-22T13:58:19+00:00'
  engine: qwen3:14b
  token_count: 1026
---
# Tidy-up and Gatekeeping: Demo Registry and CI

> We publish one article at a time, and both articles in this chapter have been published.

## Overview

The kernel's capabilities are now complete, and the example has grown to over 300 lines. The eight demos are all crammed into one room, and without tidying up, we can't teach, nor can we reach the point of passing acceptance tests. In this chapter, we tidy up and establish checks. First, we tidy up: the demos are split into a registry, where one line of registration can automatically generate firmware and Renode targets, with each of the eight scenarios having its own scene and room. The way tasks are created converges into three paths: manually laid out slots, arena slicing, and builder chains, each suitable for different situations. Weak and strong interrupt handlers are handed over to the linker for conditional writing, and the will has become a mandatory obligation. Second, we establish checks: CI automates four checkpoints — host unit tests, include firewall (kernel headers must not recognize the board), size ceiling, and Renode smoke tests where grep searches for each demo's lines. Every station runs green automatically from now on. The repository now has its first safety net, and the performance optimizations that follow are justified.

## Articles on This Station

<ChapterNav variant="sub">
  <ChapterLink href="01-demo-registry-and-arena">One Scene, One Room: Demo Registry, Three Paths for Task Creation, and the Will Becomes an Obligation</ChapterLink>
  <ChapterLink href="02-ci-gates">Run for Five Seconds, Say Your Lines: CI Smoke Tests, Include Firewall, and Size Gate</ChapterLink>
</ChapterNav>

With the safety net in place, we should now answer that question that has been hanging since the beginning: How expensive is the cost of a single context switch? The next chapter will be all about real silicon, speaking in measured numbers.
