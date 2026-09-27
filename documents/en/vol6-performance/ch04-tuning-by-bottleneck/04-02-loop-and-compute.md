---
chapter: 4
cpp_standard:
- 17
description: 'Loops are the main battlefield of C++ performance. This article walks through the classic loop optimizations from CSAPP chapter 5: code motion (hoisting loop invariants), eliminating unnecessary memory references, loop unrolling, multiple accumulators to break dependency chains, and reassociation to expose ILP. The point is telling apart what the compiler already does for you (so you can skip hand-writing it) from what usually needs a manual nudge (the FP reduction dependency chain)'
difficulty: advanced
order: 2
platform: host
prerequisites:
- 'Pipeline, ILP, and branch prediction: same data, several-fold differences depending on how you execute it'
- 'Backend memory bottlenecks: cache-friendly, AoS/SoA, and prefetch'
reading_time_minutes: 6
related:
- 'Data types and arithmetic: int vs float, the division bottleneck, and jump tables'
- 'SIMD and vectorization: auto-vectorization conditions, intrinsics, and CPU dispatch'
tags:
- host
- cpp-modern
- advanced
- 优化
title: 'Loop and compute optimization: code motion, eliminating memory references, and multiple accumulators'
translation:
  source: documents/vol6-performance/ch04-tuning-by-bottleneck/04-02-loop-and-compute.md
  source_hash: 708427dd46583be6395bcae574d7a913723509dc7e6095cceee39ffdf7c3ff55
  translated_at: '2026-09-26T06:22:14+00:00'
  engine: anthropic
  token_count: 2900
---
# Loop and compute optimization: code motion, eliminating memory references, and multiple accumulators

## Loop optimization: what is worth hand-writing, and what the compiler already does for you

Programs spend 90% of their time in loops, so loop optimization is the main battlefield of performance engineering. But there is a confusing reality here: **the loop optimizations from CSAPP chapter 5 (code motion, eliminating memory references, unrolling, multiple accumulators) are mostly done automatically by a modern compiler at `-O2`**. Hand-write them yourself, and you will often find no difference. So what is left for this article to cover?

Two things. First, **get clear on which of these the compiler does and which it does not**, so you do not hand-write blindly (adding code complexity with no speedup to show for it); second, the few cases the compiler handles poorly and that you must break open manually (the FP reduction dependency chain is the classic one) — that is where hand-writing actually pays.

CSAPP splits loop optimization into five moves; we walk through them one by one.

## 1. code motion: hoisting loop invariants

Hoist expressions that "compute the same result every iteration" out of the loop. The textbook example:

```cpp
// Bad: recomputes length() every iteration
for (int i = 0; i < s.length(); ++i) process(s[i]);
// Good: length() hoisted out of the loop
int len = s.length();
for (int i = 0; i < len; ++i) process(s[i]);
```

Modern compilers are good at const-fold + LICM (loop-invariant code motion) for pure functions (`length()` being one), and in most cases **hoist automatically**. But the precondition for hoisting is that the compiler can prove "this thing never changes": `volatile`, side effects, function calls that might modify global state — it dare not touch any of those. We measured it (`volatile float scale`, which forbids the compiler from hoisting, vs an ordinary variable it can hoist):

```text
===== A. code motion =====
  scale 是 volatile(每次 load):799.3 us
  scale 外提到普通变量:        765.3 us
```

The gap is tiny (a few percent), because the bottleneck here is the multiplication itself, not the load of scale. **Hand-written code motion usually pays little under a modern compiler**, unless you have confirmed a real dependency the compiler dare not hoist (volatile, cross-translation-unit calls). This move is mainly a **mental model**: understanding it helps you read the code the compiler generates.

## 2. Eliminating unnecessary memory references

The CSAPP classic: keep the accumulation intermediate in memory (an array element) vs in a register.

```cpp
// Bad: every iteration loads c[i], computes, stores c[i] (c[i] keeps hovering in memory)
for (int i = 0; i < N; ++i) c[i] = c[i] + a[i] * b[i] * scale;
// Good: write directly, no read-back
for (int i = 0; i < N; ++i) c[i] = a[i] * b[i] * scale;
```

Measured:

```text
===== B. 消除不必要的内存引用 =====
  反复读写 c[i]:597.8 us
  直接写 c[i]: 526.1 us
```

The gap is a few percent here too, but **do not mistake this for "-O2 eliminated the redundant memory access for you"**. Look at the assembly (`g++ -O2 -S loop_opt.cpp`): the redundant `c[i]` read-back in the `.bad` version **is not eliminated at -O2** (every iteration still has `addss (%rdx,%rax),%xmm0` reading `c[i]` back to accumulate), because `c[i]` is written every time and the compiler dare not assume the read-back can be skipped; the `.good` version simply never reads back. So the gap really is the cost of that one extra load/store — just small relative to the multiplication itself, hence only a few percent. The CSAPP textbook "put the accumulator in a register instead of an array element" case that -O2 does often handle automatically is the **pure scalar accumulation with no write-back to an array** situation. This move usually buys little under a modern compiler, but the cause and effect must be told right: do not treat "the compiler already optimized it" as a universal explanation.

