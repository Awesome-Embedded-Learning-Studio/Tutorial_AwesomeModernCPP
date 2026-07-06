---
chapter: 3
cpp_standard:
- 17
description: TMAM answers "which class of bottleneck," flame graphs answer "which stretch of code." This article covers the standard perf record sampling workflow, how to generate and read on-CPU and off-CPU flame graphs, plus two advanced tools — COZ (a causal profiler that tells you which function is worth optimizing most) and eBPF (the modern programmable tracing substrate)
difficulty: advanced
order: 3
platform: host
prerequisites:
- TMAM's four buckets and hardware sampling
- Benchmark methodology reference card
reading_time_minutes: 8
related:
- The USE method and the Roofline model
- Attribution in practice — from a slow program to the bottleneck
tags:
- host
- cpp-modern
- advanced
- 优化
- 工程实践
title: "Flame graphs, the perf workflow, and COZ / eBPF"
translation:
  source: documents/vol6-performance/ch03-attribution-methodology/03-03-flamegraph-perf.md
  source_hash: f796606cd5abf9b17dbccb7becf8329a0fcf9325e2f1077d5d9f5bf4826f94d3
  translated_at: '2026-07-06T00:00:00+00:00'
  engine: manual
  token_count: 3000
---
# Flame graphs, the perf workflow, and COZ / eBPF

## From "which class" to "which code"

Last article's TMAM attributes the bottleneck to a pipeline bucket (Frontend / Backend / Bad Spec), but "Backend Memory Bound 60%" still can't be pinned to a line of code. You need to know which function, which loop is producing those cache misses before you can change anything. That calls for a profiler that can **aggregate by code location**.

The **flame graph**, invented by Brendan Gregg, is the most readable of the bunch: it draws sampled call stacks as a "stacked-boxes chart," and at a glance you can see which call chain the time is spent on. Combined with Linux's built-in `perf`, this pair is the absolute mainstay of everyday profiling. This article covers the perf workflow, how to read a flame graph, and two advanced tools (COZ causal profiling and eBPF programmable tracing).

## The perf record workflow

The whole flow is three steps: sample, fold the stack, draw. Sampling uses `perf record`:

```bash
# Standard on-CPU sampling: 99Hz, DWARF call stacks (no frame pointer dependency)
perf record -F 99 --call-graph dwarf -- ./app

# Export after sampling
perf script > out.perf

# Fold + draw (Brendan Gregg's FlameGraph repo scripts)
./stackcollapse-perf.pl out.perf > out.folded
./flamegraph.pl out.folded > out.svg
# Open out.svg in a browser; hover/click to zoom
```

