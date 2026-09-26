---
chapter: 1
cpp_standard:
- 11
- 17
description: What to do with a pile of measured numbers — why performance data should be reported as median plus confidence interval rather than a lone mean, why the data is almost never normal so Mann-Whitney U beats the t-test, the right way to do an A/B comparison, and why a micro conclusion can never vouch for macro performance.
difficulty: intermediate
order: 4
platform: host
prerequisites:
- How to write a credible microbenchmark
- 'Measurement pitfalls and environment readiness: a 16-item checklist'
reading_time_minutes: 6
related:
- Production measurement and CI performance regression detection
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
  translated_at: '2026-09-26T05:37:30+00:00'
  engine: anthropic
  token_count: 4500
---
# Statistics and reporting: turning a distribution into a conclusion

## You've got a pile of numbers, now what

ch01-01 through ch01-03 got you to the point where the performance numbers you produce are "not a hollow shell, measured right, and from a clean environment". But what you hold is never one number — it's a set of numbers, a distribution. As ch01-01 said right at its opening: performance is not a boolean, it's a distribution. This article answers the final question: how does that distribution become a credible conclusion — "A is X% faster than B, and the speedup is real, not noise"?

The essence of this step is statistical inference. It sounds intimidating, but the rules you actually need to master are just a handful — and each one corresponds to a mistake you will genuinely make.

## What to report, and what not to

Hard rules first:

- **Always report**: the median, a dispersion measure (the IQR interquartile range, or a 95% confidence interval CI), the sample count, and an environment snapshot (kernel / CPU / governor / `perf_event_paranoid`). With `Repetitions` + `ReportAggregatesOnly(true)`, GBench hands you `mean` / `median` / `stddev` / `cv` automatically — the table we ran in ch01-02 is exactly that.
- **Never report**: a lone mean. The `mean` from one run is not a conclusion — it is one sample.

Why single out the single-run mean for a ban? Because performance data is **right-skewed**: runs are normally fast, then an occasional reallocation, an occasional descheduling, an occasional cache miss drags out a long tail. The mean gets pulled up by that tail; the median doesn't budge. Two datasets where "A clearly has the better typical behavior" can still end with B ahead on the mean, just because B's tail is thinner — and your conclusion flips.

## Why the median is more trustworthy than the mean

In §2.4 of *Performance Analysis and Tuning on Modern CPUs*, Bakhvalov has a very telling figure: the performance measurements of two versions, A and B, **plotted as distributions**, with the two curves heavily overlapping. A's peak (the most likely elapsed time) sits further left (faster) than B's, so A looks like the winner. But because the distributions overlap, **"A is faster than B" holds only with some probability P**: there are always samples in which B is the faster one. The single sample you draw can land exactly in the stretch where B wins.

The immediate corollaries:

- Don't draw conclusions from one or two samples. What you want is a comparison of distributions, not a comparison of point estimates.
- Use the **median** to represent "typical behavior", and a **dispersion measure** (IQR / 95% CI / cv) to represent "how stable that typical is". A cv (`stddev/mean`) below 1% is very stable; above 5%, the dataset itself is untrustworthy — go back and hunt down the noise sources (ch01-03) before rushing to a conclusion.
- Look at the shape of the distribution itself. A **bimodal** distribution (two peaks) means two behaviors are mixed inside your benchmark — classically the cache-hit versus cache-miss paths, or lock contention versus no contention. Bakhvalov's reminder: a bimodal distribution is not noise, it's a signal — it means you should split the two scenarios apart and measure them separately, not smear them together and take a median.

## Hypothesis testing: t-test or Mann-Whitney U

"A is 12% faster than B — is that 12% real?" This is a statistical question, and it has a name: **hypothesis testing**. The idea: first assume "A and B are no different" (the null hypothesis), then ask how improbable your data would be under that null. If it is improbable enough (the p-value below a threshold, usually 0.05), you declare "the difference is significant" and reject the null.

Which test you choose depends on what the data's distribution looks like:

- **Student's t-test** (a parametric test): assumes the data follows a **normal distribution**. Easy to compute; this is the textbook default.
- **Mann-Whitney U** (a non-parametric test): assumes nothing about the distribution's shape; it compares only the ranks of the two datasets (who lands above whom once sorted).

Now the key point, paraphrased straight from Bakhvalov in §2.4: **in performance measurement data, a normal distribution almost never shows up**. Performance data is typically skewed, long-tailed, even multimodal. So those textbook formulas that assume normality (the t-test included) must be used with caution in performance work. He calls this out explicitly because too many people reach for the t-test by default.

Practical advice: for A/B significance testing, **default to Mann-Whitney U** (non-parametric, robust); use the t-test only after running a normality test first and confirming the data is genuinely near-normal. Python's `scipy.stats.mannwhitneyu` and R's `wilcox.test` both compute it directly.

## The right way to do an A/B comparison

Put the pieces above together, and a credible "A is faster than B" conclusion has to satisfy:

1. **Same environment**: same machine, same governor, same workload, with only the one thing you're comparing changed. Don't measure A on one machine and B on another.
2. **Same binary**: ideally one binary with a compile-time switch toggling between A and B, avoiding the layout bias that separate builds introduce.
3. **Many repetitions**: run A N times and B N times (N ≥ 30 is best), producing a distribution for each.
4. **Report the effect size, not just the p-value**: the p-value only tells you "is the difference real", not "how big is the difference". A conclusion of "p<0.05, 0.3% faster" is statistically significant yet meaningless in engineering terms. Report the complete form: "12% faster (95% CI [10%, 14%], p<0.01)".
5. **Run more than one round**: re-run across different days and different warmup states to confirm, guarding against the environment having drifted during this round.

Automating this whole regimen in CI — the subject of ch01-05 — means continuously running exactly this kind of A/B and flagging regressions automatically.

## micro vs macro: one last time

Because it is the deadliest one, it's worth singling out in every single article of this chapter.

**Never use a microbenchmark's conclusions to vouch for production performance.** A function that measures IPC=2 with every cache access hitting inside a microbenchmark can perfectly well drop to IPC=0.3 under a real workload, courtesy of cache misses. That's not "micro is inaccurate" — micro measures "how fast can this thing run under ideal conditions", and production offers no ideal conditions. The two are different languages; there is no direct exchange rate.

In Bakhvalov's §2.4 examples, the "distribution comparison" is done within the same class of scenario (both micro, or both macro). When you must compare across scenarios, either confine the micro conclusion to "the direction of relative improvement for this function", or go do macro measurement outright (production telemetry / macro workload benchmarks). The latter is ch01-05's business.

## References

- Bakhvalov, D., *Performance Analysis and Tuning on Modern CPUs*, §2.4 *Manual Performance Testing* (distribution comparison, bimodality, hypothesis testing, and the "performance data is almost never normal" quote)
- Feitelson, D. G., *Workload Modeling for Computer Systems Performance Evaluation* (the dedicated performance-statistics reference Bakhvalov recommends; modal distributions, skewness, and so on)
- Wikipedia: [Mann–Whitney U test](https://en.wikipedia.org/wiki/Mann%E2%80%93Whitney_U_test), [Student's t-test](https://en.wikipedia.org/wiki/Student%27s_t-test)
- This volume's ch01-01 (the root of micro vs macro) and ch01-02 (`ReportAggregatesOnly`'s `mean`/`median`/`stddev`/`cv`)
