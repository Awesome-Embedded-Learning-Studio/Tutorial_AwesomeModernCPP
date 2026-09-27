---
chapter: 4
cpp_standard:
- 17
description: 'Choosing data types and arithmetic is the class of optimization the compiler cannot do for you — you have to pick by hand. With real measurements, this article walks through integer division as a bottleneck 5x over multiplication (power-of-two divisors reduce to shifts, constant divisions become multiply-by-reciprocal, runtime-variable division has no escape), the cost gap between int and float, and when a switch jump table truly beats an if-else chain (answer: not always)'
difficulty: advanced
order: 3
platform: host
prerequisites:
- 'Loop and compute optimization: code motion, eliminating memory references, and multiple accumulators'
- 'Pipeline, ILP, and branch prediction: same data, several-fold differences depending on how you execute it'
reading_time_minutes: 6
related:
- 'SIMD and vectorization: auto-vectorization conditions, intrinsics, and CPU dispatch'
- "Branches: branchless, predication, and \"don't go branchless blindly\""
tags:
- host
- cpp-modern
- advanced
- 优化
title: "Data types and arithmetic: int vs float, the division bottleneck, and jump tables"
translation:
  source: documents/vol6-performance/ch04-tuning-by-bottleneck/04-03-types-and-arithmetic.md
  source_hash: 3ed09d040cb9a2cc599f0b70367ba7d53d17f05e9e378b32ab42fd38352e0b5a
  translated_at: '2026-09-26T06:22:38+00:00'
  engine: anthropic
  token_count: 4300
---
# Data types and arithmetic: int vs float, the division bottleneck, and jump tables

## The class of optimization the compiler can't do for you

When we walked through loop optimization in ch04-02, you probably noticed that more than half of it was "-O2 already does this for you." This article looks at arithmetic from a different angle: for some operations, how fast they run **depends on which data type you chose and which operation you wrote**, and **the choice is in your hands — the compiler is powerless**. The canonical example is division.

Division is the most "expensive" basic integer operation on x86: its latency is relatively long (on Zen 3, `idiv` takes about 9-12 cycles for 32-bit and up to 9-17 cycles for 64-bit, versus 1-3 cycles for `imul`/`lea` — data from Agner's *Instruction tables*, Zen 3 section; note that is "a dozen-odd cycles," not "tens of cycles" — the "tens of cycles" figure in older textbooks dates from the era of Pentium's long-latency divider), and its throughput is low (a new division can be issued only every 6-12 cycles; dividers are scarce, so divisions queue up one behind another). That makes it a textbook **structural hazard** as covered in ch02-03: several divisions fighting over the same divide port. Multiplication is the opposite: `imul` at 1 cycle, fully pipelined. **Avoid division whenever you can** — that is the core of this article.

## The division bottleneck: 5x over multiplication

Let's just measure it (local Zen 3, `taskset -c 0`, average time per element):

```text
===== A. 整数算术成本 =====
  x/8   (除数=2 的幂,编译器换位移): 0.33 ns
  x>>3  (手写位移)                 : 0.38 ns
  x/7   (除数=运行期变量)           : 1.64 ns  ← 除法瓶颈
  x*3   (乘法)                      : 0.33 ns
  除法(变量)/乘法 = 5.0x
```

Three things to read off this:

**1. When the divisor is a power of two, the compiler swaps in a shift for you.** `x/8` is exactly as fast as `x>>3` (both 0.33-0.38 ns), because GCC sees that `8 = 2^3` and compiles it to `>>3` on its own. **So don't feel "inelegant" writing `x/8` — the compiler has already optimized it.** This holds for constant divisors in general too: for a constant division like `x/10`, the compiler substitutes "multiply by the reciprocal of 10" (a technique that approximates division with multiply + shift) and never performs an actual division.

**2. When the divisor is a runtime variable, division is unavoidable — 5x the cost of multiplication.** For `x/d` with `d` a variable, the compiler can't substitute a shift or a reciprocal; it has to genuinely execute the `idiv` instruction. 1.64 ns versus 0.33 ns for multiplication — **5x**. Those are the hard numbers of the "division bottleneck."

**3. Practical corollary: on hot paths, rewrite variable division into multiplication or shifts.** The usual moves:

- Divisor is a power of two → use a shift (or simply write `/8` and let the compiler swap it in).
- Divisor isn't a power of two but is constant → trust the compiler's reciprocal substitution.
- `x % m` (modulo) costs the same as `x / m` (both bottom out in division), so the modulo bottleneck works the same way.
- Hash tables use `& (size-1)` in place of `% size` (provided size is a power of two) — which is exactly why modern hash tables such as `absl::flat_hash_map` and `folly::F14` round their bucket counts to powers of two: it saves one division. **Note that `std::unordered_map` deliberately goes the other way**: the libstdc++ implementation picks **prime** bucket counts (measured locally, `reserve(100)` yields 103, `reserve(1000)` yields 1031, `reserve(10000)` yields 10273 — none of them powers of two), gladly paying one real division for the sake of hashing quality. That is the other side of the "hash quality vs division cost" trade-off.

