---
chapter: 1
cpp_standard:
- 11
- 17
description: What to do after you've got a pile of numbers — why performance data reports the median + confidence interval rather than a single mean, why it's almost never normal so Mann-Whitney beats t-test, the right posture for A/B comparison, and why you can't back macro conclusions with micro numbers.
difficulty: intermediate
order: 4
platform: host
prerequisites:
- How to write a credible microbenchmark
- 'Measurement pitfalls and environment readiness: a 16-item checklist'
reading_time_minutes: 6
related:
- Production measurement and CI performance regression
tags:
- host
- cpp-modern
- intermediate
- 优化
- 测试
title: 'Statistics and reporting: turning a distribution into a conclusion'
translation:
  source: documents/vol6-performance/ch01-benchmark-methodology/04-statistics-and-reporting.md
  source_hash: a013bb919b59df4eba7daefd9f726144825177be853b048c5158f1bb297ddec4
  translated_at: '2026-07-06T00:00:00+00:00'
  engine: manual
  token_count: 2800
---
# Statistics and reporting: turning a distribution into a conclusion

## You've got a pile of numbers, now what

ch01-01 through ch01-03 let you produce performance numbers that "aren't an empty shell, are measured right, and come from a clean environment". But what you get is never one number, it's a pile of numbers — a distribution. ch01-01 said it at the very start: performance isn't a boolean, it's a distribution. This article answers the last question: how do you turn that distribution into a credible conclusion, "A is X% faster than B, and that X% is real, not noise"?

This is, in essence, statistical inference. It sounds intimidating, but what you actually need to master boils down to a few rules, and each one maps to a mistake you will really make.

## What to report, what not to report

First, hard rules:

- **Must report**: the median, a dispersion measure (IQR interquartile range, or 95% confidence interval CI), sample count, and an environment snapshot (kernel / CPU / governor / `perf_event_paranoid`). With `Repetitions` + `ReportAggregatesOnly(true)`, GBench gives you `mean` / `median` / `stddev` / `cv` automatically — the table we got in ch01-02.
- **Forbidden**: a single-pass mean. The `mean` from one run isn't a conclusion, it's a single sample.

Why single out the "single-pass mean" for prohibition? Because performance data is **right-skewed**: usually fast, but occasionally a reallocation, getting scheduled away, or a cache miss drags out a long tail. The mean gets pulled up by the long tail, while the median doesn't budge. Two datasets where "A's typical performance is clearly better" can have B win on the mean simply because B has fewer long tails — the conclusion flips.

## Why the median is more reliable than the mean

Bakhvalov has a very illustrative figure in §2.4 of *Performance Analysis and Tuning on Modern CPUs*: performance measurements of two versions A and B **plotted as distributions**, two curves that overlap heavily. A's peak (the most likely latency) sits further left than B's (faster), so A looks like the winner. But because the distributions overlap, **"A is faster than B" only holds for some probability P**: there are always samples where B is actually faster. You draw one sample and it might land in the segment where B is faster.

This has a direct corollary:

- Don't draw a conclusion from one or two samples. What you want is a comparison of distributions, not a comparison of point estimates.
- Use the **median** to represent "typical performance", and a **dispersion measure** (IQR / 95% CI / cv) to represent "how stable this typical is". A cv (`stddev/mean`) below 1% is stable; above 5% the group itself is untrustworthy — go back and find the noise source (ch01-03) before drawing any conclusion.
- Look at the shape of the distribution itself. If it's **bimodal** (two peaks), your benchmark has mixed two behaviors — the classic cases are cache-hit vs cache-miss paths, or lock-contention vs no-contention. Bakhvalov warns: a bimodal distribution isn't noise, it's a signal, meaning you should split the two scenarios and measure them separately, not mash them together and take the median.

## Hypothesis testing: t-test or Mann-Whitney U

"A is 12% faster than B — is that 12% real?" That's a statistical question, called **hypothesis testing**. The idea is to first assume "A and B are no different" (the null hypothesis), then look at how unlikely your data is under that null. If unlikely enough (p value below a threshold, usually 0.05), call it "significant" and reject the null.

Which test to pick depends on what the data distribution looks like:

- **Student's t-test** (parametric): assumes the data follows a **normal distribution**. Simple to compute, the default in textbooks.
- **Mann-Whitney U** (non-parametric): makes no assumption about distribution shape; it just compares the ranks (who's bigger after sorting) of the two groups.

Here's the key point, paraphrasing Bakhvalov in §2.4: **in performance measurement data, a normal distribution almost never shows up.** Performance data is usually skewed, long-tailed, even multimodal. So the textbook formulas that assume normality (including the t-test) must be used cautiously in performance settings. He calls this out specifically because too many people reach for the t-test by default.

Practical advice: for A/B significance judgement, **default to Mann-Whitney U** (non-parametric, robust); only use the t-test if you've first run a normality test and confirmed the data is actually close to normal. Python's `scipy.stats.mannwhitneyu` and R's `wilcox.test` both compute it directly.

## The right posture for A/B comparison

Putting the above together, a credible "A is faster than B" conclusion must satisfy:

1. **Same environment**: same machine, same governor, same workload, only the one thing you're comparing changes. Don't measure A and B on two different machines.
2. **Same binary**: ideally a compile-time switch in one binary flips A/B, to avoid layout bias from different compilations.
3. **Multiple repetitions**: measure A N times, B N times (N ≥ 30 is best), each yielding a distribution.
4. **Report effect size, not just p value**: the p value only tells you "is the difference real", not "how big is the difference". A "p<0.05, 0.3% faster" conclusion is statistically significant but engineering-meaningless. Report the full form: "12% faster (95% CI [10%, 14%], p<0.01)".
5. **Run more than one round**: confirm across days and warmup states, in case this round's environment drifted.

What this set gets automated into in CI (ch01-05) is doing this kind of A/B continuously and judging regressions automatically.

## micro vs macro: one more time

Because it's the most deadly, every article in this chapter is going to call it out once.

**It is forbidden to back production performance with microbenchmark conclusions.** A function that measures IPC=2, all-cache-hit in a microbenchmark can completely become IPC=0.3 in a real workload because of cache misses. This isn't "micro is inaccurate" — micro measures "how fast can it run under ideal conditions", and production has no ideal conditions. The two are two languages and can't be directly converted.

In Bakhvalov's §2.4 example, the "distribution comparison" is done within the same kind of scenario (both micro or both macro). When comparing across scenarios, you either restrict the micro conclusion to "the relative direction of improvement for this function", or you go do macro measurement (production telemetry / macro load benchmark). The latter is the subject of ch01-05.

## References

- Bakhvalov, D. *Performance Analysis and Tuning on Modern CPUs* §2.4 *Manual Performance Testing* (distribution comparison, bimodal, hypothesis testing, the "performance data is almost never normal" line).
- Feitelson, D. G. *Workload Modeling for Computer Systems Performance Evaluation* (Bakhvalov's recommended reference for performance statistics; modal distributions, skewness, etc.).
- Wikipedia: [Mann–Whitney U test](https://en.wikipedia.org/wiki/Mann%E2%80%93Whitney_U_test), [Student's t-test](https://en.wikipedia.org/wiki/Student%27s_t-test).
- This volume's ch01-01 (the root of micro vs macro), ch01-02 (the `mean`/`median`/`stddev`/`cv` from `ReportAggregatesOnly`).
