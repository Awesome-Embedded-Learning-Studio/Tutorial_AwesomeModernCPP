---
chapter: 4
cpp_standard:
- 17
description: 'The remedy for the Bad Speculation bucket is eliminating unpredictable branches. This article covers branchless (removing branches via cmov/bitwise tricks) and the principle of predication, but the core message is counterintuitive: modern CPU branch predictors are extremely strong, and the compiler often goes branchless for you automatically (loops vectorized into SIMD masks, scalars turned into cmov) — so "don''t go branchless blindly": predictable branches are nearly free, and a branchless rewrite that adds instructions or data dependencies can end up slower. Always benchmark'
difficulty: advanced
order: 6
platform: host
prerequisites:
- 'Pipeline, ILP, and branch prediction: same data, several-fold differences depending on how you execute it'
- 'Data types and arithmetic: int vs float, the division bottleneck, and jump tables'
reading_time_minutes: 6
related:
- 'Loop and compute optimization: code motion, eliminating memory references, and multiple accumulators'
- 'Frontend optimization: code layout, PGO, and BOLT'
tags:
- host
- cpp-modern
- advanced
- 优化
title: 'Branches: branchless, predication, and "don''t go branchless blindly"'
translation:
  source: documents/vol6-performance/ch04-tuning-by-bottleneck/04-06-branch-branchless.md
  source_hash: 7ade38dd6e5ddb37b7f7494d863b58dd57e394732e08dd0a469aca4c1858f809
  translated_at: '2026-09-26T06:31:51+00:00'
  engine: anthropic
  token_count: 1600
---
# Branches: branchless, predication, and "don't go branchless blindly"

## The remedy for the Bad Speculation bucket

In ch02-03 we measured this: with a 50/50 random branch, every predictor miss has to flush the pipeline, and that is brutally expensive — sorted array (predictable) vs shuffled array (unpredictable) differed by **4.2x**. TMAM files this waste under the **Bad Speculation** bucket. This bucket has exactly one line of remedy: **eliminate unpredictable branches**.

There are two ways to do that: **branchless** (rewrite the branch into branchless cmov/bitwise ops) and **predication** (compute both paths and select by condition at the end — if both paths are computed anyway, there is no such thing as "guessing wrong"). Both sound like silver bullets, but the core message of this article is the counterintuitive one: **don't go branchless blindly**. We'll use experiments to show why.

## branchless: cmov and bitwise operations

The most common branchless rewrites are `std::min`/`std::max`/`std::clamp` and friends: underneath they use the **cmov** (conditional move) instruction — both paths' results are computed, then one is "selected" by condition. No branch jump anywhere, hence no misprediction.

```cpp
// Branchy: on unpredictable data, mispredictions flush the pipeline
int clamp_if(int x, int lo, int hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}
// Branchless (std::min/max compiles to cmov)
int clamp_cmov(int x, int lo, int hi) {
    return std::min(std::max(x, lo), hi);
}
```

