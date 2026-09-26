---
chapter: 2
cpp_standard:
- 17
description: 'The previous two articles settled data movement; this one steps inside the CPU''s execution core: how the instruction pipeline overlaps instructions in flight, how out-of-order execution mines instruction-level parallelism (ILP) out of your code, and why a mispredicted branch has to flush the pipeline. Two measured experiments make it concrete — dot1 vs dot4 at 2.9x, sorted vs shuffled arrays at 4.2x — and we close by introducing the data / control / structural pipeline hazards.'
difficulty: advanced
order: 3
platform: host
prerequisites:
- 'Memory hierarchy and the latency ladder: why sequential access is 100x faster'
- 'Cachelines and locality: the 64-byte minimum unit of transfer'
reading_time_minutes: 11
related:
- 'Loop and compute optimization: code motion, eliminating memory references, and multiple accumulators'
- 'Branches: branchless, predication, and "don''t go branchless blindly"'
tags:
- host
- cpp-modern
- advanced
- 优化
- atomic
title: 'Pipeline, ILP, and branch prediction: same data, several-fold differences depending on how you execute it'
translation:
  source: documents/vol6-performance/ch02-cpu-microarchitecture/02-03-pipeline-ilp-branch.md
  source_hash: ff71a74f8f884866fff5246e94238587c8a284ac1319cdf25076c3a34e34caa7
  translated_at: '2026-09-26T05:57:33+00:00'
  engine: anthropic
  token_count: 8600
---
# Pipeline, ILP, and branch prediction: same data, several-fold differences depending on how you execute it

## The data moves now — can the CPU chew through it fast enough

The previous two articles took the memory hierarchy apart end to end: contiguous data layout, row-major traversal, keeping the hot data set under control — all of that solves "can data be delivered to the CPU's mouth in time". But ch04 will show you some counterintuitive phenomena: sometimes the data layout has not changed at all, and just writing a few extra accumulators into a loop, or rephrasing a single branch, makes performance differ several-fold again. That tells us the other half of what decides performance is **how the CPU executes instructions** — it has nothing to do with data movement.

So this article steps into the CPU's execution core and covers three interlocking mechanisms: the **pipeline** overlaps instructions in flight, **instruction-level parallelism (ILP)** makes independent instructions genuinely run at the same time, and **branch prediction** bets on a direction whenever an `if` shows up. Together the three decide "how fast your instruction stream can run". We stop at the depth that is "enough to ground your judgments"; the deeper water (register renaming, the reorder buffer ROB, execution-port scheduling) is left to Agner's microarchitecture manuals — we hand you the pointers.

## The pipeline: an assembly line for instructions

Executing one instruction is not the "one cycle and done" story; it is split into stages like a factory assembly line, typically: fetch → decode → execute → memory → write-back. Each instruction flows through those stages in turn, and **different stages of different instructions advance in parallel within the same cycle**: while instruction N is executing, instruction N+1 is decoding, and instruction N+2 is being fetched. That is the **pipeline**.

In the ideal case a pipeline retires one instruction per cycle (the classic scalar pipeline); modern CPUs go one step further with **superscalar** designs, where each stage handles several instructions, so several instructions can retire per cycle. In the AMD Zen chapter, Agner gives Zen's retirement width as **8 µops per cycle** (a µop is a micro-operation internal to the CPU; one x86 instruction may decompose into several µops). That is the concrete meaning of "the CPU computes blazingly fast": eight micro-operations completed in a single cycle.

But that "8 per cycle" is an **upper bound**. Whether you reach it depends on two things: whether the pipeline **stays fed** (no hazard blocking it), and whether the code **offers enough independent instructions to run in parallel** (ILP).

## ILP: out-of-order execution mines parallelism from your code

For that "8 µops per cycle" throughput to hold, the prerequisite is that the 8 µops have **no data dependencies among them**. Modern CPUs achieve this with **out-of-order execution**: instead of running instructions one by one in the order you wrote them, the core dynamically scans its instruction window and simultaneously issues mutually independent instructions to multiple execution units. The fewer data dependencies in your code, the more instructions can run in parallel, the higher the **instruction-level parallelism (ILP)**, and the faster it goes.

Flip that around: if you write one long **dependency chain**, where every instruction depends on the previous one's result, no amount of CPU width helps — it can only sit and wait. The most common example is the single-accumulator reduction:

```cpp
float dot1(const float* a, const float* b) {
    float acc = 0.0f;
    for (int i = 0; i < N; ++i) acc += a[i] * b[i]; // every iteration depends on the previous acc
    return acc;
}
```

`acc += a[i]*b[i]` is a **true dependency**: this iteration's addition needs the previous value of `acc`, so the CPU cannot overlap adjacent multiply-adds. That is one long chain: ILP is nearly zero, and the execution units spend most of their time idle, waiting for the next addition to finish.

The classic way to break this chain is **multiple accumulators**: use several independent accumulators, each maintaining its own short chain:

