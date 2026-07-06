---
chapter: 1
cpp_standard:
- 11
- 17
description: The quick-reference page for all of ch01. Later performance articles and Labs reference it from their opening. One card condenses environment readiness, credible-microbenchmark writing, reporting and comparison, the micro vs production/CI boundary, and a perf cheat sheet.
difficulty: intermediate
order: 6
platform: host
prerequisites:
- How to write a credible microbenchmark
reading_time_minutes: 4
related:
- Why microbenchmarks lie
- 'Measurement pitfalls and environment readiness: a 16-item checklist'
- 'Statistics and reporting: turning a distribution into a conclusion'
- Production measurement and CI performance regression detection
tags:
- host
- cpp-modern
- intermediate
- 优化
- 测试
title: Benchmark methodology reference card
translation:
  source: documents/vol6-performance/ch01-benchmark-methodology/06-methodology-reference.md
  source_hash: 0f7fcb6674e2c52d1d4cb29efbf97dd6b3e1fa4b6842147dcbc9f1c300a446ce
  translated_at: '2026-07-06T00:00:00+00:00'
  engine: manual
  token_count: 2200
---
# Benchmark methodology reference card

> This is the **quick-reference page** for all of ch01. Every later performance article and every performance Lab in this volume opens by referencing the discipline taught here, the way vol5 runs TSan through all of concurrency correctness. This isn't a tutorial (the tutorial is ch01-01 through ch01-05); it's a reference card, the kind you tape to the wall.

## §0 Premise (one sentence)

**Performance is a random variable, not a number.** What you measure is always a distribution; you sample + do statistical inference, you don't run once and jot down a number. (See ch01-01.)

## §1 Before measuring: environment readiness (micro A/B scenario)

| Must-do | Command / how |
|---|---|
| Lock CPU governor | `sudo cpupower frequency-set -g performance` |
| Disable Turbo | BIOS, or lock the frequency |
| Pin a core | `taskset -c <some core> ./bench` (don't pick 0) |
| NUMA-bind a node | `numactl --cpunodebind=0 --membind=0 ./bench` |
| perf usable | `sudo sysctl -w kernel.perf_event_paranoid=1` |
| Compile flags | `RelWithDebInfo` (`-O2 -g`); for profiling add `-fno-omit-frame-pointer` |
| Health check | `bash perf-env-check.sh` (see ch01-03, check only, no modify) |

> ⚠️ **Only do these in the micro A/B scenario.** When evaluating production performance **do none of them**: you want to replicate reality (keep DFS, neighbors, ASLR) and process the noise with statistics. See ch01-05.

## §2 Writing a credible microbenchmark

| Point | How |
|---|---|
| Use a framework, don't hand-roll | Google Benchmark as main workhorse, nanobench as lightweight supplement (instant feedback when teaching microarchitecture) |
| Prevent DCE | `benchmark::DoNotOptimize(x)` pins the result to memory/register; **note: does not prevent `x` itself from being computed away by constant propagation**, so the input must be runtime data |
| Force writes to land | `benchmark::ClobberMemory()` as the safety net |
| Sweep parameters | `->RangeMultiplier(2)->Range(8, 8<<10)`; `state.SetComplexityN(...)` auto-fits big-O |
| Repeat + aggregate | `->Repetitions(3)->ReportAggregatesOnly(true)` reports mean/median/stddev/cv |
| Wall-clock | `->UseRealTime()` (mandatory for multithreading) |

See ch01-02 (with a complete runnable example and real output).

## §3 Reporting and comparison

- **Must report**: median, IQR or 95% CI, cv, sample count, environment snapshot (kernel / CPU / governor / `perf_event_paranoid`).
- **Forbidden**: single-pass mean (performance data is right-skewed; the long tail biases the mean).
- **A/B**: same environment, same binary (only one thing changed), multiple repetitions (N ≥ 30); for hypothesis testing **default to Mann-Whitney U** (non-parametric; performance data is almost never normal); only use t-test with a prior normality check.
- **Report effect size**: the full form "12% faster (95% CI [10%, 14%], p<0.01)", not just a p value. Statistically significant ≠ engineering-meaningful.
- **A bimodal distribution is a signal, not noise**: you've mixed two behaviors (cache hit/miss, lock contention); split them and measure separately.

See ch01-04.

## §4 micro vs production/CI (boundary, do not mix)

| Scenario | What you do | Output |
|---|---|---|
| **micro A/B** | Eliminate noise, cleanly compare two implementations | "Is the change direction right" |
| **Production measurement** | Replicate real noise, telemetry samples quantiles (p90/p99), statistical A/B | "Did the user actually get faster" |
| **CI regression** | Change-point detection (E-Divisive) / PMC fingerprint (AutoPerf), auto-open tickets | "Has it silently degraded" |

**Forbidden to convert across scenarios**: micro's 30% improvement doesn't carry proportionally to production. See ch01-01, ch01-05.

## §5 How vol6 articles / Labs reference this

- Every performance article and every performance Lab declares at its opening "this article follows the ch01 measurement methodology".
- When reporting performance numbers, attach an **environment snapshot** + **statistics** (median / cv / repetition count); don't report single-pass raw values.
- For A/B, use the §3 routine (same environment + same binary + Mann-Whitney + effect size).

## §6 perf cheat sheet

```bash
# Basic counts (health check: look at IPC, cache miss, branch miss)
perf stat -r 5 ./bench
perf stat -e cycles,instructions,cache-misses,branch-misses ./bench

# Sampling profile (find hotspots; must use -fno-omit-frame-pointer or dwarf for stack)
perf record -F 99 -g --call-graph dwarf -- ./bench
perf report                      # interactive
perf script | stackcollapse-perf.pl | flamegraph.pl > out.svg   # flame graph

# Microarchitectural attribution (detailed in ch03)
toplev -l3 taskset -c 0 ./bench  # TMAM four-bucket drill-down, needs pmu-tools
```

The full workflow for flame graphs, TMAM, and `toplev` is the business of ch03 (attribution methodology); this is only the entry point.

## References (tutorial articles)

- ch01-01 "Why microbenchmarks lie"
- ch01-02 "How to write a credible microbenchmark"
- ch01-03 "Measurement pitfalls and environment readiness: a 16-item checklist"
- ch01-04 "Statistics and reporting: turning a distribution into a conclusion"
- ch01-05 "Production measurement and CI performance regression detection"
- Bakhvalov, D. *Performance Analysis and Tuning on Modern CPUs*, Chapter 2.
- Google Benchmark [user_guide](https://github.com/google/benchmark/blob/main/docs/user_guide.md), Brendan Gregg [perf / FlameGraphs](https://www.brendangregg.com/linuxperf.html).
