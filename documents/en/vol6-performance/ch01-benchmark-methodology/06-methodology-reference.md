---
chapter: 1
cpp_standard:
- 11
- 17
description: The quick-reference page for all of ch01 — every later performance article and Lab cites it up front. One card condensing environment readiness, how to write a credible microbenchmark, reporting and comparison, the micro vs production/CI boundary, and a perf cheat sheet.
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
  translated_at: '2026-09-26T05:46:19+00:00'
  engine: anthropic
  token_count: 2600
---
# Benchmark methodology reference card

> This is the **quick-reference page** for all of ch01. Every performance article and every performance Lab later in this volume opens by referring back to the rules laid down here, the way vol5 threads TSan through all of concurrency correctness. This is not a tutorial (that's ch01-01 through ch01-05); it's a reference card, the kind you tape to the wall.

## §0 Premise (one sentence)

**Performance is a random variable, not a number.** What you measure is always a distribution, so you sample + do statistical inference — you never run once and jot down a single number. (Details in ch01-01)

## §1 Before measuring: environment readiness (micro A/B scenario)

| Must-do | Command / approach |
|---|---|
| Lock the CPU governor | `sudo cpupower frequency-set -g performance` |
| Disable Turbo | BIOS, or lock the frequency |
| Pin to a core | `taskset -c <some core> ./bench` (don't pick core 0) |
| Bind to a NUMA node | `numactl --cpunodebind=0 --membind=0 ./bench` |
| perf available | `sudo sysctl -w kernel.perf_event_paranoid=1` |
| Compile options | `RelWithDebInfo` (`-O2 -g`); for profiling add `-fno-omit-frame-pointer` |
| Health check | `bash perf-env-check.sh` (see ch01-03; checks only, changes nothing) |

> ⚠️ **Do these only in the micro A/B scenario.** When evaluating production performance, **do none of them**: replicate reality instead (keep DFS, neighbors, ASLR) and handle the noise with statistics. See ch01-05.

## §2 Writing a credible microbenchmark

| Point | How |
|---|---|
| Use a framework, don't hand-roll | Google Benchmark as the workhorse, nanobench as a lightweight supplement (instant feedback when we cover microarchitecture) |
| Defend against DCE | `benchmark::DoNotOptimize(x)` pins the result to memory/register; **note: it does not stop `x` itself from being constant-propagated away**, so the input must be runtime data |
| Force writes to land in memory | `benchmark::ClobberMemory()` as the backstop |
| Sweep parameters | `->RangeMultiplier(2)->Range(8, 8<<10)`; `state.SetComplexityN(...)` auto-fits big-O |
| Repeat and aggregate | `->Repetitions(3)->ReportAggregatesOnly(true)` reports mean/median/stddev/cv |
| Wall clock | `->UseRealTime()` (mandatory for multithreaded runs) |

Details in ch01-02 (with a complete runnable example and its real output).

## §3 Reporting and comparison

- **Always report**: the median, IQR or 95% CI, cv, sample count, and an environment snapshot (kernel / CPU / governor / `perf_event_paranoid`).
- **Never report**: a single-run mean (performance data is right-skewed; the long tail drags the mean off).
- **A/B**: same environment, same binary (change exactly one thing), many repetitions (N ≥ 30); for hypothesis testing **default to Mann-Whitney U** (non-parametric — performance data is almost never normal); reach for a t-test only with a prior case for normality.
- **Report the effect size**: give the complete statement — "12% faster (95% CI [10%, 14%], p<0.01)" — not just a bare p value. Statistically significant ≠ engineering-meaningful.
- **A bimodal distribution is a signal, not noise**: two behaviors got mixed together (cache hit/miss, lock contention); split them apart and measure each separately.

Details in ch01-04.

## §4 micro vs production/CI (the boundary — do not mix)

| Scenario | What you do | Output |
|---|---|---|
| **micro A/B** | Eliminate noise, compare two implementations cleanly | "Is the change direction right" |
| **Production measurement** | Replicate real noise, telemetry samples quantiles (p90/p99), statistical A/B | "Did users actually get faster" |
| **CI regression** | Change-point detection (E-Divisive) / PMC fingerprints (AutoPerf), auto-open tickets | "Has anything silently regressed" |

**No converting across scenarios**: micro's 30% improvement does not carry proportionally into production. See ch01-01, ch01-05.

## §5 Citation rules for vol6 articles / Labs

- Every performance article and every performance Lab declares at its opening "this article follows the ch01 measurement methodology".
- When reporting performance numbers, attach an **environment snapshot** + **statistics** (median / cv / repetition count); never report single-run raw values.
- Whenever A/B is involved, use the §3 routine (same environment + same binary + Mann-Whitney + effect size).

## §6 perf cheat sheet

```bash
# Basic counters (health check: watch IPC, cache misses, branch misses)
perf stat -r 5 ./bench
perf stat -e cycles,instructions,cache-misses,branch-misses ./bench

# Sampling profile (find hotspots; make sure to use -fno-omit-frame-pointer or dwarf unwinding)
perf record -F 99 -g --call-graph dwarf -- ./bench
perf report                      # browse interactively
perf script | stackcollapse-perf.pl | flamegraph.pl > out.svg   # flame graph

# Microarchitectural attribution (covered in depth in ch03)
toplev -l3 taskset -c 0 ./bench  # TMAM four-bucket drill-down, needs pmu-tools
```

The full workflow for flame graphs, TMAM, and `toplev` belongs to ch03 (attribution methodology); here we only hand you the entry point.

## References (tutorial articles)

- ch01-01 "Why microbenchmarks lie"
- ch01-02 "How to write a credible microbenchmark"
- ch01-03 "Measurement pitfalls and environment readiness: a 16-item checklist"
- ch01-04 "Statistics and reporting: turning a distribution into a conclusion"
- ch01-05 "Production measurement and CI performance regression detection"
- Bakhvalov, D., *Performance Analysis and Tuning on Modern CPUs*, Chapter 2
- Google Benchmark [user_guide](https://github.com/google/benchmark/blob/main/docs/user_guide.md), Brendan Gregg [perf / FlameGraphs](https://www.brendangregg.com/linuxperf.html)
