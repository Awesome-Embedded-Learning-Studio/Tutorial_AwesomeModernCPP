---
chapter: 2
cpp_standard:
- 17
description: 'The first two articles covered data movement. This one steps into the CPU''s execution core: how the instruction pipeline overlaps instructions, how out-of-order execution mines instruction-level parallelism (ILP) from your code, and why a branch misprediction flushes the pipeline. Two measurements — dot1 vs dot4 at 2.9x and sorted vs shuffled arrays at 4.2x — make it concrete, and we introduce the three pipeline hazards (data, control, structural).'
difficulty: advanced
order: 3
platform: host
prerequisites:
- 'Memory hierarchy and the latency ladder: why sequential access is 100x faster'
- 'Cachelines and locality: the 64-byte minimum unit of transfer'
reading_time_minutes: 11
related:
- 'Loops and compute optimization: code motion, unrolling, and multiple accumulators'
- 'Branches: branchless and predication'
tags:
- host
- cpp-modern
- advanced
- 优化
- atomic
title: 'Pipeline, ILP, and branch prediction: same data, several times the execution speed'
translation:
  source: documents/vol6-performance/ch02-cpu-microarchitecture/02-03-pipeline-ilp-branch.md
  source_hash: ff71a74f8f884866fff5246e94238587c8a284ac1319cdf25076c3a34e34caa7
  translated_at: '2026-07-06T00:00:00+00:00'
  engine: manual
  token_count: 3300
---
# Pipeline, ILP, and branch prediction: same data, several times the execution speed

## Data can be moved — but can it be computed

The previous two articles took the memory hierarchy apart end to end: contiguous data, row-major traversal, controlling the hot working set. Those solve "can data reach the CPU's mouth in time." But in ch04 you'll see some counterintuitive phenomena: sometimes the data layout is unchanged, and just writing a few extra accumulators in a loop, or rewriting a branch a different way, costs another several times in performance. That tells you the other half of what decides performance is **how the CPU executes instructions**, independent of data movement.

This article walks into the CPU's execution core and covers three interrelated mechanisms: **the pipeline** overlaps instructions, **instruction-level parallelism (ILP)** makes independent instructions genuinely run at the same time, and **branch prediction** gambles on a direction when an `if` shows up. Together they decide "how fast your instruction sequence can run." We stop at the depth "enough to support judgment"; the deeper water (register renaming, the reorder buffer ROB, execution-port scheduling) we leave to Agner's microarchitecture manual, with pointers.

## The pipeline: an assembly line for instructions

Executing one CPU instruction isn't "done in one cycle" the way you might think. It's split into stages like a factory assembly line, typically: fetch → decode → execute → memory → write-back. Each instruction flows through these stages in turn, and **different stages of different instructions advance in parallel within the same cycle**: while instruction N is executing, N+1 is decoding, N+2 is being fetched. That's the **pipeline**.

Ideally the pipeline retires one instruction per cycle (a classic scalar pipeline); contemporary CPUs go further with **superscalar** designs, where each stage handles multiple instructions, so multiple instructions retire per cycle. In the AMD Zen chapter, Agner gives Zen's retire width as **8 µops/cycle** (a µop is a micro-operation internal to the CPU; one x86 instruction may split into several µops). That's the concrete meaning of "the CPU computes blazingly fast": 8 micro-operations per cycle.

But this "8/cycle" is an **upper bound**. Whether you hit it depends on two things: whether the pipeline is **fed enough** (no hazard blocking it), and whether the code has **enough independent instructions for it to parallelize** (ILP).

## ILP: out-of-order execution mines parallelism from your code

For that "8 µops/cycle" throughput to hold, the 8 µops must have **no data dependencies among them**. Contemporary CPUs achieve this with **out-of-order execution**: instead of running instructions one by one in the order you wrote them, the CPU dynamically scans the instruction window and simultaneously issues independent instructions to multiple execution units. The fewer the data dependencies in your code, the more instructions can run in parallel, the higher the **instruction-level parallelism (ILP)**, and the faster it goes.

Conversely, if you write a **long dependency chain** where every instruction depends on the previous one's result, it doesn't matter how wide the CPU is, it just waits. The most common example is a "single-accumulator reduction":

```cpp
float dot1(const float* a, const float* b) {
    float acc = 0.0f;
    for (int i = 0; i < N; ++i) acc += a[i] * b[i]; // each iteration depends on the previous acc
    return acc;
}
```

`acc += a[i]*b[i]` is a **true dependency**: this iteration's add needs the previous `acc`, and the CPU can't parallelize adjacent multiply-adds. That's a long chain, ILP is essentially zero, and the execution units sit idle most of the time waiting for the next add to finish.