A few **key parameters and gotchas** (all extensions of ch01-03's "measurement pitfalls"):

- **`-F 99` (sampling frequency 99Hz)**: why not 100Hz? Because 100 is a "round number" for many built-in timers and easily **phase-locks** with other periodic system events, producing regular sampling bias; 99 is an odd number (and doesn't divide 100), so sample points don't easily align with the system's round-number periodic events. This is a common small convention in performance analysis. **Note that 99 is not a prime** (99 = 9 × 11); it was picked simply as an odd number that doesn't divide 100, not for any prime property (Brendan Gregg's perf docs only say "99 Hertz," never explaining it as "prime").
- **`--call-graph dwarf` (rebuild the stack from DWARF debug info)**: GCC defaults to `-fomit-frame-pointer` (omit the frame pointer, freeing up one usable register), which means `perf` can't walk frame pointers to rebuild the call stack — the stack is broken. Two fixes: **add `-fno-omit-frame-pointer` at compile time** (recommended, near-zero cost, standard for profiling), or **sample with `--call-graph dwarf`** (rebuild the stack from DWARF debug info, a bit slower but no recompile needed). For a release binary you intend to profile, always add `-fno-omit-frame-pointer`.
- **Sampling is statistical**: `perf record` is sampling, not tracing. 99Hz for 10 seconds collects only ~990 samples per core, and short functions may not get a single sample. To catch rare hotspots, raise the frequency or extend the run; to capture one-shot startup overhead, switch to a tracing tool (perf c2c / Intel PT / ftrace).

## How to read a flame graph

The structure of a flame graph:

- **y axis (vertical) is call-stack depth**: the bottom is the entry point (`main`), and each layer up is a called function. One box stacked on another = "the lower one called the upper one."
- **x axis (horizontal) is sample count (not time order!)**: the wider the box, the bigger this function's share (and its children's) in the on-CPU sample. **The left-to-right order on x does not represent execution order**, it's just an alphabetical aggregation.
- **How to read it**: find **the widest box**, that's the function eating the most on-CPU time. But it's not necessarily the function you should optimize — you have to look at **what it sits on top of** (the call-stack context).

Two common misreadings to avoid:

1. **"The widest box must be the one to optimize"**: wrong. A "wide and flat" box (no child boxes on top) is a real hotspot worth optimizing; a "wide box with a tall stack on top" just means **the children it calls are eating time**, optimizing the box itself is useless, you optimize the widest of the children stacked above it.
2. **"The x axis is time"**: it isn't. A flame graph is an **aggregation**, not a timeline. To see "which time window was busy, in order," use a timeline / chrometrace-style tool.

### Two important variants: on-CPU vs off-CPU

The standard flame graph is **on-CPU** (what's running on the CPU), answering "where did compute time go." But it can't see **what you're waiting on**. If a program is slow because it's waiting on a lock, on IO, on sleep, the on-CPU flame graph will be empty (because at sample time the CPU isn't running your program at all).

In that case use an **off-CPU flame graph**: it samples **the call stack at the moment the thread leaves the CPU**, meaning "when you were waiting, which function were you waiting in." The widest box on the off-CPU graph is the wait you most need to cut. Brendan Gregg likens on-CPU and off-CPU to two sides of a coin: on-CPU for compute, off-CPU for waiting, and only together are they complete. In production a lot of "slow" is actually waiting (database, locks, network), and off-CPU is the key.

Generating an off-CPU graph needs bcc / bpftrace (eBPF tools, covered below), more involved than on-CPU, but irreplaceable for "stalls" type problems.

## COZ: a causal profiler that tells you "which function is worth optimizing most"

Ordinary profilers (flame graphs, perf) have a fundamental limit: they tell you "where time is going," but they don't tell you "where the biggest payoff is." A function takes 50% of the time; you make it 2x faster; how much does total time drop? Intuitively, 25%, but Charlie Curtsinger's team shows in the COZ paper (*COZ: Finding Code that Counts with Causal Profiling*, SOSP 2015) that this assumes "optimizing this function doesn't affect the cost of other functions," and in reality functions share resources (locks, cache) — speeding one up can slow another down.

COZ solves this with **causal profiling**: at runtime it virtually "speeds up" some target function, but not by actually making that function faster. Instead it does the opposite — **it inserts pauses into all the other concurrently running threads, slowing them down** (Bakhvalov, *Performance Analysis and Tuning on Modern CPUs*, §11.5; Curtsinger & Berger, SOSP 2015). Slowing everyone else down is mathematically equivalent to speeding the target function up. It then measures the total speedup; because the "other threads' time" variable is controlled, the total change can be cleanly attributed to the target function, which is exactly what "causal" means. That lets it directly answer "if I speed function X up by 10%, how much faster does the whole program get," which is the real test of "is it worth optimizing."

COZ's output is "a virtual-speedup → total-speedup curve per function," and the function with the steepest slope is the one with the biggest payoff, even if its own CPU share isn't the highest. That's a different angle from a flame graph's "find the widest box," and it's more accurate for ranking optimization priorities. COZ is open source on Linux (curtsinger.cc/coz); usage is link the COZ runtime, run the program, view the profile.

## eBPF: the modern programmable tracing substrate

**eBPF** (extended Berkeley Packet Filter) is the biggest change in Linux performance tooling in recent years. In short, it lets you **safely run small programs inside the kernel**, hooked onto various hook points (syscalls, kernel functions, tracepoints, USDTs…), collecting data on demand. It needs no kernel changes, no reboot, and its overhead is controllable.

Why should performance analysis care about eBPF? Because a pile of the tools above are built on it:

- **Off-CPU flame graphs**: bcc's `profile` or bpftrace can sample off-CPU stacks.
- **Much of `perf`**: a new generation of BPF-based tools (`bpftrace` one-liners) is more flexible.
- **System-level tracing**: `biosnoop` (block IO latency), `execsnoop` (new processes), `tcplife` (TCP connection lifecycle)… Brendan Gregg's bcc toolset has dozens, all written in eBPF.

For C++ backend work, the most practical one is **bpftrace**, a "one-line DSL for performance analysis." For example, one line traces the latency distribution of a userspace function call:

```bash
# Trace latency (us) of my_func in mysqld, as a histogram
bpftrace -e 'uprobe:/path/to/bin:my_func { @start[tid] = nsecs; }
             uretprobe:/path/to/bin:my_func /@start[tid]/ {
               @lat = hist((nsecs - @start[tid]) / 1000); delete(@start[tid]);
             }'
```

The depth of eBPF (how to write BPF programs) is beyond vol6's scope; here I only want to plant one recognition in your head: **the substrate of modern Linux performance tools is eBPF, and off-CPU and system-level tracing are all built on it.** When you need it, brendangregg.com has the full tutorial.

Compressing this article into a cheat sheet: the mainstay of everyday on-CPU profiling is perf record plus flame graphs, and you must use `-fno-omit-frame-pointer` or `--call-graph dwarf`, otherwise the stack breaks; reading a flame graph means finding the wide box, but distinguishing "wide itself (real hotspot)" from "wide on top (children eat time)," and the x axis is aggregation, not time; off-CPU flame graphs show "what you're waiting on," treat stall-class problems, and rely on eBPF (bcc/bpftrace); COZ is a causal profiler that directly tells you which function has the biggest total payoff, more accurate than "find the widest box"; eBPF is the substrate of modern Linux performance tooling, and off-CPU and system-level tracing are all built on top of it.

That rounds out ch03's four tools (USE / Roofline / TMAM / flame graphs + COZ + eBPF). The next article chains them into one complete workflow: take a slow program, walk from "it's slow" all the way to "here's the bottleneck."

## References

- Brendan Gregg, *Flame Graphs*, brendangregg.com/FlameGraphs/cpuflamegraphs.html — the original flame-graph piece, generation scripts (the FlameGraph repo), and how to read them.
- Gregg, *Brendan Gregg's perf Examples* / *perf one-liners* — a cheat sheet for the perf workflow.
- Curtsinger et al., *COZ: Finding Code that Counts with Causal Profiling*, SOSP 2015 — causal profiling; curtsinger.cc/coz has the open-source implementation.
- Gregg, *BPF Performance Tools* (book) / bcc / bpftrace documentation — the eBPF toolset.
- Bakhvalov, *Performance Analysis and Tuning on Modern CPUs*, §11.5–11.6 — COZ and eBPF in CPU tuning.
