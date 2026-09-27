---
chapter: 3
cpp_standard:
- 17
description: TMAM (Top-Down Microarchitecture Analysis) sorts pipeline slots into four buckets — Retiring / Frontend Bound / Backend Bound / Bad Speculation — telling you which pipeline stage the bottleneck sits in. This article covers how the four buckets are drawn, the toplev workflow, and the hardware sampling mechanisms behind it — what data LBR / PEBS / Intel PT can each get you
difficulty: advanced
order: 2
platform: host
prerequisites:
- 'The USE method and the Roofline model: system-wide first, then compute vs bandwidth'
- 'Pipeline, ILP, and branch prediction: same data, several-fold differences depending on how you execute it'
reading_time_minutes: 7
related:
- 'Flame graphs, the perf workflow, and COZ / eBPF'
- 'Frontend optimization: code layout, PGO, and BOLT'
tags:
- host
- cpp-modern
- advanced
- 优化
- 工程实践
title: 'The four TMAM buckets and hardware sampling: LBR / PEBS / Intel PT'
translation:
  source: documents/vol6-performance/ch03-attribution-methodology/03-02-tmam-and-hw-sampling.md
  source_hash: 13e8a6ee3c48422f8159f7ca845611a8be3b9913dcc2eaaf395990df9bf559c8
  translated_at: '2026-09-26T06:07:42+00:00'
  engine: anthropic
  token_count: 1900
---
# The four TMAM buckets and hardware sampling: LBR / PEBS / Intel PT

## Attributing the bottleneck to a pipeline stage

The Roofline from the previous article can tell compute-bound from bandwidth-bound, but "compute" and "bandwidth" are both still too coarse. A CPU pipeline has many stages — fetch, decode, execute, memory access, branch prediction — and "not enough compute" could mean the decoder can't keep up (frontend), the execution units are waiting for data (backend), or branch prediction keeps missing and work gets flushed for nothing (bad speculation). The fixes for those three cases are worlds apart.

**TMAM (Top-Down Microarchitecture Analysis Method)** is the framework Intel proposed to answer exactly this question (Yasin, *A Top-Down Method for Performance Analysis and Tuning*, 2014). It takes the slots that can enter the pipeline each cycle and sorts them by their final fate into four buckets. Whichever bucket's share is abnormally high tells you which pipeline stage the bottleneck lives in. Andi Kleen later implemented the framework as the one-command `pmu-tools/toplev` tool, the workhorse of modern CPU performance analysis.

## The four buckets: four fates for a slot

Every cycle, the CPU frontend tries to "allocate" a certain number of slots (slot count = pipeline width). Those slots have only four possible fates:

| Bucket | Meaning | A high share points to | Typical fix (maps to ch04) |
|---|---|---|---|
| **Retiring** | the slot genuinely retired into a useful instruction | **the higher the better** (the ideal) | well-structured code; keep going |
| **Frontend Bound** | the slot idled because the frontend (fetch/decode) couldn't keep up | icache miss / iTLB miss / code bloat | code layout, PGO, BOLT (ch04-07) |
| **Backend Bound** | the slot stalled in the backend — execution units **waiting for data** (memory) or **waiting for a port** (core) | cache miss / data dependency / execution-port contention | cache optimization, SIMD, breaking dependency chains (ch04-01/02/03) |
| **Bad Speculation** | the slot was wasted on a **wrong speculative path** (flushed after a branch misprediction) | unpredictable branches | branchless, predication (ch04-06) |

How to read the four buckets: **Retiring is the good bucket; the other three are bad buckets, and whichever one's share is abnormally high is the current bottleneck.** A well-tuned numerical-compute kernel can reach 50%–70% Retiring (SIMD fully loaded); if yours sits at 20% while Backend Memory takes 60%, the thing to treat is cache, not adding SIMD.

Backend Bound splits further into two branches: **Backend Memory Bound** (waiting for data — cache misses, bandwidth) and **Backend Core Bound** (waiting for an execution port — division, a long-latency dependency chain). This subdivision is extremely useful: it maps directly onto the two routes in ch04, "treat memory" or "treat compute."

> Boundary note: the TMAM four buckets are an attribution framework — they tell you which class the bottleneck falls into. How to actually change things (vectorize, cut memory traffic, go branchless) is ch04's business. Don't unfold optimization detail inside the attribution chapter; that's ch04's home turf.

## The toplev workflow: drill down layer by layer

