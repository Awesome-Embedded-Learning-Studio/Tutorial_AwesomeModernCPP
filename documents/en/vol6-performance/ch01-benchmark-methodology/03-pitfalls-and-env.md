---
chapter: 1
cpp_standard:
- 11
- 17
description: Concrete countermeasures against the third deception from ch01-01 (noise)
  — 16 environment pitfalls that distort performance numbers, grouped by frequency,
  cache, scheduling, and tooling, each with the reason it distorts plus the command
  to avoid it, rounded off by the one-shot perf-env-check.sh health-check script
difficulty: intermediate
order: 3
platform: host
prerequisites:
- How to write a credible microbenchmark
reading_time_minutes: 6
related:
- Why microbenchmarks lie
- 'Statistics and reporting: turning a distribution into a conclusion'
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
  translated_at: '2026-09-26T05:39:07+00:00'
  engine: anthropic
  token_count: 1700
---
# Measurement pitfalls and environment readiness: a 16-item checklist

## Why we need a checklist

ch01-01 covered the third way a microbenchmark can lie to you: system noise drowning out the signal. That article answered "why is there noise"; this one answers "how exactly do I turn it off". The 16 items below are the environmental traps you are most likely to step on when running credible microbenchmarks on Linux. Each follows the "**pitfall → why it distorts → how to avoid it**" pattern, and most come with a command you can copy straight into your terminal.

But before we dive in, let's repeat the single most important boundary from ch01-01: **these noise-elimination techniques should only be used when you are making a relative A/B comparison.** If what you are evaluating is "how fast does this actually feel to the user", you should instead **replicate** the real environment (keep the noise, keep the DFS, keep the neighboring processes) and then handle it with statistical methods — that is the job of production measurement in ch01-05. This page serves the microbenchmark scenario of "I want to compare two implementations cleanly".

## The 16 pitfalls

To make them easier to remember, they are grouped into four buckets by nature.

### Group 1: Frequency and power (the biggest source of swings in your numbers)

| # | Pitfall | Why it distorts | How to avoid |
|---|---|---|---|
| 1 | **CPU frequency scaling (DVFS)** | governor=ondemand leaves the frequency floating freely, and GBench even prints `***WARNING*** CPU scaling enabled` at startup | `sudo cpupower frequency-set -g performance` to lock the top frequency |
| 2 | **Turbo Boost** | A single core bursts to a high frequency; cold start differs from steady state, and the boost drops once the part heats up | Disable Turbo in the BIOS; or lock the frequency; when measuring steady state, warm it up thoroughly first (this is exactly what warmup is about) |

If you leave these two unsolved, two runs of the same code differing by 10% is perfectly normal. It is especially severe on laptops (limited cooling, so Turbo keeps entering and exiting).

### Group 2: Caches, memory, and address translation

| # | Pitfall | Why it distorts | How to avoid |
|---|---|---|---|
| 3 | **Cold start vs steady state** | The first access misses the cache (it goes to DRAM) while later accesses hit — a 10–100x gap | The framework's estimation phase has already warmed things up; if you want to measure the cold path, drop the pages with `posix_fadvise(fd, POSIX_FADV_DONTNEED)` |
| 4 | **Page faults** | The first touch of a page triggers a soft fault (microsecond-scale), inflating a single operation by tens of times | Lock the pages with `mlockall(MCL_CURRENT \| MCL_FUTURE)`; or touch every page once in advance |
| 9 | **NUMA** | On multi-socket machines a cross-node memory access costs 2–4x more, so what you measured as "memory bandwidth" quietly becomes "interconnect bandwidth" | `numactl --cpunodebind=0 --membind=0 ./bench` to bind threads and memory to the same node |
| 15 | **ASLR / code layout** | A different PIE base address makes instruction-cache (icache) and branch-predictor alignment jitter by 10–20%; it also drives "memory layout bias" (Mytkowicz 2009) | Add `-no-pie` for fine-grained microarchitectural measurements; to cancel layout bias, use random interleaving |

### Group 3: Scheduling and interference

| # | Pitfall | Why it distorts | How to avoid |
|---|---|---|---|
| 5 | **Context switches / interrupts** | Your code gets scheduled away, producing long-tail outlier samples | Pin to one core with `taskset -c <core>`; report the median (not the mean) |
| 8 | **CPU pinning** | Threads migrate across cores and the cache goes cold every time | `taskset -c 3 ./bench` (pick one core and do not let the OS wander around) |
| 10 | **SMT / hyperthread contention** | The other thread on the same physical core eats your execution units | Disable hyperthreading in the BIOS; or pin to physical cores only with taskset (use one of every two sibling cores) |
| 11 | **Timer resolution** | `clock()` on nanosecond-scale work is pure noise (not enough resolution) | `std::chrono::steady_clock` (see ch00-02); or read cycles from `perf stat` |

### Group 4: Tool usage and statistics