The classic way to break this chain is **multiple accumulators**: use several independent accumulators, each maintaining a short chain:

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

Four accumulators are four independent chains, and the CPU dispatches them to different execution ports to run genuinely in parallel. Let's measure (to demonstrate scalar ILP, we **deliberately disable auto-vectorization** when compiling; otherwise SIMD would do this even faster and mask the ILP effect, which itself is a foreshadowing we'll get to):

```text
===== B. ILP (dot product of 32768 floats, scalar, no vectorization) =====
   single accumulator dot1:   23.7 us/run  (one long dependency chain, CPU waits for each add)
   4 accumulators   dot4:    8.1 us/run  (4 independent chains, CPU fills the execution ports in parallel)
   2.92x difference
```

**Same number of multiply-adds, 4 accumulators is nearly 3x faster than 1.** That's direct evidence of ILP. The assembly gives it away too:

```text
; dot1: one serial chain
    mulss  (%rsi,%rax), %xmm0      ; compute a[i]*b[i] → xmm0
    addss  %xmm0, %xmm1            ; acc += xmm0; next iteration's add depends on xmm1 here

; dot4: four independent chains (accumulators xmm1/xmm4/xmm3/xmm2 are mutually independent)
    mulss  (%rsi),       %xmm0 ; addss %xmm0, %xmm1
    mulss  -12(%rsi),    %xmm0 ; addss %xmm0, %xmm4   ← independent of xmm1
    mulss  -8(%rsi),     %xmm0 ; addss %xmm0, %xmm3   ← independent of xmm1/xmm4
    mulss  -4(%rsi),     %xmm0 ; addss %xmm0, %xmm2   ← independent of the three above
```

Each `addss` in dot1 waits for the previous writeback to `%xmm1`; in dot4 the four adds write to four different registers, and the out-of-order engine fires them all at once. This lesson gets a full treatment in ch04-02 "loops and compute optimization," where it's called the **multiple-accumulator transform** / **breaking the dependency chain**, one of the easiest performance dividends to pick up in scientific computing, reductions, and dot-product-style code.

> A note from me: you might think "I don't have to write dot4 myself, won't the compiler unroll automatically?" It will, but usually needs `-O3 -funroll-loops`, and it can't violate floating-point associativity (`-ffast-math` is required for that), so under default `-O2` an FP reduction often stays a single chain. That's why we hand-write dot4 above to reliably capture ILP. The "what the compiler *can* do vs what it *will* do, separated by optimization level and language semantics" topic gets a systematic treatment in ch04.

## Branch prediction: guess right and it's free, guess wrong and the pipeline flushes

The second mechanism is **branch prediction**. To keep throughput high, the pipeline doesn't stop and wait when it hits an `if`. It **guesses** which way to go and then speculatively keeps executing. If it guesses right, the speculative work is all kept, almost free; if it guesses wrong, every instruction the speculative phase stuffed into the pipeline must be **flushed**, and fetch restarts from the correct direction. The cost of a flush is having executed a dozen to twenty-some cycles of work for nothing; the deeper the pipeline, the more a wrong guess hurts.

That leads to a counterintuitive but extremely important conclusion: **the cost of a branch doesn't depend on the branch itself, but on whether it's "easy to predict."** A branch that always goes the same way (like a loop-exit condition) has a 100% predictor hit rate and is almost free; a 50/50 random branch can only be guessed half right, and every wrong guess pays the flush cost. The classic experiment, on the same array, "if it's ≥ 128, add it up":

```cpp
uint64_t sum_gt128(const std::vector<uint8_t>& d) {
    uint64_t s = 0;
    for (int i = 0; i < N; ++i) {
        if (d[i] >= 128) s += d[i];   // a two-way branch
    }
    return s;
}
```

Prepare two datasets: a **shuffled array** (each element ≥128 or not is roughly random, branch is 50/50 unpredictable) and a **sorted array** (first half all <128, second half all ≥128, branch pattern is very clear). Same code, same amount of data, only the order differs:

```text
===== A. Branch prediction (conditional sum over 32768 elements, 3000-run average) =====
   shuffled (random branch, predictor can't hit): 0.053 ms/run
   sorted   (clear pattern, almost no mispredictions): 0.013 ms/run
   4.2x difference
```

**4.2x.** Same accumulation, same data-movement cost, the gap comes entirely from branch-prediction-miss flushes. The sorted array's branch is "first consecutive not-taken, then consecutive taken"; the predictor learns it in two or three tries and the hit rate approaches 100%. The shuffled array can't be guessed right, flushing the pipeline nearly every other iteration. That's the answer to the famous Stack Overflow question "why is processing a sorted array faster" — the root cause isn't the data, it's the **predictability of the branch**.

