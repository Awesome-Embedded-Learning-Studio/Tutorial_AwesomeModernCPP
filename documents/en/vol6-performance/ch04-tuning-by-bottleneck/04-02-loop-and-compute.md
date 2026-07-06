---
chapter: 4
cpp_standard:
- 17
description: Loops are the main battlefield of C++ performance. This article covers the classic loop optimizations from CSAPP chapter 5 — code motion (loop-invariant hoisting), eliminating unnecessary memory references, loop unrolling, multiple accumulators to break dependency chains, and reassociation to expose ILP. The key is telling what the compiler already does for you (don't hand-write it) from what it often needs you to break manually (the FP reduction dependency chain)
difficulty: advanced
order: 2
platform: host
prerequisites:
- Pipeline, ILP, and branch prediction
- Backend memory bottlenecks — cache-friendly, AoS/SoA, and prefetch
reading_time_minutes: 6
related:
- Data types and arithmetic — int vs float, the division bottleneck, and jump tables
- SIMD and vectorization — auto-vectorization conditions, intrinsics, and CPU dispatch
tags:
- host
- cpp-modern
- advanced
- 优化
title: "Loop and compute optimization: code motion, eliminating memory references, and multiple accumulators"
translation:
  source: documents/vol6-performance/ch04-tuning-by-bottleneck/04-02-loop-and-compute.md
  source_hash: 708427dd46583be6395bcae574d7a913723509dc7e6095cceee39ffdf7c3ff55
  translated_at: '2026-07-06T00:00:00+00:00'
  engine: manual
  token_count: 2800
---
# Loop and compute optimization: code motion, eliminating memory references, and multiple accumulators

## Loop optimization: what's worth hand-writing vs what the compiler already does

Programs spend 90% of their time in loops, so loop optimization is the main battlefield of performance engineering. But there's a confusing reality here: **the modern compiler at `-O2` already does most of CSAPP chapter 5's loop optimizations (code motion, eliminating memory references, unrolling, multiple accumulators) for you.** Hand-write them and you'll often see no difference. So what's left for this article?

Two things. First, **figure out what the compiler does and what it doesn't**, so you don't hand-write blindly (adding code complexity without any speedup). Second, the few things the compiler doesn't do well and that you have to break manually (the FP reduction dependency chain is the classic case) — that's where hand-writing pays.

CSAPP splits loop optimization into five moves. Let's walk them.

## 1. Code motion: hoisting loop invariants

Hoist an expression that "computes the same thing every iteration" out of the loop. The textbook example:

```cpp
// Bad: recompute length() every iteration
for (int i = 0; i < s.length(); ++i) process(s[i]);
// Good: hoist length()
int len = s.length();
for (int i = 0; i < len; ++i) process(s[i]);
```

Modern compilers are good at const-fold + LICM (loop-invariant code motion) for pure functions like `length()`, and in most cases **hoist automatically**. But the precondition for hoisting is that the compiler can prove "this thing doesn't change": `volatile`, side effects, function calls that might modify global state — none of those will it touch. Measured (`volatile float scale`, forcing the compiler not to hoist vs an ordinary variable it can hoist):

```text
===== A. code motion =====
  scale is volatile (loaded every time): 799.3 us
  scale hoisted into an ordinary variable: 765.3 us
```

The gap is small (a few percent), because the bottleneck here is the multiplication itself, not the load of scale. **Hand-written code motion usually pays little under a modern compiler**, unless you've confirmed there's a real dependency the compiler won't hoist (volatile, cross-TU calls). This move is mainly a **mental model** — understanding it helps you read the code the compiler generates.

## 2. Eliminating unnecessary memory references

The CSAPP classic: keep the accumulator intermediate in memory (an array element) vs in a register.

```cpp
// Bad: every iteration loads c[i], computes, stores c[i] (c[i] keeps hovering in memory)
for (int i = 0; i < N; ++i) c[i] = c[i] + a[i] * b[i] * scale;
// Good: write directly, no read-back
for (int i = 0; i < N; ++i) c[i] = a[i] * b[i] * scale;
```

Measured:

```text
===== B. Eliminating unnecessary memory references =====
  Repeated read/write of c[i]: 597.8 us
  Direct write of c[i]:        526.1 us
```

The gap is also a few percent, but **don't mistake this for "-O2 eliminates the extra memory access for you."** Look at the assembly (`g++ -O2 -S loop_opt.cpp`): the extra `c[i]` read-back in the "bad" version **isn't eliminated at -O2** (every iteration still has `addss (%rdx,%rax),%xmm0` reading `c[i]` back to accumulate), because `c[i]` is written every time and the compiler can't assume the read-back can be skipped; the "good" version simply doesn't read back. So the gap is the real cost of one extra load/store, just small relative to the multiplication itself, hence only a few percent. The CSAPP textbook "put the accumulator in a register instead of an array element" case that -O2 does often eliminate automatically is the **pure-scalar-accumulator, no write-back-to-array** situation. This move usually doesn't buy much under a modern compiler, but the cause-and-effect has to be right: don't treat "the compiler already optimized it" as a universal explanation.

## 3. Multiple accumulators: breaking the dependency chain (the compiler often can't, worth hand-writing)

This is the **most often hand-broken** of the five, because the compiler is constrained by language semantics and can't do it automatically. Look back at ch02-03: the single-accumulator reduction `acc += a[i]*b[i]` is one long RAW dependency chain with almost zero ILP; splitting into 4 accumulators gives 4 independent chains, and only then can the CPU fill the execution ports in parallel. Measured (ch02-03): **single accumulator 23.7 us vs 4 accumulators 8.1 us, 2.92x faster**.

Why doesn't the compiler do this one for you? For **integer reductions** it sometimes auto-unrolls into multiple accumulators; for **FP reductions**, it **can't**, because floating-point addition is not associative (`(a+b)+c ≠ a+(b+c)`, different mantissa rounding error), and rewriting it automatically would change the result and violate standard semantics. So:

```cpp
// FP single chain: the compiler dare not multi-accumulate (would change the FP result)
float acc = 0; for (int i = 0; i < N; ++i) acc += a[i] * b[i];

// Hand-written 4 accumulators: you (the programmer) take on the responsibility that
// "the association order changed, the result has a tiny difference"
float a0=0,a1=0,a2=0,a3=0;
for (int i = 0; i < N; i += 4) { a0 += a[i]*b[i]; a1 += a[i+1]*b[i+1]; ... }
return a0 + a1 + a2 + a3;
```

This is the **highest-value hand-written move** in ch04: dot products, reductions, inner products — FP hot loops like these get a steady 2-4x from hand-written multiple accumulators. The other fix is `-ffast-math` (loosen FP semantics, allow the compiler to reassociate), but `-ffast-math` changes NaN/Inf behavior, is a global switch, and has a high cost, **so only use it in a local context where you're sure you don't need strict FP semantics.** ch04-05's SIMD article runs into this same "FP associativity blocking the way" problem again.

## 4. Loop unrolling

Change `for (i) a[i]` into `for (i+=4) { a[i]; a[i+1]; a[i+2]; a[i+3]; }`. Two benefits: less loop control overhead (branches, counter increments), and more room for register allocation and ILP. Modern compilers do it at `-O3 -funroll-loops`, and the compiler picks the unroll factor more smartly than hand-writing (it can trade off register pressure), **so by default don't hand-write unrolling.** Hand-written unrolling is worth it in two cases: (a) you're doing multiple accumulators at the same time (unrolling and multi-accumulate are often written together); (b) the compiler didn't unroll but your benchmark proves unrolling helps. Blind hand-unrolling often **slows down** because the larger code footprint causes icache misses.

## 5. Reassociation

Rewrite the parentheses in an operation: `((a+b)+c)+d` → `(a+b)+(c+d)`. This is essentially another form of "break the dependency chain" — reparenthesizing so independent subexpressions can run in parallel. It's two ways of writing the same idea as multiple accumulators (exposing ILP). For FP it's equally constrained by associativity and needs hand-writing or `-ffast-math`.

Compressing the five moves into a "hand-write vs compiler" cheat sheet:

| Optimization | Does the compiler at -O2? | Hand-write value |
|---|---|---|
| Code motion (invariant hoist) | Mostly yes | Low (unless volatile/cross-TU calls) |
| Eliminate memory refs | Mostly yes | Low |
| Multiple accumulators (FP reduction) | **No** (FP associativity) | **High (2-4x)** |
| Loop unrolling | Yes at -O3 -funroll-loops | Low (unless paired with multi-accumulate) |
| Reassociation (FP) | **No** | Medium-high |

In one sentence: **for integer loops, the compiler does most of it for you, so don't over-hand-write; for FP reduction/dot-product hot loops, hand-written multiple accumulators is a steady free lunch.** Whichever move, after hand-writing you must benchmark against a control — "the compiler already did it" and "your hand-written version is actually slower" are both far too common.

The next article is on data-type and arithmetic selection, where there's another class of optimization the compiler can't do for you and you must choose yourself: the division bottleneck.

## References

- Bryant & O'Hallaron, *CSAPP*, Chapter 5 *Optimizing Program Performance* — the classic derivation of code motion / eliminating memory references / unrolling / multiple accumulators / reassociation (the source of this article's five moves).
- Agner Fog, *Optimizing software in C++*, §12 *Optimizing loops*; local copy.
- ch02-03 Pipeline, ILP, and branch prediction (this volume; the source of the dot1/dot4 2.92x multi-accumulator measurement).
- Measured code for this article: `code/volumn_codes/vol6-performance/ch04/loop_opt.cpp`.
