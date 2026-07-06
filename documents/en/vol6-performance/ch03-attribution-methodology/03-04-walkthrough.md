---
chapter: 3
cpp_standard:
- 17
description: Chain ch03's first three articles — USE / Roofline / TMAM / flame graphs — into one complete workflow. Using a weighted-dot-product case (working set sitting on the L3 boundary, bandwidth-bound), walk step by step with the four tools to localize "Backend Memory Bound, hugging the DRAM bandwidth slope, the optimization is AoS to SoA to drop pad traffic," and stress the iterative nature of bottleneck migration
difficulty: advanced
order: 4
platform: host
prerequisites:
- The USE method and the Roofline model
- TMAM's four buckets and hardware sampling
- Flame graphs, the perf workflow, and COZ / eBPF
reading_time_minutes: 8
related:
- Backend memory bottlenecks — cache-friendly, AoS/SoA, and prefetch
- Loop and compute optimization — code motion, unrolling, and multiple accumulators
tags:
- host
- cpp-modern
- advanced
- 优化
- 工程实践
title: "Attribution in practice: from a slow program to the bottleneck"
translation:
  source: documents/vol6-performance/ch03-attribution-methodology/03-04-walkthrough.md
  source_hash: 5740bcf58b7f119931f64030a9f1dba29192d172d043d0daf4d85369f38c4714
  translated_at: '2026-07-06T00:00:00+00:00'
  engine: manual
  token_count: 3100
---
# Attribution in practice: from a slow program to the bottleneck

## How the four tools chain together

In the first three articles of ch03 we learned four tools: USE (system-wide view), Roofline (compute vs bandwidth), TMAM (which pipeline stage), and flame graphs + COZ (land on code). But when you actually get your hands on it, you don't run all four end to end — that's too slow. Their proper relationship is a funnel, filtering coarse-to-fine:

```text
slow program
  │
  ├─ 1. USE sweeps the system: is it even on the CPU? (paging? disk full? network?)
  │     └─ After ruling out system-level causes, confirm the bottleneck is on CPU compute
  │
  ├─ 2. Roofline qualitative call: compute-bound or bandwidth-bound? (sets the optimization direction)
  │
  ├─ 3. TMAM four buckets: which pipeline stage? (Frontend/Backend Memory/Backend Core/Bad Spec)
  │     └─ Drill to cache level, use precise events to localize to an assembly instruction
  │
  └─ 4. Flame graph: which function is this slow code in? (confirm which line to change)
        └─ COZ adds: how big is the total payoff of fixing this function? (rank optimization priorities)
```

