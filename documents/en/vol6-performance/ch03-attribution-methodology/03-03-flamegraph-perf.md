---
chapter: 3
cpp_standard:
- 17
description: TMAM answers "which class of bottleneck"; flame graphs answer "which stretch of code." This article covers the standard perf record sampling workflow, how to generate and read on-CPU and off-CPU flame graphs, plus two advanced tools — COZ (a causal profiler that tells you which function is most worth optimizing) and eBPF (modern programmable tracing)
difficulty: advanced
order: 3
platform: host
prerequisites:
- 'The four TMAM buckets and hardware sampling: LBR / PEBS / Intel PT'
- Benchmark methodology reference card
reading_time_minutes: 8
related:
- 'The USE method and the Roofline model: system-wide first, then compute vs bandwidth'
- 'Attribution in practice: from a slow program to a pinpointed bottleneck'
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
  translated_at: '2026-09-26T06:07:40+00:00'
  engine: anthropic
  token_count: 2100
---
# Flame graphs, the perf workflow, and COZ / eBPF

## From "which class of bottleneck" to "which stretch of code"

Last article, TMAM attributed the bottleneck to one pipeline bucket (Frontend / Backend / Bad Spec), but a conclusion like "Backend Memory Bound 60%" still doesn't land on a line of code. You need to know which function, which loop is manufacturing those cache misses before you can act on it. What that calls for is a profiler that can aggregate by code location.

The **flame graph**, invented by Brendan Gregg, is the most readable of these tools: it draws the sampled call stacks as a chart of stacked boxes, and at a glance you can see which call chain the time is spent on. Paired with Linux's built-in `perf`, this combo is the absolute workhorse of everyday profiling. This article covers the perf workflow plus how to read a flame graph, then introduces two advanced tools (COZ causal profiling and eBPF programmable tracing).

## The perf record workflow

The whole flow is three steps: sample → fold the stacks → draw the chart. Sampling uses `perf record`:

```bash
# Standard on-CPU sampling: 99Hz, DWARF call stacks (no frame-pointer dependency)
perf record -F 99 --call-graph dwarf -- ./app

# Export after sampling
perf script > out.perf

# Fold + draw (Brendan Gregg's FlameGraph repo scripts)
./stackcollapse-perf.pl out.perf > out.folded
./flamegraph.pl out.folded > out.svg
# Open out.svg in a browser; hover / click to zoom
```

