---
chapter: 3
cpp_standard:
- 17
description: 'Chains the USE / Roofline / TMAM / flame-graph toolkit from ch03''s first
  three articles into one complete workflow. A weighted-dot-product case (working
  set sitting right on the L3 boundary, bandwidth-bound) walks the four tools step
  by step to the verdict — Backend Memory Bound, hugging the DRAM bandwidth slope,
  with the fix being AoS→SoA to drop pad traffic — and stresses the iterative nature
  of bottleneck migration'
difficulty: advanced
order: 4
platform: host
prerequisites:
- 'The USE method and the Roofline model: system-wide first, then compute vs bandwidth'
- 'The four TMAM buckets and hardware sampling: LBR / PEBS / Intel PT'
- 'Flame graphs, the perf workflow, and COZ / eBPF'
reading_time_minutes: 8
related:
- 'Backend memory bottlenecks: cache-friendly, AoS/SoA, and prefetch'
- 'Loop and compute optimization: code motion, eliminating memory references, and multiple accumulators'
tags:
- host
- cpp-modern
- advanced
- 优化
- 工程实践
title: 'Attribution in practice: from a slow program to a pinpointed bottleneck'
translation:
  source: documents/vol6-performance/ch03-attribution-methodology/03-04-walkthrough.md
  source_hash: 5740bcf58b7f119931f64030a9f1dba29192d172d043d0daf4d85369f38c4714
  translated_at: '2026-09-26T06:10:06+00:00'
  engine: anthropic
  token_count: 5100
---
# Attribution in practice: from a slow program to a pinpointed bottleneck

## How the four toolkits chain together

In the first three ch03 articles we learned four toolkits: USE (the system-wide view), Roofline (compute vs bandwidth), TMAM (which pipeline stage to attribute to), and flame graphs + COZ (down to the code). But when you actually sit down to work, you don't run all four every time — that would be far too slow. Their correct relationship is a funnel, filtering from coarse to fine:

```text
A slow program
  │
  ├─ 1. USE sweeps the system: is the problem even on the CPU? (paging? disk full? network?)
  │     └─ once system-level causes are ruled out, confirm the bottleneck is CPU compute
  │
  ├─ 2. Roofline, the qualitative call: compute-bound or bandwidth-bound? (sets the broad optimization direction)
  │
  ├─ 3. TMAM four buckets: which pipeline stage is the bottleneck? (Frontend/Backend Memory/Backend Core/Bad Spec)
  │     └─ drill into the cache hierarchy; precise events pin it down to an assembly instruction
  │
  └─ 4. Flame graph: which function holds this slow stretch of code? (confirms which line to change)
        └─ COZ on top: how big is the overall payoff of fixing this function? (ranks optimization priorities)
```