| # | Pitfall | Why it distorts | How to avoid |
|---|---|---|---|
| 6 | **Dead code optimized away** | The result is never consumed → DCE deletes the loop (see `foo()` in ch01-01) | `DoNotOptimize` / `doNotOptimizeAway`; note that it does **not** stop the expression itself from being folded away (ch01-02) |
| 7 | **No release build plus debug info** | Performance numbers from `-O0` are meaningless; plain `-O2` without `-g` cannot be source-annotated | Standardize on `RelWithDebInfo` (`-O2 -g`); for profiling, add `-fno-omit-frame-pointer` (otherwise stacks break and the flame graph explodes) |
| 12 | **Mean vs median** | Microbenchmark results are right-skewed (long tail), dragging the mean upward | Report the median + IQR; GBench `Repetitions` + `ReportAggregatesOnly` (ch01-02) |
| 13 | **Too few samples** | Confidence intervals so wide that A and B are indistinguishable | ≥30 samples; report the 95% CI; judge A/B significance with the Mann-Whitney U test (ch01-04) |
| 14 | **Unstable across repeated runs** | The environment is not pinned down, so results drift between runs | Take ≥3 runs and keep the steadiest; `perf stat -r 5` does the repetition for you |
| 16 | **PEBS skid** | A sampled event "skids" a few instructions before it lands on the instruction actually responsible | Use events with the `:pp` / `:ppp` (precise IP) suffix, such as `MEM_LOAD_RETIRED.L3_MISS:ppp` |

Sixteen items looks like a lot, but the core is one sentence: **control everything controllable (frequency, cores, memory layout), really consume every result that should be consumed (`DoNotOptimize`), and treat the numbers as a distribution (median + multiple repeated rounds).** The rest depends on the scenario: for micro work, do as many as you can; for production evaluation, do none of them (replicate reality instead).

## One-shot health check: `perf-env-check.sh`

Checking all of these by hand before every measurement session is tedious, so we compressed it into one script. It **checks only, never modifies** (actions that require sudo, such as changing the governor or disabling Turbo, are left to your own judgment) and prints out whatever problems it finds:

```bash
#!/usr/bin/env bash
# perf-env-check.sh — environment health check for credible microbenchmarks (check only, never modify)
set -u

ok()   { printf "  ✓ %s\n" "$1"; }
warn() { printf "  ⚠ %s\n" "$1"; }

echo "=== CPU governor(应=performance)==="
g=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null)
[ "$g" = performance ] && ok "governor=performance" || warn "governor=$g(DVFS 会浮动)。修:sudo cpupower frequency-set -g performance"

echo "=== Turbo Boost(Intel)==="
if [ -f /sys/devices/system/cpu/intel_pstate/no_turbo ]; then
  nt=$(cat /sys/devices/system/cpu/intel_pstate/no_turbo)
  [ "$nt" = 1 ] && ok "Turbo 已关" || warn "Turbo 开着(no_turbo=$nt),冷热启动数字会差"
else
  echo "  · 非 intel_pstate 或无该接口,跳过(可在 BIOS 设)"
fi

echo "=== perf_event_paranoid(<=1 才好采样)==="
p=$(cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null)
[ "${p:-3}" -le 1 ] && ok "perf_event_paranoid=$p" || warn "=$p(perf 受限)。修:sudo sysctl -w kernel.perf_event_paranoid=1"

echo "=== NUMA 拓扑(多 socket 才在意)==="
command -v numactl >/dev/null && numactl --hardware 2>/dev/null | grep -E "^available|node [0-9]+ cpus" | head -4 || warn "无 numactl"

echo "=== CPU 亲和性(应明确绑一个核,别让 OS 晃)==="
cpu=$(grep Cpus_allowed_list /proc/self/status 2>/dev/null | awk '{print $2}')
n=$(nproc 2>/dev/null)
echo "  Cpus_allowed_list=$cpu (nproc=$n) → 没绑核就 taskset -c <某个核> ./bench(别挑 0 号核,常被系统中断占用)"

echo "=== ASLR(微架构精细测时应关)==="
aslr=$(cat /proc/sys/kernel/randomize_va_space 2>/dev/null)
echo "  randomize_va_space=$aslr(2=全开;精细 icache/分支测时可 sudo sysctl -w kernel.randomize_va_space=0)"
```

Save it as `perf-env-check.sh`; one run of `bash perf-env-check.sh` tells you what your environment is still missing. The complete script also lives under `code/volumn_codes/vol6-performance/ch01/`.

## Which measures fit which scenario

| Scenario | Do these (eliminate noise) | What not to do |
|---|---|---|
| **Microbenchmark A/B comparison** | 1/2/4/5/8/9/10/15 — do as many as possible; what you want is a clean signal-to-noise ratio | Do not extrapolate the conclusions straight to production |
| **Evaluating production performance** | **Do almost none of them** — replicate the real environment (keep DFS, neighbors, ASLR) | Do not turn off noise sources, or you are no longer measuring what users will actually experience |
| **Profiling to find hotspots** | 7 (`-fno-omit-frame-pointer`), 16 (`:pp`) | Hotspot hunting is by definition sampling under real load |

This table is where the ch01-01 line "the very hand that makes a micro clean is the hand that makes it lie to you" becomes concrete: **the same set of techniques is the antidote in a microbenchmark scenario and poison in a production scenario.** Getting that dosage right matters more than memorizing 16 commands.

## References

- easyperf.net: *How to get consistent results when benchmarking on Linux* (one of the direct sources of this checklist)
- Brendan Gregg: [Linux Performance](https://www.brendangregg.com/linuxperf.html) (perf / task placement / NUMA)
- Bakhvalov, D., *Performance Analysis and Tuning on Modern CPUs*, §2.1 *Noise In Modern Systems*
- This volume's ch01-01 (the taxonomy of noise) and ch01-02 (`DoNotOptimize` / `Repetitions` / `UseRealTime`)