```cpp
float dot4(const float* a, const float* b) {
    float a0 = 0, a1 = 0, a2 = 0, a3 = 0;
    for (int i = 0; i < N; i += 4) {
        a0 += a[i] * b[i];       // chain 0
        a1 += a[i + 1] * b[i + 1]; // chain 1, independent of chain 0
        a2 += a[i + 2] * b[i + 2]; // chain 2
        a3 += a[i + 3] * b[i + 3]; // chain 3
    }
    return a0 + a1 + a2 + a3;
}
```

Four accumulators are four independent chains, and the CPU dispatches them to different execution ports to genuinely run in parallel. Let's measure it (to demonstrate scalar ILP, we **deliberately disable auto-vectorization** when compiling; otherwise SIMD would do this job even faster and bury the ILP effect — which is itself foreshadowing, more on it below):

```text
===== B. ILP(点积 32768 float,标量无向量化)=====
   单累加器 dot1:   23.7 us/次  (一条长依赖链,CPU 只能等上次加完)
   4 累加器 dot4:    8.1 us/次  (4 条独立链,CPU 并行填满执行端口)
   差 2.92x
```

**Same multiply-add count, and 4 accumulators run nearly 3x faster than 1.** That is ILP caught in the act. The assembly gives it away too:

```text
; dot1: one serial chain
    mulss  (%rsi,%rax), %xmm0      ; computes a[i]*b[i] → xmm0
    addss  %xmm0, %xmm1            ; acc += xmm0; the next iteration's add depends on this xmm1

; dot4: four independent chains (accumulators xmm1/xmm4/xmm3/xmm2 are mutually independent)
    mulss  (%rsi),       %xmm0 ; addss %xmm0, %xmm1
    mulss  -12(%rsi),    %xmm0 ; addss %xmm0, %xmm4   ← independent of xmm1
    mulss  -8(%rsi),     %xmm0 ; addss %xmm0, %xmm3   ← independent of xmm1/xmm4
    mulss  -4(%rsi),     %xmm0 ; addss %xmm0, %xmm2   ← independent of the three above
```

Every `addss` in dot1 must wait for the previous write-back to `%xmm1`; in dot4 the four additions write to four different registers, and the out-of-order engine issues the whole batch at once. This lesson gets a dedicated treatment in ch04-02, *Loop and compute optimization*, under the names **multiple accumulators** / **breaking the dependency chain** — one of the easiest performance dividends to pick up in scientific computing, reductions, and dot-product-style code.

> One aside from me: you might be thinking, "I don't need to write dot4 myself — won't the compiler unroll the loop automatically?" It will, but usually only at `-O3 -funroll-loops`, and it cannot violate floating-point associativity (that takes `-ffast-math`), so under default `-O2` an FP reduction often stays a single chain. That is why the dot4 above has to be written by hand to reliably harvest the ILP. The topic of "what the compiler *can* do versus what it *will* do, separated by optimization levels and language semantics" gets the systematic treatment in ch04.

## Branch prediction: guess right and it's free, guess wrong and the pipeline flushes

The second mechanism is **branch prediction**. To keep throughput high, the pipeline does not stop and wait when it hits an `if` — it **guesses** which way to go, then speculatively keeps executing down that path. Guess right, and all the speculative work is kept, nearly free; guess wrong, and every instruction speculation stuffed into the pipeline must be **flushed**, and fetching restarts from the correct direction. The cost of a flush is a dozen to twenty-some cycles of work executed for nothing — the deeper the pipeline, the more a wrong guess hurts.

Which brings us to a counterintuitive but hugely important conclusion: **a branch's cost is determined not by the branch itself, but by how predictable it is.** A branch that always goes the same direction (a loop-exit condition, say) hits 100% in the predictor and is nearly free; a 50/50 random branch can only be guessed half right, and every miss pays the flush cost. Let's watch this with the most classic experiment there is — over one array, do "if it's above 128, accumulate it":

```cpp
uint64_t sum_gt128(const std::vector<uint8_t>& d) {
    uint64_t s = 0;
    for (int i = 0; i < N; ++i) {
        if (d[i] >= 128) s += d[i];   // a two-way branch
    }
    return s;
}
```

Prepare two datasets: a **shuffled array** (whether each element is ≥128 is near-random, so the branch is an unpredictable 50/50) and a **sorted array** (the first half all <128, the second half all ≥128 — a crystal-clear branch pattern). Same code, same amount of data; only the order differs:

```text
===== A. 分支预测(条件累加 32768 元素,3000 次平均)=====
   打乱(随机分支,预测器猜不中): 0.053 ms/次
   排序(模式清晰,几乎不预测失败): 0.013 ms/次
   差 4.2x
```

**4.2x.** Same accumulation, same data-movement cost — the entire gap comes from the flushes after branch mispredictions. The sorted array's branch is "a long run of not-taken, then a long run of taken"; the predictor learns it in two or three rounds and its hit rate approaches 100%. The shuffled array can never be guessed right, flushing the pipeline nearly once every two branches. This is the answer to the famous "why is processing a sorted array faster" question on Stack Overflow — and the root cause is not the data, it is the **predictability of the branch**.