A few **key parameters and gotchas** (all extensions of ch01-03's "measurement pitfalls"):

- **`-F 99` (sampling frequency of 99Hz)**: why not 100Hz? Because 100 is a "round number" for many built-in timers, and it easily phase-locks with other periodic system events, giving the samples a regular bias; 99 is an odd number (and doesn't divide 100), so sample points are less likely to align with the system's round-number periodic events. This is a small convention you'll see all over performance analysis. **Note that 99 is not a prime** (99 = 9 × 11) — it was picked simply as an odd number that doesn't divide 100, not for any prime property (Brendan Gregg's perf documentation only says "99 Hertz" and never explains it as "prime").
- **`--call-graph dwarf` (rebuild the stack from DWARF debug info)**: GCC defaults to `-fomit-frame-pointer` (omit the frame pointer, freeing one more usable register), which means `perf` can't rebuild the call stack by walking frame pointers — the stack comes out broken. Two fixes: **add `-fno-omit-frame-pointer` at compile time** (recommended; nearly zero cost, the standard practice when profiling), or **sample with `--call-graph dwarf`** (rebuild the stack from DWARF debug info, a bit slower but no build changes needed). For a release binary you intend to profile, always build with `-fno-omit-frame-pointer`.
- **Sampling is statistical**: `perf record` samples, it does not trace. At 99Hz for 10 seconds you collect only ~990 samples per core, and a short function may not catch a single one. To see rare hotspots, raise the frequency or extend the run; to see one-shot startup overhead, switch to a tracing tool (perf c2c / Intel PT / ftrace).

## How to read a flame graph

The structure of a flame graph:

- **The y axis (vertical) is call-stack depth**: the bottom is the entry point (`main`), and each layer above is a called function. One box resting on top of another = "the one below called the one above."
- **The x axis (horizontal) is sample count (not chronological order!)**: the wider the box, the bigger this function's share (and its children's) of the on-CPU samples. **The left-to-right order on the x axis does not represent execution order** — it's just an alphabetical aggregation.
- **How to read it**: find **the widest box**; that's the function burning the most on-CPU time. But it isn't necessarily the function you should optimize — you have to look at what it rests on (the call-stack context).

Two common misreadings to avoid:

1. **"The widest box must be the one to optimize"**: no. A "wide and flat" box (no child boxes stacked on top of it) is a genuine hotspot, worth optimizing; a "wide box with a big pile stacked on top" is wide only because the functions it calls burn time — optimizing it itself is useless, and you should optimize the widest of the child boxes stacked above it.
2. **"The x axis is time"**: it isn't. A flame graph is an aggregation, not a timeline. To see "which time window was busy, in chronological order," you need a timeline / chrometrace-style tool.

### Two important variants: on-CPU vs off-CPU

The standard flame graph is **on-CPU** (what is running on the CPU), answering "where the compute time went." But it cannot see "**what you are waiting on**." If the program is slow because it's waiting on a lock, on IO, or on sleep, the on-CPU flame graph will be empty (because at sample time the CPU isn't running your program at all).

In that situation, use an **off-CPU flame graph**: it samples "**the call stack at the moment the thread leaves the CPU**" — that is, "while you were waiting, which function were you waiting in." The widest box on the off-CPU graph is the wait you most need to eliminate. Brendan Gregg likens on-CPU and off-CPU to two sides of the same coin: on-CPU shows computing, off-CPU shows waiting, and only together are they complete. In production, a lot of "slow" is actually waiting (waiting on the database, on locks, on the network), and that's where off-CPU is the key.

Generating an off-CPU graph requires bcc / bpftrace (eBPF tools, covered below), more hassle than on-CPU, but irreplaceable for "stall-type" problems.

## COZ: a causal profiler that tells you which function is most worth optimizing

Ordinary profilers (flame graphs, perf) have a fundamental limitation: they tell you "where the time went," but not "where optimizing pays off the most." A function takes 50% of the time; you make it 2x faster — how much does the total time drop? Intuition says 25%, but the COZ paper from Charlie Curtsinger's team (*COZ: Finding Code that Counts with Causal Profiling*, SOSP 2015) points out that this assumes "optimizing this function doesn't affect the time cost of the other functions," while in reality functions share resources (locks, cache), so optimizing one place can slow down another.

COZ solves this with **causal profiling**: while the program runs, it virtually "speeds up" a chosen target function — but not by actually making that function faster. Quite the opposite: **it inserts pauses into all the other concurrently running threads, slowing them down** (Bakhvalov, *Performance Analysis and Tuning on Modern CPUs*, §11.5; Curtsinger & Berger, SOSP 2015). Slowing everyone else down is mathematically equivalent to having sped the target function up. It then measures how much the whole thing sped up; because the "other threads' time" variable is held under control, the measured overall change can be attributed cleanly to the target function — that is exactly what the word "causal" means here. This way it can directly answer "if I speed function X up by 10%, how much faster does the whole program get," and that is the real test of "is it worth optimizing."

COZ's output is a "virtual speedup → overall speedup" curve for each function; the function with the steepest slope is the optimization target with the biggest payoff, even if it doesn't own the most CPU time itself. That's a different angle from the flame graph's "find the widest box," and it is more accurate for ranking optimization priorities. COZ is open source on Linux (curtsinger.cc/coz); the usage is: link the COZ runtime, run the program, inspect the profile.

## eBPF: the substrate of modern programmable tracing

**eBPF** (extended Berkeley Packet Filter) is the biggest game-changer in Linux performance tooling in recent years. Simply put, it lets you safely run small programs inside the kernel, attached to all kinds of hook points (syscalls, kernel functions, tracepoints, USDT…), collecting data on demand. No kernel changes, no reboot, controllable overhead.

Why should performance analysis care about eBPF? Because a whole pile of the earlier tools are built on top of it:

- **Off-CPU flame graphs**: bcc's `profile` or bpftrace can sample off-CPU stacks.
- **Much of what `perf` does**: the new generation of BPF-based tools (`bpftrace` one-liners) is more flexible.
- **System-wide tracing**: `biosnoop` (block IO latency), `execsnoop` (new processes), `tcplife` (TCP connection lifecycle)… Brendan Gregg's bcc toolset has dozens of tools, all written in eBPF.

For C++ backend work, the most practical one is **bpftrace**, a "one-line DSL purpose-built for performance analysis." For example, a single command traces the latency distribution of calls to a userspace function:

```bash
# Trace the latency (us) of a function in mysqld, as a histogram
bpftrace -e 'uprobe:/path/to/bin:my_func { @start[tid] = nsecs; }
             uretprobe:/path/to/bin:my_func /@start[tid]/ {
               @lat = hist((nsecs - @start[tid]) / 1000); delete(@start[tid]);
             }'
```

The depth of eBPF (how to write BPF programs) is beyond vol6's scope; here we only want to plant one idea in your head: **the substrate of modern Linux performance tools is eBPF, and off-CPU and system-wide tracing both rest on it**. When you need it, brendangregg.com has the full tutorials.

Compressing this article into one cheat card: the workhorse of everyday on-CPU profiling is perf record + flame graphs — always use `-fno-omit-frame-pointer` or `--call-graph dwarf`, otherwise the stack comes out broken; when reading a flame graph, find the wide box, but distinguish "wide itself (a real hotspot)" from "wide because of what's stacked on top (children burning time)," and remember the x axis is aggregation, not time; off-CPU flame graphs show "what you're waiting on" and cure stall-type problems, built on eBPF (bcc/bpftrace); COZ is a causal profiler that directly tells you which function's optimization pays off the most overall, more accurate than "find the widest box"; and eBPF is the substrate of modern Linux performance tooling — off-CPU and system-wide tracing are both built on top of it.

That wraps up ch03's four toolsets (USE / Roofline / TMAM / flame graphs + COZ + eBPF). In the next article we chain them into one complete workflow: take a slow program, and walk from "it's slow" all the way to "here is the bottleneck."

## References

- Brendan Gregg, *Flame Graphs*, brendangregg.com/FlameGraphs/cpuflamegraphs.html — the original flame graph write-up, the generation scripts (the FlameGraph repo), and how to read them
- Gregg, *Brendan Gregg's perf Examples* / *perf one-liners* — a quick reference for the perf workflow
- Curtsinger et al., *COZ: Finding Code that Counts with Causal Profiling*, SOSP 2015 — causal profiling; the open-source implementation is at curtsinger.cc/coz
- Gregg, *BPF Performance Tools* (book) / bcc / bpftrace documentation — the eBPF toolset
- Bakhvalov, *Performance Analysis and Tuning on Modern CPUs*, sections 11.5–11.6 — applying COZ and eBPF to CPU tuning
