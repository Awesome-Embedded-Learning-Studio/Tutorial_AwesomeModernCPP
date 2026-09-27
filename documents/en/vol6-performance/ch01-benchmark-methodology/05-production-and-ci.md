---
chapter: 1
cpp_standard:
- 11
- 17
description: Moving measurement off the dev machine and into production and CI — how production telemetry samples real-user data at <1% overhead, why simple thresholds cannot stop performance regressions, MongoDB's change point analysis approach, and the five steps a CI performance system should automate.
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
  translated_at: '2026-09-26T05:47:48+00:00'
  engine: anthropic
  token_count: 3300
---
# Production measurement and CI performance regression detection

## From the dev machine to production

ch01-01 through ch01-04 were about doing microbenchmark A/B comparisons **on the dev machine**: eliminate the noise, report the median, run a hypothesis test. That discipline answers one question — "is my change heading in the right direction". But there are two more questions it cannot answer:

1. **Did users actually get faster after launch?** A microbenchmark's conclusions cannot be projected straight onto production (ch01-01 hammered this repeatedly), so you have to measure in the production environment itself.
2. **How do you stop performance from silently degrading as releases pile up?** Big projects change fast, and performance regression bugs leak into production code at an astonishing rate. If you rely on a human watching every single time, regressions will slip through sooner or later.

This article answers both: how to do production measurement, and how to detect performance regressions automatically in CI. The material comes mostly from Bakhvalov's *Performance Analysis and Tuning on Modern CPUs*, §2.2 and §2.3.

## Production measurement: accept the noise, use statistics

The biggest difference between production and the dev machine: **you cannot eliminate the noise, and you shouldn't try**. The whole "lock the frequency, pin the core, disable Turbo" routine from ch01-03 is pure poison in production: eliminate the noise, and what you measure is no longer what users will experience. The principle of production measurement flips around: **replicate reality + handle the noise with statistics**.

A few key points:

- **Shared-infrastructure interference.** On a public cloud, your service runs on the same physical machine as other people's workloads (virtualized / containerized), and neighbor processes affect your performance in unpredictable ways. You cannot replicate this interference on a dev machine.
- **Telemetry: sample on real users' devices.** The big players increasingly instrument the client side to collect performance data. Bakhvalov cites Netflix's Icarus telemetry service, which runs on thousands of devices scattered around the globe and lets engineers see "how real users perceive performance" — data you cannot replicate in a lab.
- **The overhead must be extremely low.** The first principle of production profiling is "low overhead is job one". Paraphrasing the 2010 paper by Ren et al.: for continuous profiling on data-center machines running real traffic, **the acceptable total overhead is under 1%**. So you are limited to lightweight methods (low-frequency sampling, only a fraction of the machines, short time windows).
- **Statistical methods for quantile metrics.** Production performance is not about "how fast on average" but "what the p90 / p99 latency is" — the long tail is what kills the user experience. LinkedIn's approach (Bakhvalov cites Liu et al. 2019) is to run A/B tests in the production environment with statistical methods, comparing exactly these quantile metrics.

In one sentence: **production measurement = keep the noise + statistical inference** — a different language from micro's "eliminate the noise + clean comparison".

## CI performance regression detection: why simple thresholds don't work

Product iteration is fast, and performance regressions keep leaking in. What do you block them with?

**First instinct: have a human eyeball the chart.** Don't. Humans lose focus fast, especially on noisy charts. In Bakhvalov's §2.3 figure, the eye catches the dip on August 5, but the few small regressions after it most likely get missed. And this is boring, daily work — not a job for a human.

**Second instinct: set a threshold — "alert if the drop exceeds X%".** Sounds reasonable; in practice it has two hard flaws:

1. **The threshold is brutally hard to pick.** Set it low and a pile of pure-noise wiggles trigger alerts, so you spend your days chasing nothing; set it high and real regressions get filtered out. And **small regressions accumulate**: in Bakhvalov's example, with the threshold at 2%, two regressions of 1.5% each both slip through the filter and accumulate to 3% over two days — already past the threshold, and still nobody reacts.
2. **Every test needs its own threshold.** Different benchmarks have different noise levels; a single threshold cannot be universal. Chromium's LUCI is an example of explicitly configuring a threshold per test — it works, but the maintenance cost is high.

## Change point analysis

The newer approach is **change point analysis**: instead of staring at a single threshold, you watch for when the **distribution** of the whole time series changes. The MongoDB team (Daly et al. 2020) built one into their CI system Evergreen: an algorithm called "E-Divisive means" automatically finds "the points where the distribution changed" in the time series, marks them on the chart, and opens a Jira ticket automatically. The beauty of this approach is that it is robust to noise (it looks for changes in the structure of the distribution, not single jitters), and it needs no manually tuned threshold per test.

Another line of thinking (Bakhvalov cites Alam et al. 2019's AutoPerf): use **hardware performance counters** (PMCs; see ch02/ch03) to build a "performance fingerprint" for each function, and flag an anomaly when the changed version's fingerprint drifts from the baseline. This catches some of the complex performance bugs hiding inside parallel programs.

## What a CI performance system should automate

Whether the underlying mechanism is thresholds or change point analysis, Bakhvalov's §2.3 lays out five steps a typical CI performance system should automate — very practical:

1. **Set up the system under test**
2. **Run the workload**
3. **Report the results**
4. **Decide whether performance has changed**
5. **Visualize**

Plus a few requirements: support both automatic and manual submission, results must be reproducible, and when a regression is found, **open the ticket promptly**. A regression is easiest to fix while the code is still warm and the author hasn't moved on to the next task; drag it out two weeks and the author has forgotten what they even changed — twice the work for half the result.

## Tying this chapter together

With that, the ch01 loop is complete:

- ch01-01~04: **microbenchmarks** — A/B comparisons on the dev machine, eliminating noise, reporting medians, running hypothesis tests. Answers "is the change heading in the right direction".
- ch01-05 (this article): **production measurement + CI regression detection**. Production replicates real noise and reasons about quantiles statistically; CI catches regressions automatically with change point analysis. Answers "did it really get faster in production, and has anything silently degraded".

The two must not be mixed: don't use micro numbers to vouch for production, and don't apply micro's "eliminate the noise" routine inside the production environment. They are the two ends of the measurement spectrum; the bridge in the middle is the macro benchmark (representative of real load, but controlled) — the business of later chapters.

## References

- Bakhvalov, D., *Performance Analysis and Tuning on Modern CPUs*, §2.2 *Measuring Performance In Production*, §2.3 *Automated Detection of Performance Regressions* (the sources for Netflix Icarus, Ren 2010, Liu 2019, MongoDB Evergreen / Daly 2020, and AutoPerf / Alam 2019)
- Chromium LUCI performance dashboard documentation
- This volume's ch01-01 (the micro vs macro boundary) and ch01-04 (statistical methods)
