---
chapter: 1
cpp_standard:
- 11
- 17
description: The concrete countermeasure to ch01-01's third trick (noise) — 16 environment pitfalls that distort performance numbers, grouped by frequency / cache / scheduling / tooling, each with "why it distorts + the command to avoid it", plus a perf-env-check.sh one-shot health-check script.
difficulty: intermediate
order: 3
platform: host
prerequisites:
- How to write a credible microbenchmark
reading_time_minutes: 6
related:
- Why microbenchmarks lie
- Statistics and reporting
tags:
- host
- cpp-modern
- intermediate
- 优化
- 测试
title: 'Measurement pitfalls and environment readiness: a 16-item checklist'
translation:
  source: documents/vol6-performance/ch01-benchmark-methodology/03-pitfalls-and-env.md
  source_hash: 11d9a7e19b5b64e087271cb00f8917a25ca479236662ef634e7c0f20d65e1a25
  translated_at: '2026-07-06T00:00:00+00:00'
  engine: manual
  token_count: 3000
---
# Measurement pitfalls and environment readiness: a 16-item checklist

## Why you need a checklist

ch01-01 covered the third microbenchmark deception: system noise drowning the signal. That article told you "why there's noise"; this one tells you "exactly how to turn it off". The 16 items below are the environment pitfalls most often tripped when doing credible microbenchmarking on Linux. Each is laid out as "**pitfall → why it distorts → how to avoid it**", most with a command you can copy straight.

But before we start, repeat the most important boundary from ch01-01: **these noise-elimination techniques should only be used when doing relative A/B comparisons.** If what you're evaluating is "how fast the user actually feels", you instead **replicate** the real environment (keep the noise, keep DFS, keep the neighbor processes) and then process that noise with statistics — that's the job of ch01-05, production measurement. This page is for the microbenchmark scenario of "I want to cleanly compare two implementations".

## The 16 pitfalls

For memorability, grouped by nature into four buckets.

### Group 1: frequency and power (the biggest swing source)