## Int vs float: don't go by intuition

Plenty of people believe "floating point is slower than integers," and on modern CPUs this is **mostly false**. Zen 3 has independent FP/vector execution units; FP add and multiply are both fully pipelined with 3-4 cycle latency, and throughput is on par with integer add (FMA even does the work of a multiply and an add in a single instruction). So:

- "Replace the `double` with an `int` to go faster" usually does nothing, and can even be slower (precision loss, extra conversions).
- The floating-point operations that really are slow are **division, square root, and the transcendental functions** (`sin`/`exp`): tens of cycles of latency and low throughput — the floating-point edition of the "division bottleneck."
- Operations on **subnormal** numbers carry an extra penalty (documented in Agner's microarchitecture manual); `-ffast-math` can enable flush-to-zero to switch this penalty off, but it changes FP semantics as well.

One sentence: **ordinary FP multiply and add aren't slow; FP division and the transcendentals are.** Aim your optimization effort at the latter.

## switch vs if-else: when jump tables actually win

The last topic is one that's often taught wrong. Textbooks say that when a `switch` has many branches, it generates a **jump table** — an O(1) lookup-and-jump that beats a long `if-else` chain (which on average walks half the branches). Let's measure:

```text
===== B. switch vs if-else 链 =====
  switch:  0.48 ns
  if-else: 0.43 ns
  if-else/switch = 0.90x
```

**if-else is actually slightly faster? Before rushing to explain this as "the jump table's indirect-jump predictor dragging it down" — that is exactly the "sounds like an explanation" false-causality trap ch00-01 warned about.** Only reading the -O2 assembly tells us what really happened: in the companion code, the cases of `switch (x % 8)` are 0..7 (contiguous) and the return values are 100..107 (also contiguous), so GCC realized this is equivalent to `return 100 + (x & 7)` and **folded the whole thing into a few arithmetic instructions — generating neither a jump table nor a kept if-else chain** (in the `g++ -O2 -S` output, the count of indirect jumps `jmp *` is 0, and there is no jump-table data either); the if-else version was folded the same way. The two codegens are nearly line-for-line identical, so the 0.90× is measurement noise, not the prediction cost of a jump table.

The real lesson is precisely the ch02-03 discipline: **the cost of a branch doesn't depend on the syntax (switch vs if); it depends on what the compiler actually generated and how well that thing predicts in hardware.** Explaining it by reflex with "jump table vs branch tree" is falling straight into false causality.

**So when does a jump table actually show up?** When the case labels are **sparse and non-contiguous** (say `case 1: case 23: case 199: ...`) — the compiler can't fold that into arithmetic, so it genuinely emits a jump table: a table of addresses plus one indirect jump (`jmp *`). The target of an indirect jump is dynamic, so the branch predictor genuinely has a harder time guessing; and once the case count is also large, the jump table's O(1) soundly beats the if-else chain's O(n). So the conclusion remains "switch isn't necessarily faster than if-else" — but **the reasons have to be grounded in the assembly**: few, contiguous cases (this example) fold into arithmetic and both come out the same; many, dense cases let the jump table win big; sparse cases still give a jump table its O(1), but the indirect jump carries a prediction cost. **Before you judge, take one look at `g++ -O2 -S`.**

Compress this article into a few maxims: division is a bottleneck 5x over multiplication (runtime-variable division); a power-of-two divisor goes through a shift (the compiler usually does it for you); constant division becomes multiply-by-reciprocal in the compiler's hands; hot-path variable division/modulo should be rewritten away. Ordinary integer and FP multiply/add cost about the same — what's slow is FP division/square root/transcendentals. switch isn't necessarily faster than if-else: in this example the few, contiguous cases got folded by the compiler into arithmetic/branchless, the difference was noise, and you should look at the actual codegen with `-S` before judging. This class of optimization is one the compiler can't do for you: which data types you choose and which operations you write are the cards in your hand.

## References

- Agner Fog, *Optimizing software in C++*, §14 *Optimizing arithmetic* (instruction-level costs of integer/floating point, multiply/divide, jump tables). Local copy.
- Agner Fog, *Instruction tables* — latency/throughput/µop breakdowns per instruction, for desk-side verification. Local copy.
- ch02-03, Pipeline, ILP, and branch prediction (this volume; structural hazards and the branch predictor)
- Measurement code for this article: `code/volumn_codes/vol6-performance/ch04/arithmetic_cost.cpp`
