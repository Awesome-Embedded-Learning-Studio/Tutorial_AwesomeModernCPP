---
title: "Benchmark methodology"
description: "The anchor chapter of the entire vol6: starting from why microbenchmarks lie, it builds out the complete methodology of measurement, statistics, production and CI regression — every performance article that follows cites back to this chapter"
translation:
  source: documents/vol6-performance/ch01-benchmark-methodology/index.md
  source_hash: dd10415da96eb7f18e3e0020787ea426896252b9c93e4b9d98106efdd013c31e
  translated_at: '2026-09-26T05:46:09+00:00'
  engine: anthropic
  token_count: 300
---

# Benchmark methodology

This is the **anchor chapter** of the whole volume. ch00 laid down the two iron rules — "correct first, then fast" and "measure first, then optimize" — but behind that little phrase "measure first" hides an entire discipline: performance is not a boolean, it is a distribution; a one-off hand-rolled measurement is almost meaningless; and the microbenchmark, the handiest tool of them all, is also precisely the one most likely to lie to you.

This chapter tears "measuring accurately" down to the studs: first see through the three kinds of deception a microbenchmark can pull, then learn how to write one that doesn't lie (the semantics of `DoNotOptimize`, parameter sweeps, repetition and aggregation), then work through a 16-item environment-readiness checklist that shuts noise sources down one by one, then use statistics to turn the distribution into a trustworthy conclusion, and finally cover how to move measurement into production and CI to keep standing guard over performance. Every performance article after this one opens by citing back to this chapter's rules — the way vol5 threads TSan through concurrency correctness.

If you only read a few pieces from this volume, this chapter should account for most of them.

## In this chapter

<ChapterNav variant="sub">
  <ChapterLink href="01-why-microbenchmarks-lie">Why microbenchmarks lie</ChapterLink>
  <ChapterLink href="02-credible-microbenchmark">How to write a credible microbenchmark</ChapterLink>
  <ChapterLink href="03-pitfalls-and-env">Measurement pitfalls and environment readiness: a 16-item checklist</ChapterLink>
  <ChapterLink href="04-statistics-and-reporting">Statistics and reporting: turning a distribution into a conclusion</ChapterLink>
  <ChapterLink href="05-production-and-ci">Production measurement and CI performance regression detection</ChapterLink>
  <ChapterLink href="06-methodology-reference">Benchmark methodology reference card</ChapterLink>
</ChapterNav>
