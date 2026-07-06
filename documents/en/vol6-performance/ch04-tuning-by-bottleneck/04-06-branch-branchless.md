---
chapter: 4
cpp_standard:
- 17
description: 'The Bad Speculation bucket''s countermeasure is to eliminate unpredictable branches. This article covers branchless (cmov / bitwise ops to remove branches) and predication, but the core message is counterintuitive: modern CPU branch predictors are extremely strong + the compiler often auto-branchless-es (loops vectorized into SIMD masks, scalars into cmov), so "don''t go branchless blindly" — predictable branches are nearly free, and a branchless rewrite that adds instructions or data dependencies can be slower. Always benchmark'
difficulty: advanced
order: 6
platform: host
prerequisites:
- Pipeline, ILP, and branch prediction
- Data types and arithmetic — int vs float, the division bottleneck, and jump tables
reading_time_minutes: 6
related:
- Loop and compute optimization — code motion, unrolling, and multiple accumulators
- Frontend optimization — code layout, PGO, BOLT
tags:
- host
- cpp-modern
- advanced
- 优化
title: "Branches: branchless, predication, and \"don't go branchless blindly\""
translation:
  source: documents/vol6-performance/ch04-tuning-by-bottleneck/04-06-branch-branchless.md
  source_hash: 7ade38dd6e5ddb37b7f7494d863b58dd57e394732e08dd0a469aca4c1858f809
  translated_at: '2026-07-06T00:00:00+00:00'
  engine: manual
  token_count: 2800
---
# Branches: branchless, predication, and "don't go branchless blindly"

## The Bad Speculation bucket's countermeasure

In ch02-03 we measured: a 50/50 random branch that the predictor guesses wrong has to flush the pipeline, which is brutally expensive — sorted array (predictable) vs shuffled array (unpredictable) differs by **4.2x**. TMAM attributes that waste to the **Bad Speculation** bucket. This bucket's countermeasure has one line of thought: **eliminate unpredictable branches**.