## 3. Multiple accumulators: breaking the dependency chain (the compiler often does this poorly, worth hand-writing)

This is the move of the five that **most often needs a manual nudge**, because language semantics constrain the compiler from doing it automatically. Recall ch02-03: the single-accumulator reduction `acc += a[i]*b[i]` is one long RAW dependency chain with nearly zero ILP; splitting it into 4 independent chains with 4 accumulators is what lets the CPU fill the execution ports in parallel. Measured (ch02-03): **single accumulator 23.7 us vs 4 accumulators 8.1 us, 2.92× faster**.

Why does the compiler not do this one for you? For **integer reductions** it sometimes auto-unrolls into multiple accumulators; for **FP reductions** it **cannot**, because floating-point addition is not associative (`(a+b)+c ≠ a+(b+c)`, floating-point error), and rewriting automatically would change the result and violate standard semantics. So:

```cpp
// FP single chain: the compiler dare not auto-multi-accumulate it (would change the floating-point result)
float acc = 0; for (int i = 0; i < N; ++i) acc += a[i] * b[i];

// Hand-written 4 accumulators: you (the programmer) take on the responsibility that "the association order changed, the result differs slightly"
float a0=0,a1=0,a2=0,a3=0;
for (int i = 0; i < N; i += 4) { a0 += a[i]*b[i]; a1 += a[i+1]*b[i+1]; ... }
return a0 + a1 + a2 + a3;
```

This is the **highest-value hand-written move** in ch04: FP hot loops like dot products, reductions, and inner products get a steady 2-4× from hand-written multiple accumulators. The other fix is `-ffast-math` (loosens FP semantics, lets the compiler reassociate), but `-ffast-math` changes NaN/Inf behavior and is a global switch with a heavy cost — **use it only where you are certain strict FP semantics are not needed**. The SIMD article (ch04-05) runs into this same "FP associativity blocking the way" problem again.

## 4. Loop unrolling

Change `for (i) a[i]` into `for (i+=4) { a[i]; a[i+1]; a[i+2]; a[i+3]; }`. Two benefits: less loop-control overhead (branches, counter increments), and more room for register allocation and ILP. Modern compilers do it at `-O3 -funroll-loops`, and the compiler picks the unroll factor more cleverly than a hand-written one (it can trade off register pressure), so **by default do not hand-write unrolling**. Hand-written unrolling pays in only two cases: (a) you are doing multiple accumulators at the same time (unrolling and multi-accumulators are often written together); (b) the compiler did not unroll but your benchmark proves unrolling helps. Blind hand-unrolling often ends up slower because the larger code footprint causes icache misses.

## 5. Reassociation

Rewrite the parentheses of the operation: `((a+b)+c)+d` → `(a+b)+(c+d)`. This is essentially another form of "breaking the dependency chain": reparenthesizing so that independent subexpressions can run in parallel. It and multiple accumulators are two spellings of the same idea (exposing more ILP). For FP it is equally constrained by associativity and needs hand-writing or `-ffast-math`.

Compressing the five moves into one "hand-write vs compiler" cheat sheet:

| Optimization | Does the compiler do it at -O2? | Value of hand-writing |
|---|---|---|
| Code motion (invariant hoisting) | Mostly yes | Low (unless volatile / cross-TU calls) |
| Eliminating memory references | Mostly yes | Low |
| Multiple accumulators (FP reduction) | **No** (FP associativity) | **High (2-4×)** |
| Loop unrolling | Yes at -O3 -funroll-loops | Low (unless paired with multi-accumulators) |
| Reassociation (FP) | **No** | Medium-high |

In one sentence: **for integer loops the compiler already does most of the optimization for you, so do not over-hand-write; for FP reduction / dot-product hot loops, hand-written multiple accumulators are a steady free lunch**. Whichever move it is, benchmark against a control after hand-writing — "the compiler already did it" and "your hand-written version is actually slower" are both far too common.

The next article covers data-type and arithmetic selection, where another class of "the compiler cannot do it for you, you must choose by hand" optimization shows up: the division bottleneck.

## References

- Bryant & O'Hallaron, *CSAPP*, Chapter 5 *Optimizing Program Performance* — the classic derivation of code motion / eliminating memory references / unrolling / multiple accumulators / reassociation (the source of this article's five moves)
- Agner Fog, *Optimizing software in C++*, §12 *Optimizing loops*; local copy.
- ch02-03 Pipeline, ILP, and branch prediction (this volume; the source of the dot1/dot4 2.92× multi-accumulator measurement)
- Measured code for this article: `code/volumn_codes/vol6-performance/ch04/loop_opt.cpp`
