---
title: "Attribution methodology: from measurement to bottleneck"
description: "After you've measured something is slow, you still have to answer why it's slow and where. ch03 lays out four complementary attribution frameworks: USE for the system-wide view, Roofline for compute-vs-bandwidth, the four TMAM buckets for pipeline locality, and flame graphs plus COZ plus eBPF for landing on actual code, then ties them together into a coarse-to-fine funnel workflow in a walkthrough article"
---

# Attribution methodology: from measurement to bottleneck

ch01 teaches us how to measure performance accurately, and ch02 hands us the hardware base. But between "this is slow" and "I know why it's slow" sits an entire discipline. A program can be slow because the CPU can't compute fast enough, because the data can't be moved in fast enough, because it's waiting on a lock or on IO, or because it's thrashing memory pages. **The precondition for the right fix is the right diagnosis.** This chapter turns diagnosis into a reusable workflow.

The four tools are arranged as a coarse-to-fine funnel. Each step costs more than the one before it (in time, in tooling, in intrusiveness), but each step also narrows the battlefield for the next:

- **USE** (ch03-01): a two-minute system sweep to rule out "the problem isn't even on the CPU."
- **Roofline** (ch03-01): with a pen, compute the arithmetic intensity and decide compute-bound vs bandwidth-bound, which sets the optimization direction.
- **TMAM four buckets** (ch03-02): drill down with `toplev`, attribute the bottleneck to Frontend / Backend Memory / Backend Core / Bad Speculation on the pipeline, then sample precisely down to specific instructions.
- **Flame graphs + COZ + eBPF** (ch03-03): land on "which function is slow" and use a causal profiler to rank optimization priorities.
- **Walk-through** (ch03-04): with a real weighted-dot-product case, chain the four tools into one complete workflow.

One discipline runs through this chapter: **bottlenecks migrate**. You fix Backend Memory and Bad Speculation surfaces underneath. Attribution is therefore iterative, with a re-measure after every change, not "run the profiler once, fix once." Once you've localized accurately, ch04's "optimize by bottleneck site" can prescribe the right medicine.

> The tools in this chapter (`perf` / `toplev` / flame-graph scripts) are mostly system-level profilers. The articles are written on a WSL2 machine where these aren't installed, so the specific commands and outputs are **quoted from authoritative sources (Brendan Gregg, Bakhvalov, easyperf.net) and labeled as such**, not faked as if run locally. What can be measured on this machine (arithmetic intensity, raw timings, cache behavior) is labeled "measured." The commands themselves are standard Linux performance-analysis moves and work the same on a bare-metal Linux box with the tooling installed.

## In this chapter

<ChapterNav variant="sub">
  <ChapterLink href="03-01-use-and-roofline">The USE method and the Roofline model</ChapterLink>
  <ChapterLink href="03-02-tmam-and-hw-sampling">TMAM's four buckets and hardware sampling: LBR / PEBS / Intel PT</ChapterLink>
  <ChapterLink href="03-03-flamegraph-perf">Flame graphs, the perf workflow, and COZ / eBPF</ChapterLink>
  <ChapterLink href="03-04-walkthrough">Attribution in practice: from a slow program to the bottleneck</ChapterLink>
</ChapterNav>
