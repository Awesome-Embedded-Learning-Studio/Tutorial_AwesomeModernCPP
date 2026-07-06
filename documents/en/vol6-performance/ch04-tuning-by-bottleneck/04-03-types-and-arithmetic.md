---
chapter: 4
cpp_standard:
- 17
description: 'Picking the right data type and arithmetic is "an optimization the compiler can''t do for you — you have to choose." With measurements, this article covers integer division as a 5x bottleneck over multiplication (power-of-two divisors reduce to shifts, constant divisions become multiply-by-reciprocal, runtime-variable division has no escape), the cost gap between int and float, and when a switch jump table is actually faster than an if-else chain (answer: not always)'
difficulty: advanced
order: 3
platform: host
prerequisites:
- Loop and compute optimization — code motion, unrolling, and multiple accumulators
- Pipeline, ILP, and branch prediction
reading_time_minutes: 6
related:
- SIMD and vectorization — auto-vectorization conditions, intrinsics, and CPU dispatch
- Branches — branchless, predication, and "don't go branchless blindly"
tags:
- host
- cpp-modern
- advanced
- 优化
title: "Data types and arithmetic: int vs float, the division bottleneck, and jump tables"
translation:
  source: documents/vol6-performance/ch04-tuning-by-bottleneck/04-03-types-and-arithmetic.md
  source_hash: 3ed09d040cb9a2cc599f0b70367ba7d53d17f05e9e378b32ab42fd38352e0b5a
  translated_at: '2026-07-06T00:00:00+00:00'
  engine: manual
  token_count: 2700
---
# Data types and arithmetic: int vs float, the division bottleneck, and jump tables

## This is one class the compiler can't do for you

In ch04-02's loop optimization you'll notice more than half is "the compiler at -O2 already did it." This article looks at arithmetic from a different angle: for some operations, the speed **depends on which data type you chose and which operation you wrote**, and **the choice is in your hands — the compiler is powerless**. The classic case is division.