This article walks the full workflow once. To be clear up front: **we did not run perf/toplev on the WSL2 machine this article is being written on** (perf isn't installed locally). So the profiler outputs below are "what you would see if you ran them," extrapolated from data already measured in ch02 (those runs are real, on this machine) plus the standard cases from Bakhvalov/easyperf. Everything measurable on this machine (arithmetic intensity, raw runtimes, cache behavior) is labeled "measured"; the specific profiler outputs are labeled "extrapolated/cited" — never faked as if they had been run.

## Scenario: a ridiculously slow dot product

Say you wrote a particle physics simulation whose core is a weighted dot product: for N particles, compute `result = Σ w[i] * x[i] * y[i]`. N = 1 million (16 bytes per particle, a 16 MB working set, sitting exactly on this machine's L3 capacity boundary), and a single pass takes about 0.9 ms (order of magnitude, measured on this machine). You want to know whether there is room to optimize and where it is stuck, so you run the attribution workflow over it.

The code looks like this (simplified):

```cpp
struct Particle { float w, x, y, pad; };   // AoS: 16 bytes per particle
float weighted_dot(const std::vector<Particle>& ps) {
    float acc = 0.0f;
    for (size_t i = 0; i < ps.size(); ++i)
        acc += ps[i].w * ps[i].x * ps[i].y;   // touches three fields per iteration
    return acc;
}
```

## Step 1: USE sweeps the system — first confirm it is a CPU problem

Before touching anything, spend two minutes scanning the system to rule out "the problem isn't on the CPU at all":

```bash
vmstat 1     # watch %us+%sy (user+system CPU), the r column (run queue), si/so (paging)
free -m      # check whether memory has filled up to the point of paging
iostat -xz 1 # check the disk (this program reads no disk; it should be all idle)
```

If you see `si/so > 0` (paging is happening), the "slowness" may simply be memory shortage forcing the system to page — nothing to do with your algorithm; add memory first. If CPU utilization saturates one core while the other resources sit idle, the bottleneck is confirmed to be CPU compute — **on to step 2**.

This step looks trivial, but it can save you, within five minutes, from the tragedy of "spent a whole day optimizing the algorithm, and the disk was just full." That is exactly where the value of USE lies.

## Step 2: Roofline characterization — compute-bound or bandwidth-bound

Now the bottleneck is confirmed to be CPU compute, but there is still a fork in the road: can't compute fast enough, or can't feed data fast enough? Work out the arithmetic intensity (this one is computable locally, no profiler needed):

Per loop iteration:

- Ops: 2 multiplies + 1 multiply + 1 add — count 3 floating-point ops (strictly speaking, `w*x*y+acc` is 2 multiplies + 1 add = 3 FLOP).
- Memory traffic: reading the three floats `w`, `x`, `y` = 12 bytes (the `pad` field rides in on the same cacheline, but it doesn't count as useful traffic — count only the useful bytes for now).
- **Arithmetic intensity ≈ 3 FLOP / 12 B = 0.25 FLOP/byte**.

From 03-01: a chip in the 5800H class has its ridge-point AI on the order of ~20 FLOP/byte (8-core AVX2 FMA peak ~900 GFLOPS / ~40 GB/s). 0.25 sits far below the ridge point, so this kernel is **unambiguously memory-bandwidth-bound**, running flat against the bandwidth slope. The optimization direction is immediately clear: **cut memory traffic; don't go squeeze SIMD lanes** (even with SIMD fully loaded, the bandwidth bottleneck is unchanged — the vector units would just be spinning).

This step cost nothing more than a pen, yet it already eliminated the "add SIMD" path — at least half a day of wasted work. That is the leverage of Roofline.

## Step 3: TMAM drill-down — which cache level is missing

Roofline said "bandwidth-bound," but at which level is the bandwidth stuck? Hits in L2 but misses in L3? Or punching through L3 as well, all the way to DRAM? That is `toplev`'s job (the output below is extrapolated from this machine's cache parameters plus the Bakhvalov case):

```bash
toplev -l1 -- ./weighted_dot
#   Frontend_Bound:       8.0%
#   Backend_Bound:       62.0%   ← the dominant bucket, matching Roofline's "bandwidth-bound" call
#   Bad_Speculation:      5.0%
#   Retiring:            25.0%

toplev -l3 -- ./weighted_dot
#   Backend_Bound.Memory_Bound.L3_Bound.DRAM_Bound: 45%  ← even L3 is punched through, down to DRAM
```

"DRAM Bound" tells us: the data isn't hitting L3 (16 MB) and is going to main memory. But note: **the weighted dot product is a streaming, single-pass scan** (each element is touched exactly once, with zero temporal reuse), so **there is no L3 capacity thrashing here** (thrashing requires repeatedly re-visited elements evicting each other; a streaming scan has no re-visits). With the working set of 16 MB sitting on / slightly past the L3 boundary, the real cause is that it **runs flat against the DRAM bandwidth slope** — recall ch02-01's memory mountain: once the working set exceeds L3, throughput lands at the low end of the bandwidth slope. **The root cause is bandwidth-bound** (not capacity thrashing).

Drill down to the assembly and use a precise event to confirm which load is missing:

```bash
perf record -e MEM_LOAD_RETIRED.L3_MISS:ppp -- ./weighted_dot
perf annotate
# Highlights show: the three movss loads reading ps[i].w/x/y in the loop are the heaviest miss offenders
```

Localization complete: bottleneck = Backend Memory Bound, DRAM-level misses, produced by the field loads in the loop. Now we can change it.

## Step 4: flame graph confirmation + the fix

In this example the flame graph isn't actually critical (there is only one loop), but in a large program it can tell you whether "this 45% of DRAM misses is spread across 5 functions or all concentrated in the one function `weighted_dot`." If spread out, you have to fix them one by one; if concentrated, a single fix suffices. Here, assume the flame graph confirms it is all in `weighted_dot`, concentrated.

How to fix it? In a bandwidth-bound scenario, the key is **cutting useless traffic**. The problem is the **AoS layout**: `Particle{w,x,y,(pad)}` squeezes the three fields we use together with the unused `pad`, and every time the loop scans down the array, `pad` burns a quarter of the bandwidth for nothing. Switch to **SoA** (the full mechanism is in ch04-01): the three fields live in their own contiguous arrays and no longer carry `pad`:

```cpp
struct Particles {
    std::vector<float> w, x, y;   // three separate arrays
};
float weighted_dot(const Particles& ps) {
    float acc = 0.0f;
    for (size_t i = 0; i < ps.w.size(); ++i)
        acc += ps.w[i] * ps.x[i] * ps.y[i];
    return acc;
}
```

After the change, **go back to step 2 and re-measure first** (iterate!). A single pass drops to ~0.7 ms (order of magnitude, measured on this machine; SoA removes the 25% of traffic that `pad` was stealing). Look at toplev again: Backend Memory's share drops noticeably and Retiring climbs — much better.

## Don't celebrate yet: bottlenecks migrate

This is the part of TMAM that traps newcomers the most. You grind Backend Memory down to 20%, **re-measure the total time**, and may find it got only a little faster — because now **Bad Speculation has climbed** (it was previously masked by the memory bottleneck). Or Retiring went up, but **Frontend Bound** is rearing its head because the code layout got worse. After every fix you must **go back to step 2 and look at the four buckets again**, attack the new biggest bucket, and keep going until Retiring is satisfactory or the remaining buckets are all too small to be worth it.

This "bottleneck migration" property is what makes attribution iterative, not "run the profiler once, change once." It is also why ch01 keeps repeating "measure before and after with the same methodology" — what you believe you fixed may only have moved the short plank to a different spot.

## Wrapping up: COZ ranks the priorities

If this program has other functions besides `weighted_dot`, how do you decide **which one to fix first**? The flame graph sorts by time spent, but "the function that takes the most time" is not necessarily "the function where optimization pays the most." That is where COZ helps: for each function it draws a "virtual speedup → overall speedup" curve, and the steepest slope marks the most worthwhile target. When you have several candidate bottlenecks and a finite budget, the priorities COZ gives are more reliable than the flame graph's.

## What this workflow is worth

Walk it once end to end and you will see: the heart of attribution is not "some magical tool," it is narrowing the battlefield through the funnel, step by step:

1. USE (2 minutes) rules out system-level problems and confirms it is the CPU.
2. Roofline (a pen) decides compute vs bandwidth and sets the broad direction.
3. TMAM (a few toplev runs) drills down to the pipeline stage + cache level; precise events pin it to an instruction.
4. The flame graph confirms the function; COZ ranks the priorities.
5. Fix, then iterate — bottlenecks migrate.

Each step is more expensive than the one before it (time spent, tooling required, and intrusiveness all increase), yet each step narrows the battlefield for the next. Skip the early steps and jump straight to flame graphs, and you will get lost in a 50-function program; skip Roofline and jump straight to SIMD changes, and you may optimize in the wrong direction. This coarse-to-fine discipline is the precondition for ch04's optimize-by-bottleneck-site advice to actually hit the right spot.

With this article, ch03's attribution methodology is complete. From the next article on, we formally enter **ch04, optimizing by bottleneck site**, taking the four buckets — Backend Memory / Backend Core / Bad Speculation / Frontend — one at a time and covering how to treat each.

## References

- ch03-01 The USE method and the Roofline model (this volume)
- ch03-02 The four TMAM buckets and hardware sampling (this volume)
- ch03-03 Flame graphs, the perf workflow, and COZ / eBPF (this volume)
- Bakhvalov, *Performance Analysis and Tuning on Modern CPUs* — the complete case walkthroughs live in ch6–11 (TMAM/CPU features/Cache/Memory/Core/Frontend/multithreading; 11 chapters in total)
- ch02-01 The memory-hierarchy latency ladder (this volume; source of the memory-mountain measurements of throughput as the working set crosses the L3 boundary)