Two corollaries of this conclusion run through the chapters that follow:

1. **A predictable branch is nearly free.** If an `if` inside a loop overwhelmingly goes the same direction, don't spend effort eliminating it. What deserves the effort is the **data-dependent, random branch**.
2. **A data-dependent random branch can be eliminated with a branchless rewrite.** Turn the `if` into a `cmov` (conditional move) or an arithmetic trick, so the CPU has nothing to guess — no speculation, no flush. ch04-06, *Branches: branchless, predication*, covers this in a dedicated article; here we just plant the motivation.

> One trap in this experiment must be spelled out: compiled at **default `-O2`**, GCC will **auto-vectorize** the `if`-accumulation into a SIMD compare-add, or convert it into a `cmov` — either way the "branch" is gone, and shuffled and sorted end up equally fast (that is exactly how I faceplanted on my first run: 1.0x). So here I deliberately add `-fno-tree-vectorize -fno-tree-slp-vectorize -fno-if-conversion` to keep the scalar branch in place (blocking loop-level vectorization, SLP vectorization, and if-conversion, the three routes respectively), and only then does the 4.2x show up. The teaching point: **the "branch cost" you observe depends on what the compiler actually turned your code into** — it may already have made it branchless, or it may not have. Read the assembly to confirm; don't assume.

## Pipeline hazards: three kinds of stalls, matching the previous two sections

String the pipeline, ILP, and branch prediction together, and CSAPP Chapter 4 gives them the unifying name "**hazard**": under certain conditions the pipeline is forced to stall. Hazards come in three kinds, mapping neatly onto what we covered above:

- **Data hazard**: adjacent instructions have a true data dependency (this one needs the previous one's result), and the pipeline must wait. That is dot1's plight from the ILP section — the long `acc +=` dependency chain is a string of RAW (read-after-write) hazards. The fix is to break the dependency chain and raise ILP.
- **Control hazard**: a branch makes the fetch direction uncertain. That is the subject of the branch-prediction section; the fix is either to make the branch predictable, or to go branchless and remove the branch outright.
- **Structural hazard**: several instructions fight over the same execution resource at the same time. Zen's integer side, for example, has 4 ALUs but only one divider, so multiple integer divisions in the same cycle have to queue; FP division is scarcer still and has long latency (tens of cycles). That is why ch04-03, *Data types and arithmetic*, devotes space to "division is the bottleneck — replace it with multiplication or bit tricks when you can": it is not that division is slow as a computation, it is that dividers are few and high-latency, so structural hazards come easily.

Remember those three names and you're set. CSAPP Chapter 4 works out the full hazard-detection and forwarding machinery — that belongs to a computer-architecture course, and vol6 will not re-derive it. What we care about is "how these three kinds of stalls translate into C++-level performance pits", and that is ch04's business.

## Loose threads for the next article

This article laid out the three mechanisms on the CPU's execution side: the **pipeline** overlaps instructions in flight (a Zen-class CPU has a theoretical retirement width of 8 µops per cycle, but that is an upper bound); **ILP** decides whether you can approach that bound (dot1's long dependency chain crushes ILP to zero, dot4's multiple accumulators pull it back up — a measured 2.9x apart); and **branch prediction** penalizes unpredictable branches hard (sorted vs shuffled, 4.2x; predictable ones are nearly free; the branchless rewrite for random branches is saved for ch04-06). Add the **three hazards** (data / control / structural), and you have the hardware foundation for ch04's "optimize by bottleneck site".

With this article, ch02's single-core hardware groundwork is complete: the memory hierarchy (02-01), cachelines and locality (02-02), and pipeline / ILP / branches (this article). In the next one we add the last piece of the puzzle — virtual address translation and the TLB — plus a cheat sheet of microarchitecture differences across CPU families, a desk-side reference for anyone who needs to tune across platforms.

## References

- Agner Fog, *The microarchitecture of Intel, AMD and VIA CPUs*, §22 *AMD Ryzen*: Zen-family pipeline widths (4-wide decode, 6 µops/clock dispatch, 8 µops/clock retire), branch throughput (taken 1/2 clock, not-taken 2/clock), the µop cache, execution-unit counts. Local copy: `.claude/drafts/books/optimazation_in_cpp/microarchitecture.md`
- Bryant & O'Hallaron, *CSAPP*, Chapter 4 *Processor Architecture* (concept-level definitions of the pipeline and hazards) and Chapter 5 *Optimizing Program Performance* (the classic derivations of loop unrolling, multiple accumulators, and reassociation)
- The legendary Stack Overflow question *Why is processing a sorted array faster than processing an unsorted array?* (the origin of the branch-prediction experiment; muffinista / Mysticial's classic answer)
- The measurement code for this article: `code/volumn_codes/vol6-performance/ch02/pipeline_branch_ilp.cpp`
