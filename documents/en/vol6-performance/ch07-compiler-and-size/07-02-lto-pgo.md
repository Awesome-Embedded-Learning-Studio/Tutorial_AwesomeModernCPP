---
chapter: 7
cpp_standard:
- 17
description: "LTO (link-time optimization) lets the compiler inline across translation units, eliminating the 'cross-TU call'
  optimization blocker — measured 3.9x faster on the same cross-file call; PGO (profile-guided optimization) pays
  off significantly on large codebases but does nothing on microbenchmarks (an honest null result). This article
  covers the mechanics of both, the engineering rollout (how to build with them), and the costs"
difficulty: advanced
order: 2
platform: host
prerequisites:
- '-O levels and optimization blockers: what the compiler can and can''t do'
- 'Frontend optimization: code layout, PGO, and BOLT'
reading_time_minutes: 5
related:
- 'Linking performance, multi-compiler comparison, and compile-time metaprogramming'
- 'Binary size optimization: -Os, --gc-sections, and template bloat control'
tags:
- host
- cpp-modern
- advanced
- 优化
- 工具链
title: "LTO, ThinLTO, and PGO: engineering them into your build"
translation:
  source: documents/vol6-performance/ch07-compiler-and-size/07-02-lto-pgo.md
  source_hash: 1ec0c7fda10a91b83af0e9ba5eed141f8bd5dd0b4bdca663fc4f7a2c6db82739
  translated_at: '2026-09-26T08:36:30+00:00'
  engine: anthropic
  token_count: 1400
---
# LTO, ThinLTO, and PGO: engineering them into your build

ch07-01 established that "cross-translation-unit" is one of the optimization blockers: while compiling one `.cpp`, the compiler cannot see the implementations living in the other `.cpp` files, so it doesn't dare to inline across files. This article covers the fix, **LTO**, plus **PGO**, which "lays out code according to the real profile". Both are release-build-level engineering rollouts, and both pay off significantly on large projects.

## LTO: cross-file inlining at link time

The idea behind **LTO (Link-Time Optimization)** is this: at compile time, every `.o` file carries the intermediate representation (GIMPLE/LLVM IR) rather than only machine code; at link time, the linker merges all the IR and **redoes cross-file optimization and inlining from scratch**. Now cross-TU function calls can be inlined, and constant propagation and dead-code elimination can run across files.

Let's measure the simplest cross-TU scenario: `main.cpp` calls `helper(int)` from `helper.cpp`:

```bash
# No LTO: helper lives in another TU, the compiler cannot see the implementation, no inlining
g++ -O2 main.cpp helper.cpp -o lto_nolto
# With LTO: merged at link time, helper can be inlined → constant propagation goes further
g++ -O2 -flto main.cpp helper.cpp -o lto_lto
```

Measured (with `helper` called a hundred million times):

```text
No LTO:   178.6 ms
With LTO:  46.2 ms   ← 3.9x
```

**3.9x.** The entire gap comes from "cross-TU inlining + the optimizations that follow": LTO lets the compiler see `helper`'s implementation, inlines it into `main`'s loop, and then constant propagation / loop simplification squeezes the whole thing down to near-optimal. Without LTO, every loop iteration is a real function call (and `helper` has a loop of its own inside).

A side benefit: LTO also performs **cross-file dead-code elimination**, so the binary often ends up smaller (16136 → 16024 bytes in this case, with the unused code reclaimed).

### ThinLTO: scalable LTO

Full LTO merges all the IR into one giant view, which on large projects means **slow links and heavy memory use**: a Chrome-scale project linking under full LTO takes tens of minutes and tens of GB of RAM. **ThinLTO** (LLVM, `-flto=thin`) shards the work: first lightweight summaries (import/export decisions), then each module is optimized in parallel. On large projects ThinLTO **links much faster and uses less memory** than full LTO, with near-equivalent optimization results. GCC has an analogous mechanism (`-flto=auto` for parallelism).