The workflow of `toplev` (the Python tool in `pmu-tools` that wraps Intel's TMAM performance counters) is to drill down level by level — typically three steps:

```bash
# 1. Look at the L1 four buckets first to pick the main battlefield
toplev -l1 -- ./app
# Sample output (from the easyperf.net examples, not run locally):
#   Frontend_Bound:      12.5%   ← normal
#   Backend_Bound:       58.0%   ← main bucket!
#   Bad_Speculation:      8.3%
#   Retiring:            21.2%

# 2. Backend is the main bucket, drill to L2 to see Memory vs Core
toplev -l2 -- ./app
#   Backend_Bound.Core_Bound:   15.0%
#   Backend_Bound.Memory_Bound: 43.0%   ← memory-bound

# 3. Memory Bound drills further to L3, to see which cache level / DRAM is stuck
toplev -l3 -- ./app
#   ... L3_Bound.DRAM_Bound: 38%   ← most likely cache misses hitting DRAM
```

Drill down to L3 and you know the bottleneck at "L3 misses hitting DRAM" granularity. But that's still not enough: to actually change anything, you need to know **which instruction is missing**. That takes hardware sampling events with precise addresses.

## The hardware sampling trio: LBR / PEBS / Intel PT

`toplev` tells you "which level is stuck"; landing on "which assembly instruction" relies on the CPU's hardware sampling mechanisms. Modern Intel/AMD CPUs have three of them, each capturing data at a different granularity:

**LBR (Last Branch Record)**: the CPU maintains a ring buffer recording the last few dozen to a few hundred **branch jumps** (from/to address pairs). LBR's strength is capturing **control flow**: where branch mispredictions happen, the call stack (LBR can rebuild the stack without needing a frame pointer), hot loops. The cost: it records only branch jumps, not ordinary memory accesses.

**PEBS (Precise Event-Based Sampling)**: this is what `perf` events "with the `:pp` suffix" lean on. Ordinary sampling is based on the **instruction pointer** and suffers skid (after the event fires and before the interrupt is delivered, the CPU still executes a few more instructions, so the sample point "slides" past the real one and localization is imprecise). PEBS has the CPU save the register state at the moment of the event **precisely** — including the exact instruction address — into the PEBS buffer, with almost no skid. For pinpointing "which load instruction cache-missed," this is a hard requirement:

```bash
# Use an event with the :ppp (precise IP) suffix to localize the exact instruction that cache-missed
perf record -e MEM_LOAD_RETIRED.L3_MISS:ppp -- ./app
perf report   # or perf annotate for assembly-level hits
```

> This is the flip side of entry 16, "PEBS skid," in ch01-03's "measurement pitfalls" table: only a `:ppp` precise event localizes cleanly; an ordinary event skids a few instructions and you end up patching the wrong function.

**Intel PT (Processor Trace)**: goes even further — it **continuously records** the complete control flow (the direction of every branch), so you can **fully reconstruct the execution trace** (though it records no data values). The cost is that it produces huge amounts of data (buffers eat memory), and parsing takes dedicated tools (`perf script`, `libipt`). Intel PT is for the deep-analysis cases of "I need to know exactly which path this run took and how every branch turned"; everyday profiling is fine with PEBS.

AMD's corresponding mechanisms go by different names (AMD uses IBS, Instruction-Based Sampling — similar in spirit to PEBS but implemented differently; the branch-record counterpart is also called LBR). The framework (the TMAM four buckets) is portable, but the underlying event names change with the vendor — something to watch in cross-platform tuning: a perf command tuned on one Intel box needs its event names changed to run on an AMD. `perf list` shows the events available on your machine.

## Bottlenecks migrate: it's iterative, not one-shot

The TMAM workflow has one extremely important property, and newcomers regularly trip over it: **fix one bottleneck and the next one surfaces.**

Say you drill down and find Backend Memory at 60% (L3 misses hitting DRAM). You put real effort into the cache layout, and Backend Memory drops to 20%. You re-measure thinking you're done — and total performance improved only a little, because **Bad Speculation has now climbed to 35%** (it was masked by the memory bottleneck; once memory got fast, branch misprediction became the new bottleneck). That is TMAM's "bottleneck migration": the pipeline's weakest link changes — fix one link, and the next becomes the weakest.

So TMAM is an iterative process, not a one-shot diagnosis:

1. `toplev -l1` to find the current biggest bucket → drill down to localize → fix → back to 1.
2. Each round handles the **current biggest** bucket, until Retiring's share is satisfactory or the remaining buckets are all too small to matter.

This "bottlenecks migrate" property is also why ch01 keeps stressing "measure with the same methodology before and after optimizing." What you think you've fixed may just have moved the bottleneck somewhere else.

Looking back at what TMAM gives us: the four buckets (Retiring the good one / Frontend Bound / Backend Bound split into Memory + Core / Bad Speculation), where whichever bucket's share is high is where the bottleneck lives; the toplev workflow — L1 to pick the main bucket, L2/L3 to drill down into the cache hierarchy, then precise events (`:ppp`) to pin a specific assembly instruction, then fix and iterate again; the division of labor in the hardware sampling trio — LBR captures control flow and stacks, PEBS captures precise memory events (curing skid), Intel PT continuously reconstructs the complete trace; and one discipline running through it all — bottlenecks migrate, so each round treats the current biggest bucket, and you re-measure after every change until Retiring is satisfactory.

TMAM answers "which class of bottleneck," but not yet "which stretch of code, which function." Pinning it down to code is the job of flame graphs — the next article.

## References

- Yasin, *A Top-Down Method for Performance Analysis and Tuning* (2014) — the original TMAM paper
- Andi Kleen's `pmu-tools` (`toplev`) — github.com/andikleen/pmu-tools, the command-line implementation of TMAM
- easyperf.net, *Top-Down performance analysis methodology* (2019-02-09) — an illustrated walkthrough of the toplev workflow
- Bakhvalov, *Performance Analysis and Tuning on Modern CPUs*, Chapter 6, *CPU Features For Performance Analysis* — how LBR / PEBS / Intel PT work at the mechanism level
- Intel, *Optimization Reference Manual*, Appendix B — the official definitions of TMAM and the performance counters
- `perf` documentation: `perf record` / `perf annotate` / the `:pp` precise-event suffix