This conclusion has two corollaries that run through later chapters:

1. **A predictable branch is almost free.** If an `if` inside a loop almost always goes the same way, don't bother eliminating it. What's worth eliminating is the **data-dependent, random branch**.
2. **A data-dependent random branch can be removed with a branchless rewrite.** Rewriting the `if` as a `cmov` (conditional move) or an arithmetic trick means the CPU doesn't have to gamble, no speculation, no flush. ch04-06 "branches: branchless and predication" covers this in depth; here we just plant the motivation.

> A trap in this experiment has to be spelled out: if the code above is compiled at **default `-O2`**, GCC will **auto-vectorize** the `if`-sum into a SIMD compare-add, or turn it into a `cmov`. Either way the "branch" is gone, and shuffled and sorted end up equally fast (I stepped in this exact pit the first time around, 1.0x). So I deliberately add `-fno-tree-vectorize -fno-tree-slp-vectorize -fno-if-conversion` here to keep the scalar branch alive (those three flags block loop vectorization, SLP vectorization, and if-conversion respectively), and only then does the 4.2x show up. The teaching point here: **the "branch cost" you see actually depends on what the compiler turned your code into**, it may have already made it branchless, or it may not have. Read the assembly and confirm, don't assume.

## Pipeline hazards: three kinds of stalls, matching the previous two sections

Stringing the pipeline, ILP, and branch prediction together, CSAPP Chapter 4 uses the word "**hazard**" to unify them: under certain conditions the pipeline is forced to stall. They come in three kinds, matching exactly what was covered above:

- **Data hazard**: adjacent instructions have a true data dependency (this one needs the previous one's result), and the pipeline must wait. That's the plight of dot1 in the ILP section, the long `acc +=` dependency chain is a string of RAW (read-after-write) hazards. The fix is to break the chain and raise ILP.
- **Control hazard**: a branch makes the fetch direction uncertain. That's the subject of the branch-prediction section, and the fix is either to make the branch predictable or to use branchless to eliminate it outright.
- **Structural hazard**: multiple instructions simultaneously compete for the same execution resource. For example, Zen's integer unit has 4 ALUs but only one divider, so multiple integer divides in the same cycle have to queue; FP divide is even scarcer and has longer latency (tens of cycles). That's why ch04-03 "data types and arithmetic" will specifically cover "division is a bottleneck, replace it with multiplication or bit ops when you can." It's not that division is slow, it's that dividers are few and high-latency, easy to hit a structural hazard.

Remember those three names and you're set. CSAPP Chapter 4 has the full hazard-detection and forwarding machinery, which belongs to a computer-architecture course and vol6 won't reproduce. What we care about is "how these three stalls translate into C++-level performance pits," and that's ch04's job.

## A thread for the next article

This article covered three mechanisms on the CPU's execution side: **the pipeline** overlaps instructions (a Zen-class CPU has a theoretical retire width of 8 µops/cycle, but that's an upper bound), **ILP** decides whether you can approach that bound (the long dependency chain of dot1 crushes ILP to zero, multiple accumulators in dot4 pull it back up, 2.9x measured gap), and **branch prediction** penalizes unpredictable branches hard (sorted vs shuffled, 4.2x; predictable ones are almost free; the branchless rewrite for random branches is saved for ch04-06). Add the **three hazards** (data / control / structural), and you have the hardware foundation for ch04's "optimize by bottleneck site."

That wraps up ch02's single-core hardware foundation: the memory hierarchy (02-01), cachelines and locality (02-02), and pipeline / ILP / branches (this article). The next article adds the last piece of the puzzle, virtual-address translation and the TLB, plus a cheat sheet of microarchitecture differences across CPU families, as a desk reference for anyone tuning across platforms.

## References

- Agner Fog, *The microarchitecture of Intel, AMD and VIA CPUs*, §22 *AMD Ryzen*: Zen-family pipeline widths (4-wide decode, 6 µop/clock dispatch, 8 µop/clock retire), branch throughput (taken 1/2 clock, not-taken 2/clock), µop cache, execution-unit counts. Local copy: `.claude/drafts/books/optimazation_in_cpp/microarchitecture.md`
- Bryant & O'Hallaron, *CSAPP*, Chapter 4 *Processor Architecture* (concept-level definitions of pipeline and hazards) and Chapter 5 *Optimizing Program Performance* (the classic derivations of loop unrolling, multiple accumulators, and reassociation)
- The legendary Stack Overflow question *Why is processing a sorted array faster than processing an unsorted array?* (the source of the branch-prediction experiment, muffinista / Mysticial's classic answer)
- Source for this article's measurements: `code/volumn_codes/vol6-performance/ch02/pipeline_branch_ilp.cpp`