The cost of `cmov`: both paths get computed (one extra computation), and there is a data dependency (the selected result depends on the condition), but there is **no control dependency and no pipeline flush**. On an **unpredictable** branch, cmov wins hands down; on a **predictable** branch, cmov is actually slower (you paid for computing both paths plus a longer data dependency chain, while the branchy version's predictor hits and stays nearly free).

## Try it yourself (and an honest result)

We measured three clamp formulations on 50/50 random data: an `if` branch / `std::min`+`std::max` (cmov) / a bitwise trick:

```text
===== branchless vs 分支(clamp,随机数据 50/50)=====
  if 分支:       0.27 ns/次(预测失败冲刷)
  std::min/max:  0.25 ns/次
  位 trick:      0.25 ns/次
  if / cmov = 1.07x
```

**The gap is only 1.07x — not the "branchless wins big" we expected.** Why? Look at the assembly (`g++ -O2 -S branchless.cpp`): **the number of `cmov` instructions in the entire loop = 0**. At default `-O2`, GCC **auto-vectorized all three clamp loops into the same SIMD masked operation** (`-fopt-info-vec` reports `loop vectorized using 16 byte vectors`). In other words, whether you write `if`, `std::min/max`, or a bitwise trick, once compiled at -O2 inside a loop **they all become the same vector code**, so all three run equally fast. **Note the mechanism: it is not "if was turned into cmov"** (only a scalar, one-shot clamp gets cmov from the compiler) **— the loop got vectorized. Don't explain this with the wrong mechanism.**

This is an **extremely important honest result**: the `if` you wrote is not necessarily a real branch — the compiler may have gone branchless for you long ago (loops usually via SIMD vectorization, one-shot scalars usually via cmov). To confirm, you have to read the assembly (`-S`) plus the vectorization diagnostics (`-fopt-info-vec`): a real branch shows up as `jcc` (conditional jump), a scalar branchless as `cmov`/`cset`, a loop branchless as SIMD mask instructions. **Don't assume your if is a branch, and don't assume hand-written branchless is necessarily faster.**

So where does the real-branch penalty live? To force a real branch out, you have to **disable the compiler's if-conversion** (just as in ch02-03, where we needed `-fno-if-conversion` to measure the 4.2x). The ch02-03 experiment (shuffled vs sorted, 4.2x) is the honest evidence of the real-branch penalty; this article's 1.07x is the honest evidence that "the compiler already went branchless". Put the two together and you get the complete picture.

## predication: compute both paths

Generalize the idea behind `cmov` and you get **predication**: rather than branching to pick one path and compute it, **compute both paths and select by condition at the end**. GPUs and vector architectures (the SSE/AVX masks) lean on predication heavily, because they hate branches (divergence). On x86, predication shows up as cmov / cset.

The cost of predication: total work = the sum of both paths (even though only one result is taken). So it **only fits scenarios where "both paths are cheap"** (`max`, `min`, `clamp`, simple assignments). If one of the two paths is expensive (a division, say, or a function call), predication forcing both through is a big loss — **a branch is better there** (the expensive path only runs when actually taken). That tradeoff is the other face of "don't go branchless blindly".

## `[[likely]]` / `[[unlikely]]`: giving the compiler a branch probability

C++20 standardized `[[likely]]` / `[[unlikely]]` (the old way was GCC's `__builtin_expect`): it hands the compiler a branch-probability hint, so it can lay out the likely path's code together (better icache; see ch04-07 on frontend optimization) and adjust its branch-prediction assumptions.

```cpp
if (rare_error) [[unlikely]] { handle_error(); }  // tell the compiler this path is rarely taken
```

Note that the **main payoff of `[[likely]]` is code layout** (cluster the hot path together, push the cold path to the end of the function), which raises the icache hit rate — that is a Frontend optimization. It is **not** "making the branch predictor more accurate" (the hardware predictor does not read your source annotations; it reads runtime history). So `[[likely]]` helps when "the branch is heavily skewed + the function is fairly large", and does basically nothing for "a branch inside a small loop".

## "Don't go branchless blindly": four disciplines

Compress this article's honest results into four disciplines:

1. **Predictable branches are nearly free.** Loop-exit conditions, `if (ptr == nullptr)` — branches that almost always go the same way sit at 99%+ predictor hit rate; don't waste effort eliminating them. What deserves elimination are **data-dependent, unpredictable branches**.
2. **Your `if` is not necessarily a real branch.** The compiler often goes branchless on its own (loops get vectorized into SIMD masks, scalars turn into cmov); confirm with the assembly plus `-fopt-info-vec`.
3. **branchless is not a silver bullet.** If predication forces both paths to compute (one of them expensive), or lengthens the data dependency chain, branchless is actually slower.
4. **Always benchmark against a control.** Measure before and after the branchless change with the same methodology (ch01); the signal beats intuition.

This "don't go blindly" discipline actually runs through all of ch04. 04-02's loop optimization, 04-04's inline, this article's branchless — the story is the same every time: **the modern compiler + hardware predictor already do most of it for you; your job is not "hand-optimize harder" but "measure out the real bottleneck, change precisely, verify afterward"**. Performance tuning is not a pile of tricks; it is measurement-driven precision surgery.

Look back over this article: the remedy for the Bad Speculation bucket is eliminating unpredictable branches, and the techniques are branchless (cmov/bitwise) and predication; measured if/cmov/bitwise-trick clamps all run at basically the same speed (1.07x), because at `-O2` all three loop forms got vectorized into the same SIMD mask code (`-S` shows cmov count = 0), while the evidence for the real-branch penalty is ch02-03's 4.2x; predication only fits the both-paths-cheap case — with one expensive path, a plain branch wins; `[[likely]]`/`[[unlikely]]`'s main payoff is code layout (icache), not the predictor; and at the bottom of it all, don't go branchless blindly — predictable branches are free, your if may already be a cmov, and the benchmark is the only referee.

## References

- Agner Fog, *The microarchitecture of Intel, AMD and VIA CPUs*, the branch-prediction chapter. Local copy.
- Bakhvalov, *Performance Analysis and Tuning on Modern CPUs*, Chapter 9, *Optimizing Bad Speculation*.
- ch02-03 Pipeline, ILP, and branch prediction (this volume; the source of the measured 4.2x real-branch penalty).
- This article's measurement code: `code/volumn_codes/vol6-performance/ch04/branchless.cpp`