This article walks the full workflow. To be clear up front: **I did not run perf/toplev on this WSL2 machine** (perf isn't installed locally). So the profiler outputs below are derived from ch02's actually-measured data (those are real local runs) plus standard cases from Bakhvalov/easyPerfect, **extrapolated** as "what you'd see if you ran them." What can be measured locally (arithmetic intensity, raw timings, cache behavior) is labeled "measured"; specific profiler outputs are labeled "extrapolated/quoted," never faked as run.

## Scenario: a ridiculously slow dot product

Say you wrote a particle-physics simulation whose core is a "weighted dot product": for N particles, compute `result = Σ w[i] * x[i] * y[i]`. N = 1 million (16 bytes per particle, a 16 MB working set, sitting right on the local L3 capacity boundary), and a single run takes about 0.9 ms (measured locally, order of magnitude). You want to know whether it has room to optimize and where it's stuck — **work through it with the attribution workflow**.

The code looks like this (simplified):

```cpp
struct Particle { float w, x, y, pad; };   // AoS: 16 bytes per particle
float weighted_dot(const std::vector<Particle>& ps) {
    float acc = 0.0f;
    for (size_t i = 0; i < ps.size(); ++i)
        acc += ps[i].w * ps[i].x * ps[i].y;   // each iteration touches three fields
    return acc;
}
```

## Step 1: USE sweep — confirm it's a CPU problem first

Before doing anything, spend two minutes on a system sweep to rule out "the problem isn't even on the CPU":

```bash
vmstat 1     # %us+%sy (user+system CPU), r column (run queue), si/so (paging)
free -m      # has memory filled up to paging?
iostat -xz 1 # disk (this program doesn't read disk; should all be zero)
```

If you see `si/so > 0` (paging), the "slow" might just be the system paging because memory is short, having nothing to do with your algorithm — add memory first. If CPU utilization maxes out one core while other resources sit idle, confirm the bottleneck is on CPU compute and **move to step 2**.

This step looks trivial, but it can save you in five minutes from the tragedy of "spent a day optimizing the algorithm and it turned out the disk was full." That's the value of USE.

## Step 2: Roofline qualitative call — compute or bandwidth?

Now you've confirmed a CPU compute bottleneck, but still need to split: can't compute fast enough, or can't feed data fast enough? Compute the arithmetic intensity (this is locally computable, no profiler needed):

Per loop iteration:

- Ops: 2 multiplies + 1 multiply + 1 add = 3 floating-point ops (strictly, `w*x*y+acc` is 2 mul + 1 add = 3 FLOP).
- Memory: reading `w`, `x`, `y` (three floats) = 12 bytes (the `pad` field comes along in the same cacheline but doesn't count as "useful traffic"; count useful for now).
- **Arithmetic intensity ≈ 3 FLOP / 12 B = 0.25 FLOP/byte**.

From 03-01: the roofline-point AI for a chip in the 5800H class is around ~20 FLOP/byte (8-core AVX2 FMA peak ~900 GFLOPS / ~40 GB/s). 0.25 is far below the roofline point, so this kernel is **unambiguously memory-bandwidth-bound**, riding the bandwidth slope. The optimization direction is immediately clear: **cut memory traffic, don't squeeze SIMD lanes** (even with SIMD fully loaded, the bandwidth bottleneck is unchanged, just spinning).

This step cost only a pen and already ruled out the wrong path of "add SIMD," which would have been at least half a day of wasted work. That's the leverage of Roofline.

## Step 3: TMAM drill — which cache level is missing?

Roofline said "bandwidth-bound," but at which level? L2 hit but L3 miss? Or L3 also blown through to DRAM? That's `toplev`'s job (the output below is extrapolated from local cache parameters plus the Bakhvalov case):

```bash
toplev -l1 -- ./weighted_dot
#   Frontend_Bound:       8.0%
#   Backend_Bound:       62.0%   ← main bucket, matches Roofline's "bandwidth-bound"
#   Bad_Speculation:      5.0%
#   Retiring:            25.0%

toplev -l3 -- ./weighted_dot
#   Backend_Bound.Memory_Bound.L3_Bound.DRAM_Bound: 45%  ← even L3 is blown through to DRAM
```

"DRAM Bound" tells us: data didn't hit L3 (16 MB), it went to main memory. But note — **the weighted dot product is a streaming single-pass scan** (each element is visited once, with zero temporal reuse), so **there is no L3 capacity thrash here** (thrash requires repeatedly-revisited elements evicting each other; a streaming scan has no revisits). The working set of 16 MB sitting on/slightly exceeding the L3 boundary, the real cause is that it's **riding the DRAM bandwidth slope** — see ch02-01's memory mountain, where throughput falls to the low end of the bandwidth slope once the working set exceeds L3. **The root cause is bandwidth-bound** (not capacity thrash).

Drilling down to assembly, use a precise event to confirm which load is missing:

```bash
perf record -e MEM_LOAD_RETIRED.L3_MISS:ppp -- ./weighted_dot
perf annotate
# Highlights: the three movss reads of ps[i].w/x/y in the loop are the miss heavyweights
```

Localization complete: bottleneck = Backend Memory Bound, DRAM-level miss, and the misses are produced by the field loads in the loop. Now you can change it.

## Step 4: flame-graph confirmation + the fix

In this example the flame graph isn't actually critical (there's only one loop), but in a larger program it can tell you "is this 45% of DRAM misses spread across 5 functions or all concentrated in `weighted_dot`." If spread, you fix each; if concentrated, one fix does it. Assume the flame graph confirms everything is in `weighted_dot`, concentrated.

How to fix? In a bandwidth-bound scenario, the key is **cutting useless traffic**. The problem is the **AoS layout**: `Particle{w,x,y,(pad)}` packs the three useful fields together with the unused `pad`, and as the scan walks the array each iteration, `pad` eats a quarter of the bandwidth for nothing. Switch to **SoA** (full mechanism in ch04-01), with the three fields each contiguous and no longer carrying `pad`:

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

After the change, **go back to step 2 and re-measure** (iterate!), and you find the single-run time drops to ~0.7 ms (measured locally, order of magnitude; SoA drops the 25% pad traffic). Look at toplev again and Backend Memory's share is visibly down, Retiring up — much better.

## Don't celebrate too soon: bottlenecks migrate

This is the part of TMAM that gets newcomers. You fix Backend Memory down to 20%, **re-measure total time**, and you might find only a small speedup — because now **Bad Speculation has climbed** (it was hidden under the memory bottleneck). Or Retiring went up, but **Frontend Bound** rose because the code layout got worse. Every fix has to **go back to step 2 and look at the four buckets again**, handle the new biggest bucket, until Retiring is satisfactory or the remaining buckets are all too small to bother.

This "bottleneck migration" property is why attribution is iterative, not "run the profiler once, fix once." It's also why ch01 keeps saying "measure before and after with the same methodology" — what you think you've fixed may just have moved the short board to a new spot.

## Wrap-up: COZ for priorities

If this program has other functions (not just `weighted_dot`), how do you decide **which to fix first**? The flame graph sorts by time, but "the function that takes the most time" isn't necessarily "the function with the biggest optimization payoff." That's where COZ helps: it draws a "virtual speedup → total speedup" curve for each function, and the steepest one is the most worth optimizing. When you have several candidate bottlenecks and a limited budget, COZ's priorities are more reliable than the flame graph.

## The value of this workflow

Walk it once and you'll find that the heart of attribution isn't "some magic tool," it's **narrowing the battlefield step by step through the funnel**:

1. USE (2 minutes) rules out system-level causes and confirms it's CPU.
2. Roofline (a pen) decides compute vs bandwidth and sets the direction.
3. TMAM (a few toplev runs) drills down to the pipeline stage and cache level, with precise events localizing to an instruction.
4. Flame graph confirms the function; COZ ranks priorities.
5. Fix, then **iterate** — bottlenecks migrate.

Each step costs more than the one before it (in time, tooling, and intrusiveness), but each step also narrows the battlefield for the next. Skip the early steps and jump straight to a flame graph, and you'll get lost in a 50-function program; skip Roofline and jump straight to SIMD, and you might optimize the wrong direction. This coarse-to-fine discipline is the precondition for ch04's optimize-by-bottleneck-site to prescribe the right medicine.

With this article, ch03's attribution methodology is done. From the next article on, we officially enter **ch04, tuning by bottleneck site**, walking each of the four buckets — Backend Memory / Backend Core / Bad Speculation / Frontend — through how to treat it.

## References

- ch03-01 The USE method and the Roofline model (this volume)
- ch03-02 TMAM's four buckets and hardware sampling (this volume)
- ch03-03 Flame graphs, the perf workflow, and COZ / eBPF (this volume)
- Bakhvalov, *Performance Analysis and Tuning on Modern CPUs* — full case walkthroughs in ch6–11 (TMAM/CPU features/Cache/Memory/Core/Frontend/multithreading; the book has 11 chapters total).
- ch02-01 Memory hierarchy and the latency ladder (this volume; the source for the memory-mountain measurements of throughput when the working set crosses the L3 boundary).
