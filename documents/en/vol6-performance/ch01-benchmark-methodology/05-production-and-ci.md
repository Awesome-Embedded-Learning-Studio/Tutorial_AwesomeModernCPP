---
chapter: 1
cpp_standard:
- 11
- 17
description: Moving measurement from the dev machine to production and CI — how production telemetry samples real-user data at <1% overhead, why simple thresholds can't stop performance regressions, MongoDB's change-point detection thinking, and the five steps a CI performance system should automate.
difficulty: intermediate
order: 5
platform: host
prerequisites:
- 'Statistics and reporting: turning a distribution into a conclusion'
reading_time_minutes: 5
related:
- Why microbenchmarks lie
- Benchmark methodology reference card
tags:
- host
- cpp-modern
- intermediate
- 优化
- 测试
title: Production measurement and CI performance regression detection
translation:
  source: documents/vol6-performance/ch01-benchmark-methodology/05-production-and-ci.md
  source_hash: 2747614e06c4e7c171633ca3822c2d9cd296aabecf9b15b5fe41108ba09f8e2d
  translated_at: '2026-07-06T00:00:00+00:00'
  engine: manual
  token_count: 2600
---
# Production measurement and CI performance regression detection

## From the dev machine to production

ch01-01 through ch01-04 cover doing microbenchmark A/B comparisons **on the dev machine**: eliminating noise, reporting the median, running hypothesis tests. That discipline answers "is my change direction right". But there are two questions it can't answer:

1. **Did the user actually get faster after launch?** microbenchmark conclusions can't be projected straight onto production (ch01-01 stresses this repeatedly); you have to measure in the production environment.
2. **How do you stop performance from silently degrading as versions pile up?** Large projects change fast, and performance regression bugs leak into production code at surprising speed; relying on a human to watch every time will leak sooner or later.

This article answers those two: how to do production measurement, and how to automatically detect performance regression in CI. The material comes mostly from Bakhvalov's *Performance Analysis and Tuning on Modern CPUs* §2.2 and §2.3.

## Production measurement: accept the noise, use statistics

The biggest difference between the production environment and the dev machine is: **you can't eliminate the noise, and you shouldn't try.** The "lock frequency, pin core, disable Turbo" routine from ch01-03 is all poison in production: if you eliminate the noise, what you measure isn't what the user will experience. The principle of production measurement flips around: **replicate reality + process the noise with statistics**.

A few key points:

- **Shared-infrastructure interference.** On the public cloud your service runs on the same physical machine as someone else (virtualized / containerized), and neighbor processes affect your performance in unpredictable ways. You can't replicate this interference on the dev machine.
- **Telemetry: sample on real user devices.** The big shops increasingly instrument the client side to collect performance data. Bakhvalov cites Netflix's Icarus telemetry service, running on thousands of devices scattered around the world, helping engineers see "how real users perceive performance" — data you can't replicate in the lab.
- **Overhead must be tiny.** The principle of production profiling is "low overhead is job one". Paraphrasing Ren et al. 2010: doing continuous profiling on data-center machines running real traffic, **the acceptable total overhead is under 1%**. So you can only use lightweight methods (low-frequency sampling, only a fraction of machines, short time windows).
- **Statistical methods for quantile metrics.** What production performance cares about isn't "how fast on average", it's "what's the p90 / p99 latency" — the long tail is what kills user experience. LinkedIn's approach (Bakhvalov cites Liu et al. 2019) is to do A/B testing in the production environment with statistical methods, comparing these quantile metrics.

In one sentence: **production measurement = keep the noise + statistical inference**, two languages apart from micro's "eliminate noise + clean comparison".

## CI performance regression detection: why simple thresholds don't work

Iteration is fast, and performance regressions keep leaking in. What stops them?

**First reaction: eyeball the chart.** Don't. People lose focus fast, especially on noisy charts. In Bakhvalov's §2.3 figure, the human eye catches the dip on August 5, but the next few small regressions probably get missed. Plus this is boring daily work, not fit for humans.

**Second reaction: set a threshold, "alert if the drop exceeds X%".** Sounds reasonable; in practice two hard flaws:

1. **The threshold is extremely hard to pick.** Set it low and a pile of pure-noise wiggles trigger alerts; you're chasing air all day. Set it high and real regressions get filtered out. And **small regressions accumulate**: Bakhvalov's example — threshold at 2%, two regressions of 1.5% each both get filtered, two days accumulate to 3%, already over threshold but nobody noticed.
2. **Each test needs its own threshold.** Different benchmarks have different noise levels; one threshold can't be universal. Chromium's LUCI is an example of explicitly configuring thresholds per test — it runs, but the maintenance cost is high.

## Change point analysis

The newer approach is **change point detection**: instead of watching a single threshold, watch when the **distribution** of the whole time series changes. The MongoDB team (Daly et al. 2020) implemented one in their CI system Evergreen, using an algorithm called "E-Divisive means" to automatically find "the point where the distribution changed" in the time series, mark it on the chart, and auto-open a Jira ticket. The nice thing about this approach is that it's robust to noise (it looks for changes in distribution structure, not single jitter), and it doesn't require manually tuning a threshold per test.

Another idea (Bakhvalov cites Alam et al. 2019's AutoPerf): use **hardware performance counters** (PMC, see ch02/ch03) to build a "performance fingerprint" for each function; if the post-change version's fingerprint deviates from the baseline, flag it as anomalous. This catches some complex performance bugs hidden inside parallel programs.

## What a CI performance system should automate

Regardless of whether the underlying mechanism is thresholds or change-point detection, Bakhvalov's §2.3 gives a typical five steps a CI performance system should automate, very practical:

1. **Set up the system under test**
2. **Run the workload**
3. **Report the result**
4. **Decide whether performance has changed**
5. **Visualize**

Plus a few requirements: support both automatic and manual submission, results must be reproducible, and when a regression is found, **open a ticket promptly** — while the code is still hot and the author hasn't moved on to the next task, regressions are easiest to fix; drag it out two weeks and the author has forgotten what they changed, and fixing it is twice the work for half the result.

## Tying this chapter together

That completes the ch01 loop:

- ch01-01~04: **microbenchmark** A/B on the dev machine, eliminating noise, reporting the median, running hypothesis tests. Answers "is the change direction right".
- ch01-05 (this article): **production measurement + CI regression**. Production replicates real noise + statistical quantiles; CI uses change-point detection to catch regressions automatically. Answers "did it really get faster in production, and has it silently degraded".

The two can't be mixed: don't back production with micro numbers, and don't use micro's "eliminate noise" routine in the production environment. They're the two ends of the measurement spectrum, and the transition in the middle is handled by macro benchmarks (representative of real load but controlled), which is the business of later chapters.

## References

- Bakhvalov, D. *Performance Analysis and Tuning on Modern CPUs* §2.2 *Measuring Performance In Production*, §2.3 *Automated Detection of Performance Regressions* (Netflix Icarus, Ren 2010, Liu 2019, MongoDB Evergreen / Daly 2020, AutoPerf / Alam 2019 all come from here).
- Chromium LUCI performance dashboard docs.
- This volume's ch01-01 (the micro vs macro boundary), ch01-04 (statistical methods).
