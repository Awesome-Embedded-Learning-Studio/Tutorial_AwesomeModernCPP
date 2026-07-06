---
chapter: 3
cpp_standard:
- 17
description: TMAM (Top-Down Microarchitecture Analysis) sorts pipeline slots into four buckets — Retiring / Frontend Bound / Backend Bound / Bad Speculation — telling you which pipeline stage the bottleneck is in. This article covers how the four buckets are defined, the toplev workflow, and the hardware sampling mechanisms behind them — what data LBR / PEBS / Intel PT each give you
difficulty: advanced
order: 2
platform: host
prerequisites:
- The USE method and the Roofline model
- Pipeline, ILP, and branch prediction
reading_time_minutes: 7
related:
- Flame graphs, the perf workflow, and COZ / eBPF
- Frontend optimization — code layout, PGO, BOLT
tags:
- host
- cpp-modern
- advanced
- 优化
- 工程实践
title: "TMAM's four buckets and hardware sampling: LBR / PEBS / Intel PT"
translation:
  source: documents/vol6-performance/ch03-attribution-methodology/03-02-tmam-and-hw-sampling.md
  source_hash: 13e8a6ee3c48422f8159f7ca845611a8be3b9913dcc2eaaf395990df9bf559c8
  translated_at: '2026-07-06T00:00:00+00:00'
  engine: manual
  token_count: 2900
---
# TMAM's four buckets and hardware sampling: LBR / PEBS / Intel PT

## Attributing the bottleneck to a pipeline stage

Last article's Roofline can tell compute-bound from bandwidth-bound, but "compute" and "bandwidth" are both still too coarse. A CPU pipeline has many stages — fetch, decode, execute, memory access, branch prediction — and "not enough compute" could mean decode can't keep up (frontend), or the execution units are waiting on data (backend), or branch prediction keeps getting it wrong and flushing the pipeline (bad speculation). The fixes for those three are completely different.

**TMAM (Top-Down Microarchitecture Analysis Method)** is the framework Intel came up with to answer this question (Yasin, *A Top-Down Method for Performance Analysis and Tuning*, 2014). It takes the slots the pipeline can allocate each cycle and sorts them **by their final fate** into four buckets. Whichever bucket's share is abnormally high tells you which pipeline stage the bottleneck is in. Andi Kleen later turned this framework into the one-command `pmu-tools/toplev`, the workhorse of modern CPU performance analysis.

## The four buckets: four fates for a slot

The CPU frontend tries to "allocate" some number of slots per cycle (slot count = pipeline width). Those slots have only four possible fates:

| Bucket | Meaning | High share means | Typical fix (maps to ch04) |
|---|---|---|---|
| **Retiring** | the slot retired into a real useful instruction | **the higher the better** (ideal) | already well-structured, keep going |
| **Frontend Bound** | the slot stalled because the frontend (fetch/decode) couldn't keep up | icache miss / iTLB miss / code bloat | code layout, PGO, BOLT (ch04-07) |
| **Backend Bound** | the slot stalled at the backend — the execution unit was **waiting on data** (memory) or **waiting on a port** (core) | cache miss / data dependency / execution-port contention | cache optimization, SIMD, breaking dependency chains (ch04-01/02/03) |
| **Bad Speculation** | the slot was wasted on a **mispredicted speculative path** (branch prediction failure, flushed) | unpredictable branches | branchless, predication (ch04-06) |

How to read the four buckets: **Retiring is the good bucket, the other three are bad buckets, and whichever has an abnormally high share is the current bottleneck.** A well-tuned numerical-compute kernel can hit 50%–70% Retiring (SIMD fully loaded); if it's only 20% with Backend Memory at 60%, you should be fixing cache, not adding SIMD.

Backend Bound splits further into two branches: **Backend Memory Bound** (waiting on data: cache misses, bandwidth) and **Backend Core Bound** (waiting on an execution port: division, a long-latency dependency chain). That split is extremely useful — it maps directly to the two routes in ch04, "fix memory" or "fix compute."

> Boundary note: the TMAM four buckets are an attribution framework; they tell you "which class the bottleneck lands in." How to actually fix it (vectorize, cut traffic, branchless) is ch04's job. Don't expand on optimization detail in the attribution chapter — that's ch04's home turf.

## The toplev workflow: drill down layer by layer

