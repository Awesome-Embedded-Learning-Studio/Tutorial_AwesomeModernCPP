---
title: "Benchmark methodology"
description: "The volume's anchor chapter: starting from why microbenchmarks lie, it builds out the full methodology of measurement, statistics, production and CI regression — every later performance article points back here"
---

# Benchmark methodology

This is the volume's **anchor chapter**. ch00 established the two iron rules — "correct first, then fast" and "measure first, then optimize" — but behind "measure first" hides an entire discipline. Performance isn't a boolean, it's a distribution; a one-off hand-rolled measurement is almost meaningless; and the microbenchmark, the handiest tool in the box, is also the one most likely to lie to you.

This chapter tears "measuring accurately" down to the studs: first see through the three ways a microbenchmark deceives you, then learn how to write one that doesn't (`DoNotOptimize` semantics, parameter sweeps, repetition aggregation), then a 16-item environment-ready checklist to shut noise sources down one by one, then statistical methods to turn a distribution into a trustworthy conclusion, and finally how to move measurement into production and CI to stand guard over performance. Every performance article after this one opens by pointing back to the rules here — the way vol5 threads TSan through concurrency correctness.

If you only read a few chapters in this volume, this one should be most of them.

## In this chapter

<ChapterNav variant="sub">
  <ChapterLink href="01-why-microbenchmarks-lie">Why microbenchmarks lie to you</ChapterLink>
  <ChapterLink href="02-credible-microbenchmark">How to write a credible microbenchmark</ChapterLink>
  <ChapterLink href="03-pitfalls-and-env">Measurement pitfalls and environment readiness: a 16-item checklist</ChapterLink>
  <ChapterLink href="04-statistics-and-reporting">Statistics and reporting: turning a distribution into a conclusion</ChapterLink>
  <ChapterLink href="05-production-and-ci">Production measurement and CI performance regression detection</ChapterLink>
  <ChapterLink href="06-methodology-reference">Benchmark methodology reference card</ChapterLink>
</ChapterNav>