| # | Pitfall | Why it distorts | Avoidance |
|---|---|---|---|
| 1 | **CPU frequency scaling (DVFS)** | governor=ondemand means frequency floats with load; GBench even prints `***WARNING*** CPU scaling enabled` on startup | `sudo cpupower frequency-set -g performance` to lock to top frequency |
| 2 | **Turbo Boost** | Single-core burst high frequency; cold start differs from steady state, drops once it heats up | Disable Turbo in BIOS; or lock the frequency; measure steady state and warm up first (that's what warmup means) |

Leave these two alone and the same code differing 10% between two runs is normal. Especially bad on laptops (cooling-limited, Turbo constantly going in and out).

### Group 2: cache, memory, and address translation

| # | Pitfall | Why it distorts | Avoidance |
|---|---|---|---|
| 3 | **Cold start vs steady state** | First access misses cache (goes to DRAM), subsequent hits cache, 10–100× difference | The framework's estimation phase already pre-warms; to measure cold start, use `posix_fadvise(fd, POSIX_FADV_DONTNEED)` to drop the pages |
| 4 | **Page faults** | First touch triggers a soft page fault (microsecond-scale), inflating a single operation tens of times over | `mlockall(MCL_CURRENT \| MCL_FUTURE)` to lock pages; or touch every page first |
| 9 | **NUMA** | On multi-socket machines cross-node memory access latency doubles or quadruples; "memory bandwidth" becomes "interconnect bandwidth" | `numactl --cpunodebind=0 --membind=0 ./bench` to bind threads and memory to the same node |
| 15 | **ASLR / code layout** | Different PIE base addresses make instruction cache (icache) and branch predictor alignment jitter by 10–20%; also affects "memory layout bias" (Mytkowicz 2009) | For fine microarchitectural timing add `-no-pie`; to remove layout bias use random interleaving |

### Group 3: scheduling and interference

| # | Pitfall | Why it distorts | Avoidance |
|---|---|---|---|
| 5 | **Context switches / interrupts** | Getting scheduled away produces long-tail outlier samples | `taskset -c <core>` to pin; use the median for statistics (not the mean) |
| 8 | **CPU pinning** | Threads migrating across cores, cache cold every time | `taskset -c 3 ./bench` (pick a core, don't let the OS jiggle it) |
| 10 | **SMT / hyperthread contention** | The sibling thread on the same physical core eats execution units | Disable hyperthreading in BIOS; or taskset only physical cores (use one of every two sibling cores) |
| 11 | **Timer resolution** | `clock()` measuring nanoseconds is all noise (not enough resolution) | `std::chrono::steady_clock` (see ch00-02); or `perf stat` to look at cycles |

### Group 4: tooling posture and statistics

| # | Pitfall | Why it distorts | Avoidance |
|---|---|---|---|
| 6 | **Dead code gets optimized away** | Result unused → DCE deletes the loop (see ch01-01's `foo()`) | `DoNotOptimize` / `doNotOptimizeAway`; note it **does not prevent the expression itself from being computed away** (ch01-02) |
| 7 | **No release + no debug info** | `-O0` performance numbers are meaningless; pure `-O2` without `-g` can't do source annotation | Use `RelWithDebInfo` (`-O2 -g`) uniformly; add `-fno-omit-frame-pointer` for profiling (otherwise the stack breaks, flame graph explodes) |
| 12 | **Mean vs median** | Microbenchmarks are right-skewed (long tail), mean gets dragged up | Report median + IQR; GBench `Repetitions` + `ReportAggregatesOnly` (ch01-02) |
| 13 | **Too few samples** | Confidence interval so wide you can't tell A from B | ≥30 samples; report 95% CI; use the Mann-Whitney U test for A/B significance (ch01-04) |
| 14 | **Run-to-run instability** | Environment not fixed, drift across runs | Take the most stable of ≥3 runs; `perf stat -r 5` repeats for you |
| 16 | **PEBS skid** | A sampled event "skids" a few instructions before landing on the real instruction | Use events with the `:pp` / `:ppp` (precise IP) suffix, e.g. `MEM_LOAD_RETIRED.L3_MISS:ppp` |

The 16 look like a lot, but the core is one sentence: **control everything you can (frequency, core, memory layout), really consume the result that should be consumed (`DoNotOptimize`), and treat the numbers as a distribution (median + multiple rounds).** The rest depends on the scenario — for micro, do as many as possible; for production evaluation, don't do them (replicate reality).

## One-shot health check: `perf-env-check.sh`

Checking all these by hand before every measurement is annoying, so we compress it into a script. It **only checks, never modifies** (things like changing governor or turning off Turbo need sudo and are left for you to decide), and prints whatever problems it finds:

```bash
#!/usr/bin/env bash
# perf-env-check.sh —— credible microbenchmark environment health check (check only, no modify)
set -u

ok()   { printf "  ✓ %s\n" "$1"; }
warn() { printf "  ⚠ %s\n" "$1"; }

echo "=== CPU governor (should=performance) ==="
g=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null)
[ "$g" = performance ] && ok "governor=performance" || warn "governor=$g (DVFS will float). Fix: sudo cpupower frequency-set -g performance"

echo "=== Turbo Boost (Intel) ==="
if [ -f /sys/devices/system/cpu/intel_pstate/no_turbo ]; then
  nt=$(cat /sys/devices/system/cpu/intel_pstate/no_turbo)
  [ "$nt" = 1 ] && ok "Turbo off" || warn "Turbo on (no_turbo=$nt), cold/hot start numbers will differ"
else
  echo "  · Not intel_pstate or no such interface, skipping (set in BIOS)"
fi

echo "=== perf_event_paranoid (<=1 to sample nicely) ==="
p=$(cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null)
[ "${p:-3}" -le 1 ] && ok "perf_event_paranoid=$p" || warn "=$p (perf restricted). Fix: sudo sysctl -w kernel.perf_event_paranoid=1"

echo "=== NUMA topology (only matters on multi-socket) ==="
command -v numactl >/dev/null && numactl --hardware 2>/dev/null | grep -E "^available|node [0-9]+ cpus" | head -4 || warn "no numactl"

echo "=== CPU affinity (should explicitly pin one core, don't let the OS jiggle) ==="
cpu=$(grep Cpus_allowed_list /proc/self/status 2>/dev/null | awk '{print $2}')
n=$(nproc 2>/dev/null)
echo "  Cpus_allowed_list=$cpu (nproc=$n) → if not pinned, taskset -c <some core> ./bench (don't pick core 0, often busy with system interrupts)"

echo "=== ASLR (turn off for fine microarchitectural timing) ==="
aslr=$(cat /proc/sys/kernel/randomize_va_space 2>/dev/null)
echo "  randomize_va_space=$aslr (2=full; for fine icache/branch timing: sudo sysctl -w kernel.randomize_va_space=0)"
```

Save it as `perf-env-check.sh` and run `bash perf-env-check.sh` to see what your environment is still missing. The full script also lives under `code/volumn_codes/vol6-performance/ch01/`.

## Which scenarios call for which items

| Scenario | Do these (eliminate noise) | Don't do these |
|---|---|---|
| **microbenchmark A/B comparison** | 1/2/4/5/8/9/10/15, as many as possible; you want a clean signal-to-noise ratio | Don't project the conclusion straight onto production |
| **Evaluating production performance** | **Almost none**, replicate the real environment (keep DFS, neighbors, ASLR) | Don't turn off noise sources, or you're not measuring what users will experience |
| **Profiling for hotspots** | 7 (`-fno-omit-frame-pointer`), 16 (`:pp`) | Finding hotspots is sampling under real load anyway |

This table is the concrete landing of ch01-01's "the hand that makes micro clean is the hand that makes it deceptive": **the same set of techniques is medicine in the micro scenario and poison in the production scenario.** Getting this balance right matters more than memorizing the 16 commands.

## References

- easyperf.net: *How to get consistent results when benchmarking on Linux* (one of the direct sources for this checklist).
- Brendan Gregg: [Linux Performance](https://www.brendangregg.com/linuxperf.html) (perf / task placement / NUMA).
- Bakhvalov, D. *Performance Analysis and Tuning on Modern CPUs* §2.1 *Noise In Modern Systems*.
- This volume's ch01-01 (noise classification), ch01-02 (`DoNotOptimize` / `Repetitions` / `UseRealTime`).