`toplev` (the Python tool in `pmu-tools` that wraps Intel's TMAM performance counters) works by drilling down level by level. A typical three steps:

```bash
# 1. Look at the L1 four buckets first, pick the main battlefield
toplev -l1 -- ./app
# Sample output (quoted from easyPerfect.net, not run locally):
#   Frontend_Bound:      12.5%   ← normal
#   Backend_Bound:       58.0%   ← main bucket!
#   Bad_Speculation:      8.3%
#   Retiring:            21.2%

# 2. Backend is the main bucket, drill to L2 to see Memory vs Core
toplev -l2 -- ./app
#   Backend_Bound.Core_Bound:   15.0%
#   Backend_Bound.Memory_Bound: 43.0%   ← memory-bound

# 3. Memory Bound drills further to L3, see which cache level / DRAM
toplev -l3 -- ./app
#   ... L3_Bound.DRAM_Bound: 38%   ← likely cache misses hitting DRAM
```

By L3 you know "the bottleneck is an L3 miss hitting DRAM" at that granularity. But that's still not enough — you still need to know **which instruction is missing** before you can change anything. That's what **hardware sampling events with precise addresses** are for.

## The hardware-sampling trio: LBR / PEBS / Intel PT

`toplev` tells you "which level is stuck," but to land on "which assembly instruction," you rely on the CPU's hardware sampling mechanisms. Modern Intel/AMD CPUs have three, each giving you a different granularity of data:

**LBR (Last Branch Record)**: the CPU keeps a ring buffer recording the last few dozen to few hundred **branch jumps** (from/to address pairs). LBR's strength is capturing **control flow** — where branch prediction failed, the call stack (LBR rebuilds the stack without needing a frame pointer), hot loops. The cost: it only records branch jumps, not ordinary memory accesses.

**PEBS (Precise Event-Based Sampling)**: this is what `perf` events with the `:pp` suffix lean on. Ordinary sampling is based on the **instruction pointer**, which suffers from skid (between the event happening and the interrupt being delivered, the CPU executes a few more instructions, so the sample point "slides" past the real one and localization is imprecise). PEBS makes the CPU **precisely** save the register state at the event time (including the precise instruction address) into the PEBS buffer, with almost no skid. This is essential for "which load instruction cache-missed":

```bash
# Use an event with the :ppp (precise IP) suffix to localize the exact instruction that cache-missed
perf record -e MEM_LOAD_RETIRED.L3_MISS:ppp -- ./app
perf report   # or perf annotate for assembly-level hits
```

> This is the flip side of item 16 in ch01-03's "measurement pitfalls" table, "PEBS skid": the `:ppp` precise event is what localizes cleanly; an ordinary event skids a few instructions and you end up patching the wrong function.

**Intel PT (Processor Trace)**: even more aggressive — it **continuously records** the full control flow (the direction of every branch) and can **completely reconstruct the execution trace** (though it doesn't record data values). The cost is a lot of data (buffers eat memory) and parsing needs dedicated tools (`perf script`, `libipt`). Intel PT is for "I need to know exactly which path this run took and how every branch turned" type of deep analysis; ordinary profiling is fine with PEBS.

AMD's equivalents have different names (AMD uses IBS, Instruction-Based Sampling, similar in spirit to PEBS but implemented differently; the branch-record equivalent is also called LBR). The framework (the TMAM four buckets) is portable; the underlying event names change with the vendor, which is something to watch for in cross-platform tuning — a perf command tuned on an Intel box needs its event names changed for AMD. `perf list` enumerates the events available on your machine.

## Bottlenecks migrate: it's iterative, not one-shot

The TMAM workflow has one extremely important property, and newcomers get burned on it regularly: **fixing one bottleneck reveals the next.**

Say you drill down and find Backend Memory at 60% (L3 misses hitting DRAM), you put real work into fixing the cache layout, and Backend Memory drops to 20%. You re-measure thinking you're done — only to find total performance moved only a little, because now **Bad Speculation climbed to 35%** (it was hidden under the memory bottleneck; once memory got fast, branch misprediction became the new bottleneck). That's TMAM's "bottleneck migration": the short board of the pipeline changes; you fix one and the next becomes the new short board.

So TMAM is an iterative process, not a one-shot diagnosis:

1. `toplev -l1` to find the current biggest bucket → drill down to localize → fix → back to 1.
2. Each round handles the **current biggest** bucket, until Retiring's share is satisfactory or the remaining buckets are all too small to bother.

This "bottlenecks migrate" property is also why ch01 keeps stressing "measure before and after with the same methodology." What you think you've fixed may just have moved the bottleneck somewhere else.

Looking back at what TMAM gives us: four buckets (Retiring the good one / Frontend Bound / Backend Bound split into Memory + Core / Bad Speculation), and whichever bucket's share is high is where the bottleneck is; the toplev workflow is L1 to pick the main bucket, L2/L3 to drill into cache levels, then precise events (`:ppp`) to localize to a specific assembly instruction, then fix and iterate; the hardware-sampling trio divides the labor, with LBR capturing control flow and stacks, PEBS capturing precise memory events (curing skid), and Intel PT continuously reconstructing the full trace; and one running discipline: bottlenecks migrate, so each round treats the current biggest bucket, you re-measure after every change, until Retiring is satisfactory.

TMAM answers "which class of bottleneck," but it hasn't yet answered "which stretch of code, which function." Localizing to code is the job of flame graphs, the next article.

## References

- Yasin, *A Top-Down Method for Performance Analysis and Tuning* (2014) — the original TMAM paper.
- Andi Kleen's `pmu-tools` (`toplev`) — github.com/andikleen/pmu-tools, the command-line implementation of TMAM.
- easyPerfect.net, *Top-Down performance analysis methodology* (2019-02-09) — an illustrated tutorial on the toplev workflow.
- Bakhvalov, *Performance Analysis and Tuning on Modern CPUs*, Chapter 6 *CPU Features For Performance Analysis* — the mechanism-level treatment of LBR / PEBS / Intel PT.
- Intel, *Optimization Reference Manual*, Appendix B — the official definition of TMAM and performance counters.
- `perf` documentation: `perf record` / `perf annotate` / the `:pp` precise-event suffix.
