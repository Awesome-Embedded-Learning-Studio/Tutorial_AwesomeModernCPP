---
sidebar_order: 0
title: "Concurrency Worldview and the First Tool"
description: "Stand up a concurrency mindset from one blocked-solid main loop, and catch your first data race with ThreadSanitizer"
translation:
  source: documents/vol5-concurrency/ch00-concurrency-fundamentals/index.md
  source_hash: 443549b19a87d3509869dc4a39e79915984fa3ea827b671314572f8f7edd21ea
  translated_at: '2026-09-29T14:40:17+00:00'
  engine: anthropic
  token_count: 400
---

# Concurrency Worldview and the First Tool

We don't start this volume with `std::thread`. Most tutorials open by walking the reader straight into spawning threads; we'd rather build the mindset up front: why concurrency is worth it, and what things look like when they go wrong. Once both questions have landed, every line of concurrency code that follows has some judgment behind it. The three principles written at the head of this volume — `correctness before performance`, `locks before lock-free`, `synchronization before tasks` — govern every tradeoff in every chapter that follows, and in this chapter we'll spell them out one by one.

The arrangement of the two articles goes like this. The first uses nothing but a main loop blocked solid to make clear "why we need concurrency" and "where concurrency differs from parallelism", and along the way it hefts the upper bound on the payoff from parallelism, plus when you shouldn't reach for concurrency at all. The second is Tools 101: data races are the number-one killer in concurrency, so we take ThreadSanitizer as our first tool, learning both to read its reports and to recognize its limits. As for how much it costs to spin up a thread, that number has to be measured by hand, and we placed the measuring method in [OS Threads and Their Cost](../ch04-concurrent-data-structures/00-os-threads-and-cost.md) at the head of [Chapter 4](../ch04-concurrent-data-structures/) — by the time you head back there, you'll have the hands-on feel to pick it right up.

The chapter map, the lab system, and the reading conventions for the whole volume all live on [the volume home page](../) — when you're lost, go back to that page and get your bearings.

## In This Chapter

<ChapterNav variant="sub">
  <ChapterLink href="01-why-concurrency">Why Concurrency: A Blocked Main Loop</ChapterLink>
  <ChapterLink href="02-data-race-and-tsan">Data Races and ThreadSanitizer, Lesson One</ChapterLink>
</ChapterNav>