There are two ways to eliminate: **branchless** (turn the branch into branchless cmov/bitwise ops) and **predication** (compute both paths, then pick by condition at the end — if both are computed, "guessing wrong" doesn't matter). Both sound like silver bullets, but the core message of this article is the counterintuitive one: **don't go branchless blindly.** We use experiments to show why.

## branchless: cmov and bitwise ops

The most common branchless rewrite is `std::min`/`std::max`/`std::clamp` and friends, which bottom out in the **cmov** (conditional move) instruction: both paths' results are computed, then "selected" by condition, with no branch jump anywhere and therefore no misprediction.

```cpp
// Branchy: mispredicts on unpredictable data, pipeline flush
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

The cost of `cmov`: both paths are computed (one extra computation), there's a data dependency (the selected result depends on the condition), but **there's no control dependency, no pipeline flush**. On an **unpredictable** branch, cmov wins big; on a **predictable** branch, cmov is actually slower (because both are computed + the data dependency chain is longer, while the branch version's predictor hits and is nearly free).

## Run it (with an honest result)

I measured three clamp styles on 50/50 random data: an if branch / `std::min`+`std::max` (cmov) / a bitwise trick:

```text
===== branchless vs branch (clamp, random data 50/50) =====
  if branch:    0.27 ns/run (misprediction flush)
  std::min/max: 0.25 ns/run
  bitwise trick: 0.25 ns/run
  if / cmov = 1.07x
```

**Only a 1.07x gap, not the "branchless wins big" I expected.** Why? Look at the assembly (`g++ -O2 -S branchless.cpp`) — **the cmov count in the entire loop is 0**: at default `-O2`, GCC **auto-vectorizes all three clamp loops into the same SIMD mask operation** (`-fopt-info-vec` reports `loop vectorized using 16 byte vectors`). In other words, whether you write `if`, `std::min/max`, or a bitwise trick, **after -O2 in a loop they all become the same vector code**, so all three are equally fast. **Note this isn't "if was turned into cmov"** (only a scalar single-shot clamp gets cmov) **— it's "the loop got vectorized." Don't explain it with the wrong mechanism.**

This is an **extremely important honest result**: the `if` you wrote isn't necessarily a real branch — the compiler may have already branchless-ed it for you (in loops via SIMD vectorization, in scalar single-shots via cmov). To confirm, you have to look at the assembly (`-S`) + the vectorization diagnostics (`-fopt-info-vec`): a real branch is a `jcc` (conditional jump), a scalar branchless is `cmov`/`cset`, a loop branchless is a SIMD mask instruction. **Don't assume your if is a branch, and don't assume hand-written branchless is necessarily faster.**

Where does the real-branch cost live then? To force a real branch out, you have to **disable the compiler's if-conversion** (just like in ch02-03, where I needed `-fno-if-conversion` to measure the 4.2x). The ch02-03 experiment (shuffled vs sorted, 4.2x) is the honest evidence of the real-branch penalty. The 1.07x in this article is the honest evidence that "the compiler already went branchless." Together they're the complete picture.

## predication: compute both paths

The generalization of `cmov` is **predication**: rather than branch and pick one path to compute, **compute both paths and pick by condition at the end.** GPUs and vector architectures (SSE/AVX masks) use predication heavily because they hate branches (divergence). On x86, predication shows up as cmov / cset.

The cost of predication: total work = the sum of both paths (even though only one's result is taken). So it's **only suited to "both paths are cheap"** scenarios (`max`, `min`, `clamp`, simple assignments). If one of the two paths is expensive (a division, a function call), predication forcing both is a big loss — **a branch is better there** (the expensive path only runs when it hits). That tradeoff is the other face of "don't go branchless blindly."

## `[[likely]]` / `[[unlikely]]`: hand the compiler a branch probability

C++20 standardized `[[likely]]` / `[[unlikely]]` (the old way was GCC's `__builtin_expect`): give the compiler a branch-probability hint so it lays out the likely path's code together (improving icache, see ch04-07 frontend optimization) and tunes the branch-prediction assumptions.

```cpp
if (rare_error) [[unlikely]] { handle_error(); }  // tell the compiler this path is rare
```

Note that the **main payoff of `[[likely]]` is code layout** (cluster the hot path, push the cold path to the end of the function) for better icache hit rate — that's a Frontend optimization. It's **not** "make the branch predictor more accurate" (the hardware predictor doesn't read your source annotations; it reads runtime history). So `[[likely]]` is useful when "the branch is heavily skewed + the function is fairly large," and basically useless for "a branch inside a small loop."

## "Don't go branchless blindly": four disciplines

Compress this article's honest results into four disciplines:

1. **Predictable branches are nearly free.** Loop-exit conditions, `if (ptr == nullptr)` — branches that almost always go the same way have 99%+ predictor hit rate, no need to bother eliminating them. What you should eliminate are **data-dependent, unpredictable branches**.
2. **Your `if` isn't necessarily a real branch.** The compiler often auto-branchless-es (in loops via SIMD masks, in scalar via cmov); confirm with the assembly + `-fopt-info-vec`.
3. **branchless isn't a silver bullet.** If predication computes both paths (one of them expensive), or lengthens the data dependency chain, branchless is actually slower.
4. **Always benchmark against a control.** Measure before and after a branchless change with the same methodology (ch01); the signal beats intuition.

This "don't go blindly" discipline actually runs through all of ch04. 04-02's loop optimization, 04-04's inline, this article's branchless — the story is the same one: **the modern compiler + hardware predictor does most of it for you, so your job isn't "hand-write optimizations hard," it's "measure the real bottleneck, change precisely, verify after."** Performance optimization isn't a pile of tricks; it's measurement-driven precision surgery.

Looking back at this article: the Bad Speculation bucket's countermeasure is eliminating unpredictable branches, with branchless (cmov/bitwise) and predication as the techniques; the measured if/cmov/bitwise-trick clamp are all basically the same speed (1.07x), because at `-O2` all three loop forms get vectorized into the same SIMD mask code (`-S` shows cmov count = 0), and the real-branch-penalty evidence is ch02-03's 4.2x; predication only suits the "both paths cheap" case — when one path is expensive, a branch wins; `[[likely]]`/`[[unlikely]]`'s main payoff is code layout (icache), not the predictor; and the bottom line is don't go branchless blindly — predictable branches are free, your if might already be a cmov, and the benchmark is the only referee.

## References

- Agner Fog, *The microarchitecture of Intel, AMD and VIA CPUs*, branch-prediction chapter; local copy.
- Bakhvalov, *Performance Analysis and Tuning on Modern CPUs*, Chapter 9 *Optimizing Bad Speculation*.
- ch02-03 Pipeline, ILP, and branch prediction (this volume; the source of the 4.2x real-branch penalty measurement).
- Measured code for this article: `code/volumn_codes/vol6-performance/ch04/branchless.cpp`.