Division is the most "expensive" basic integer op on x86. Its latency is relatively long (on Zen 3, `idiv` is about 9-12 cycles for 32-bit and 9-17 cycles for 64-bit, against 1-3 cycles for `imul`/`lea`; data from Agner's *Instruction tables*, Zen 3 section; note that's "a dozen-ish cycles," not "tens of cycles" — the "tens of cycles" in old textbooks is a leftover from the Pentium era's long-latency divider), its throughput is low (you can only issue one every 6-12 cycles; dividers are scarce, so they queue up one after another), and it's a textbook **structural hazard** from ch02-03, with multiple divisions fighting for one divide port. Multiplication is the opposite: `imul` is 1 cycle, fully pipelined. **Avoid division when you can** is the core of this article.

## The division bottleneck: 5x over multiplication

Let's measure directly (local Zen 3, `taskset -c 0`, average time per element):

```text
===== A. Integer arithmetic cost =====
  x/8   (divisor = power of 2, compiler turns into shift): 0.33 ns
  x>>3  (hand-written shift)                              : 0.38 ns
  x/7   (divisor = runtime variable)                      : 1.64 ns  ← division bottleneck
  x*3   (multiplication)                                  : 0.33 ns
  division (variable)/multiplication = 5.0x
```

Three things to read out of this:

**1. Power-of-two divisor, the compiler turns it into a shift for you.** `x/8` is the same speed as `x>>3` (both 0.33-0.38 ns), because GCC sees `8 = 2^3` and compiles it to `>>3`. **So don't feel "inelegant" writing `x/8` — the compiler optimizes it for you.** This also holds for constant divisors — `x/10` constant division becomes "multiply by the reciprocal of 10" (a technique that approximates division with multiply + shift), no real division.

**2. Runtime-variable divisor, division is unavoidable, 5x over multiplication.** When `x/d` (d is a variable), the compiler can't turn it into a shift or a reciprocal; it has to actually execute the `idiv` instruction. 1.64 ns vs multiplication's 0.33 ns, **5x**. That's the hard number of the "division bottleneck."

**3. Practical corollary: on hot paths, replace variable division with multiplication or shifts.** Common moves:

- Power-of-two divisor → shift (or just write `/8` and let the compiler shift).
- Divisor that isn't a power of two but is constant → trust the compiler to use a reciprocal.
- `x % m` (modulo) is as expensive as `x / m` (both are division underneath); the modulo bottleneck works the same way.
- Hash tables using "`& (size-1)`" instead of "`% size`" (with size a power of two) — that's why modern hash tables like `absl::flat_hash_map` and `folly::F14` make bucket counts powers of two: they save one division. **Note that `std::unordered_map` goes the other way**: libstdc++'s implementation takes bucket counts to **primes** (locally measured: `reserve(100)` gives 103, `reserve(1000)` gives 1031, `reserve(10000)` gives 10273 — none powers of two), preferring one real division for hash quality. That's the other side of the "hash quality vs division cost" tradeoff.

## Int vs float: don't go by intuition

A lot of people think "float is slower than int," and on modern CPUs that's **basically not true**. Zen 3 has independent floating-point/vector execution units, and FP add and multiply are both fully pipelined with 3-4 cycle latency, with throughput comparable to integer add (FMA even does the work of a multiply and an add in one instruction). So:

- "Swap a `double` for an `int` to go faster" usually doesn't help and can be slower (precision loss, extra conversions).
- The floating-point ops that are genuinely slow are **division, square root, transcendental functions** (`sin`/`exp`): these have tens of cycles of latency and low throughput, the "division bottleneck" of floating point.
- **Subnormal** (denormal) floating-point ops carry an extra penalty (Agner's microarchitecture manual has the data); `-ffast-math` enables flush-to-zero to turn this off, but it also changes FP semantics.

In short: **ordinary FP add/multiply isn't slow; what's slow is FP division and transcendental functions.** Put your optimization effort into the latter.

## switch vs if-else: when is a jump table actually faster

The last topic, often misexplained. Textbooks often say `switch` with many branches generates a **jump table** — an O(1) table-lookup jump that beats a long chain of `if-else` (which on average walks half the branches). We measure:

```text
===== B. switch vs if-else chain =====
  switch:  0.48 ns
  if-else: 0.43 ns
  if-else/switch = 0.90x
```

**if-else is actually slightly faster? Hold off on explaining this as "the jump table's indirect-jump predictor drags it down" — that's exactly the "sounds like an explanation" pseudo-causality trap ch00-01 warns about.** You have to read the -O2 assembly to see what really happens: in the bundled code, `switch (x % 8)` has cases 0..7 (contiguous) and return values 100..107 (also contiguous), so GCC finds this equivalent to `return 100 + (x & 7)` and **folds it into a few arithmetic instructions — generating neither a jump table nor keeping the if-else chain** (`g++ -O2 -S` output has indirect jump `jmp *` count = 0, and jump-table data is also 0); the if-else version gets folded the same way. **The two codegens are nearly line-by-line identical**, so the 0.90x is measurement noise, not the prediction cost of a jump table.

The real lesson is exactly the ch02-03 discipline: **the cost of a branch doesn't depend on syntax (switch/if); it depends on what the compiler actually generated and how well that thing predicts on the hardware.** Casually explaining it as "jump table vs branch tree" is walking straight into pseudo-causality.

**When does a jump table actually appear?** When the case labels are **sparse and non-contiguous** (say `case 1: case 23: case 199: ...`), the compiler can't fold them into arithmetic and actually emits a jump table — an address table plus one indirect jump (`jmp *`). The indirect-jump target is dynamic, so the branch predictor has a harder time; with many cases, the jump table's O(1) beats the if-else chain's O(n). So the conclusion is still "switch isn't necessarily faster than if-else," but **the reason has to be read off the assembly**: few and contiguous cases (this example) fold into arithmetic, both the same; many and dense cases let the jump table win big; sparse cases still get the jump table's O(1) but the indirect jump has a prediction cost. **Always `g++ -O2 -S` before judging.**

Compressing this article into a few lines: division is a 5x bottleneck over multiplication (runtime-variable division); power-of-two divisors reduce to shifts (the compiler often does it for you); constant divisions become multiply-by-reciprocal; hot-path variable division/modulo should be rewritten away. Int and float have comparable ordinary add/multiply; what's slow is FP division/square root/transcendental functions. switch isn't necessarily faster than if-else; in this example with few and contiguous cases the compiler folds both into arithmetic/branchless, so the difference is noise — `g++ -O2 -S` before judging. This is one class the compiler can't do for you: which data types you pick and which operations you write is the hand you're holding.

## References

- Agner Fog, *Optimizing software in C++*, §14 *Optimizing arithmetic* (instruction-level costs of int vs float, multiply/divide, jump tables); local copy.
- Agner Fog, *Instruction tables* — latency/throughput/µop breakdowns of each instruction, for desk reference; local copy.
- ch02-03 Pipeline, ILP, and branch prediction (this volume; structural hazards and the branch predictor).
- Measured code for this article: `code/volumn_codes/vol6-performance/ch04/arithmetic_cost.cpp`.