### The cost of LTO

- **Linking gets slower** (full LTO especially), and build memory use goes up.
- **Build complexity**: build systems such as CMake have to pass `-flto` correctly to both compiling and linking, and `ar` has to be gcc-ar (to handle LTO objects).
- **Debugging**: after LTO, symbols can get scrambled, and the debugger experience degrades.

In practice, **turn LTO/ThinLTO on for release builds and leave it off for debug builds**. Use ThinLTO on large projects. This is a "free lunch"-grade optimization (a one-line flag, a few to a dozen-plus percentage points of speedup), and the only price is link time.

## PGO: laying out code by the real profile

The mechanics of **PGO (Profile-Guided Optimization)** were covered in ch04-07 (three phases: instrument → run the profile → recompile with the profile); this article covers the engineering rollout and one honest conclusion.

### The honest conclusion: PGO has no payoff on microbenchmarks

We took ch04's `pgo_demo` (the small function with the 99/1 branch split), ran the full three-phase PGO strictly by the book, and compared it against a pure -O2 baseline:

```text
Pure -O2 baseline: 3.57-3.82 ms
-O2 + PGO:         3.78-4.17 ms   ← no payoff at all (even slightly slower, within noise)
```

**PGO does nothing on microbenchmarks.** The reason: this small function has just two branches and a few lines of code, and `-O2` already optimizes it well; PGO's value is **code layout on large codebases** (physically clustering the hot paths scattered across thousands of functions to improve icache), and there is nothing to lay out in a small function of a few dozen lines.

> On our first run, the "PGO build" looked 4x faster — a brief moment of excitement — until we realized that 4x was **counter overhead from the instrumented binary** (the phase-1 build ships with performance counters built in), not a PGO gain. **Measuring PGO requires a "pure -O2, non-instrumented" baseline as the control**, plus confirming that the phase-3 profile was actually applied (the compiler warning `profile count data file not found` means it was not found). We are recording this face-plant here to echo the ch01 discipline: **the PGO gain you think you are measuring may actually be instrumentation overhead**.

### Where PGO really pays off: large codebases

Every public PGO win comes from big projects: **Chrome, Firefox, the major databases**, reporting **single-digit to a dozen-plus percent** speedups — on the precondition that the codebase is large enough for icache/branch layout to be a genuine bottleneck. So, three rules:

- **Don't expect PGO to work on microbenchmarks or small projects.**
- **Enable it in large release builds**, and sample the profile with a **representative production workload** (not just any casual run).
- The rollout flow (CMake): compile with `-fprofile-generate` → run the workload → recompile with `-fprofile-use`. CI integration should store profiles and reuse them across builds.

PGO + LTO stacked together is the standard combo for large-project releases.

## References

- GCC documentation for `-flto` / `-fprofile-generate` / `-fprofile-use`
- LLVM documentation, *ThinLTO* (llvm.org/docs/ThinLTO.html)
- ch04-07 Frontend optimization: code layout, PGO, and BOLT (this volume; the PGO mechanics and the first discussion of "no payoff on microbenchmarks")
- This article's measurement code: `code/volumn_codes/vol6-performance/ch07/lto_main.cpp` + `lto_helper.cpp`; PGO reuses `ch04/pgo_demo.cpp` + `ch04/pgo.sh`

The one-line wrap-up: **LTO's cross-TU inlining measured 3.9x** (on the cross-file helper call) — turn it on for release, use ThinLTO on large projects, and the only cost is slower linking; **PGO lays out code by profile and has no payoff on microbenchmarks (an honest null)** — its value is on large codebases (Chrome/Firefox class, single-digit to a dozen-plus percent), and that 4x we once saw was instrumentation overhead, not PGO. **PGO + LTO** is the standard combo for large-project releases; wiring it in is engineering work (passing flags through CMake, storing profiles), a one-time setup that keeps paying off.
