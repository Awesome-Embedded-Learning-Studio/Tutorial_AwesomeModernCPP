---
title: "Attribution methodology: from measurement to bottleneck"
description: "Once you've measured that something is slow, you still have to answer why it's slow and where. ch03 provides four complementary attribution frameworks: USE for the system-wide view, Roofline to judge compute vs bandwidth, the four TMAM buckets to pin down which pipeline stage is hurting, and flame graphs + COZ + eBPF to land on the code — then a hands-on walkthrough ties them into a coarse-to-fine funnel workflow"
translation:
  source: documents/vol6-performance/ch03-attribution-methodology/index.md
  source_hash: 9426876dac4abf69697b6232b4cce2f0300833b9e6a79943cc6baaf5a257cfea
  translated_at: '2026-09-26T06:07:51+00:00'
  engine: anthropic
  token_count: 1300
---

# Attribution methodology: from measurement to bottleneck

ch01 taught us how to measure performance accurately, and ch02 handed us the hardware foundation. But between "measured it's slow" and "knowing why it's slow" sits an entire discipline: a program can be slow because the CPU can't keep up with the computation, because the data can't be moved in fast enough, because it's waiting on a lock or on IO, or because it's simply thrashing memory pages — **the right fix presupposes the right diagnosis**. This chapter turns that diagnosis into a reusable workflow.

The four toolsets are arranged as a **coarse-to-fine funnel**: each step costs more than the one before (time spent, tooling required, and intrusiveness all climb), but each step shrinks the battlefield for the next:

- **USE** (ch03-01): a two-minute system sweep to rule out "the problem isn't the CPU at all".
- **Roofline** (ch03-01): a pen-and-paper arithmetic-intensity calculation to decide compute-bound vs bandwidth-bound and set the broad direction for optimization.
- **The four TMAM buckets** (ch03-02): drill down with `toplev`, attribute the bottleneck to a pipeline stage — Frontend / Backend Memory / Backend Core / Bad Speculation — then sample precisely down to the specific instructions.
- **Flame graphs + COZ + eBPF** (ch03-03): land on "which function is slow", and use a causal profiler to rank optimization priorities.
- **Hands-on walkthrough** (ch03-04): a real weighted-dot-product case that chains the four toolsets into one complete pass.

One discipline runs through this entire chapter: **bottlenecks migrate**. You fix Backend Memory, and Bad Speculation may surface right behind it. Attribution is therefore iterative — re-measure after every change, instead of "profile once, tweak once". Only once the bottleneck is pinned down can ch04's "optimize by bottleneck site" advice hit the mark.

> The tools in this chapter (`perf`, `toplev`, the flame graph scripts) are mostly system-wide profilers. The articles were written under WSL2, where our machine doesn't have them installed, so the specific profiler commands and outputs are **quoted from authoritative sources (Brendan Gregg, Bakhvalov, easyperf.net) and marked as such** — we don't pretend to have run them locally; everything we could measure on our own machine (arithmetic intensity, raw runtimes, cache behavior) is marked "measured". The commands themselves are standard practice for Linux performance analysis, and they work just the same on a bare-metal box with the tools installed.

## In this chapter

<ChapterNav variant="sub">
  <ChapterLink href="03-01-use-and-roofline">The USE method and the Roofline model</ChapterLink>
  <ChapterLink href="03-02-tmam-and-hw-sampling">The four TMAM buckets and hardware sampling: LBR / PEBS / Intel PT</ChapterLink>
  <ChapterLink href="03-03-flamegraph-perf">Flame graphs, the perf workflow, and COZ / eBPF</ChapterLink>
  <ChapterLink href="03-04-walkthrough">Attribution in practice: from a slow program to a pinpointed bottleneck</ChapterLink>
</ChapterNav>
