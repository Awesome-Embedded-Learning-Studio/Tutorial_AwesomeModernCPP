---
chapter: 7
cpp_standard:
- 17
description: LTO (link-time optimization) lets the compiler inline across translation units, removing
  the "cross-TU call" optimization blocker, measured 3.9× faster on the same cross-file call; PGO
  (profile-guided optimization) pays off significantly on large codebases but does nothing on microbenchmarks
  (honest null result). This article covers their mechanism, engineering rollout (how to build), and the cost
difficulty: advanced
order: 2
platform: host
prerequisites:
- '-O levels and optimization blockers'
- 'Front-end optimization: code layout, PGO, and BOLT'
reading_time_minutes: 5
related:
- Linking performance, multi-compiler comparison, and compile-time metaprogramming
- Size optimization: -Os, --gc-sections, and template bloat control
tags:
- host
- cpp-modern
- advanced
- 优化
- 工具链
title: LTO, ThinLTO, and the engineering rollout of PGO
translation:
  source: documents/vol6-performance/ch07-compiler-and-size/07-02-lto-pgo.md
  source_hash: 1ec0c7fda10a91b83af0e9ba5eed141f8bd5dd0b4bdca663fc4f7a2c6db82739
  translated_at: '2026-07-06T00:00:00+00:00'
  engine: manual
  token_count: 2400
---
# LTO, ThinLTO, and the engineering rollout of PGO

ch07-01 covered "cross-translation-unit" as one of the optimization blockers: when the compiler builds a `.cpp`, it can't see implementations in other `.cpp` files, so it doesn't dare inline across files. This article covers the fix, **LTO**, plus **PGO**, which "lays out code by the real profile". Both are release-build-level engineering rollouts, and both pay off significantly on large projects.

## LTO: cross-file inlining at link time

**LTO (Link-Time Optimization)** works like this: at compile time, every `.o` file carries not just machine code but also the "intermediate representation (GIMPLE/LLVM IR)", and at link time the linker merges all the IR and **redoes cross-file optimization and inlining**. Cross-TU function calls can now be inlined, and constant propagation and dead-code elimination can run across files.

We measure the simplest cross-TU scenario: `main.cpp` calls `helper(int)` in `helper.cpp`:

```bash
# No LTO: helper is in another TU, compiler can't see the implementation, can't inline
g++ -O2 main.cpp helper.cpp -o lto_nolto
# With LTO: merged at link time, helper gets inlined → further constant propagation
g++ -O2 -flto main.cpp helper.cpp -o lto_lto
```

Measurement (helper called a hundred million times):

```text
No LTO:  178.6 ms
LTO:      46.2 ms   ← 3.9×
```

**3.9×.** This gap is entirely from "cross-TU inlining + downstream optimization": LTO lets the compiler see `helper`'s implementation, inlines it into `main`'s loop, and then constant propagation / loop simplification compresses the whole thing to near-optimal. Without LTO, every loop iteration is a real function call (and helper has its own loop inside).

Side benefit: LTO also does **cross-file dead-code elimination**, so the binary is often smaller (here 16136 → 16024 bytes, reclaiming unused code).

### ThinLTO: scalable LTO

Full LTO merges all the IR into one giant view, which on large projects is **slow to link and memory-hungry**: Chrome-scale projects under full LTO need tens of minutes and tens of GB of RAM at link time. **ThinLTO** (LLVM, `-flto=thin`) shards the work: first do lightweight summaries (import/export decisions), then optimize each module in parallel. On large projects ThinLTO **links much faster and uses less memory** than full LTO, with near-equivalent optimization. GCC has an analogous mechanism (`-flto=auto` for parallelism).

### The cost of LTO

- **Linking gets slower** (full LTO especially), and build memory rises.
- **Build complexity**: build systems like CMake have to correctly pass `-flto` to both compile and link, and `ar` has to use gcc-ar (to handle LTO objects).
- **Debugging**: after LTO, symbols can be shuffled around and the debugger experience degrades.

In practice, **enable LTO/ThinLTO on release builds, not on debug builds**. Use ThinLTO on large projects. This is a "free lunch" optimization (one flag, single-digit to low-teens percent speedup), with the only cost being link time.

## PGO: layout by the real profile

**PGO (Profile-Guided Optimization)** was already covered in ch04-07 (three phases: instrument → run the profile → recompile with the profile). Here we cover the engineering rollout and one honest conclusion.

### The honest conclusion: PGO has no payoff on microbenchmarks

I ran the full three-phase PGO rigorously on ch04's `pgo_demo` (a small function with a 99/1 branch split) and compared it to a pure -O2 baseline:

```text
Pure -O2 baseline: 3.57-3.82 ms
-O2 + PGO:         3.78-4.17 ms   ← no payoff at all (even slightly slower, within noise)
```

**PGO has no effect on microbenchmarks.** The reason is that this small function has only two branches and a few lines; `-O2` already optimizes it well. PGO's value is in **code layout on large codebases** (physically clustering hot paths that are scattered across thousands of functions, to help icache); for a few dozen lines there's nothing to lay out.

> The first time I ran it, the "PGO version" looked 4× faster, and I got excited for a moment, until I realized that 4× was **the counter overhead of the instrumented binary** (phase 1's build comes with performance counters built in), not a PGO win. **To measure PGO you must compare against a "pure -O2, non-instrumented" baseline**, and confirm that the phase-3 profile was actually applied (a compiler warning `profile count data file not found` means it wasn't found). I'm recording this face-plant here to echo the ch01 discipline: **what you thought was a PGO win might be instrumentation overhead**.

### Where PGO actually pays off: large codebases

Every public PGO win comes from big projects: **Chrome, Firefox, major databases**, reporting **single-digit to low-teens percent** speedups, on the precondition that the codebase is large enough for icache/branch layout to actually be a bottleneck. Three rules:

- **Don't expect PGO to work on microbenchmarks or small projects.**
- **Enable it on large release builds**, sampling the profile from a **representative production workload** (not just any random run).
- Rollout (CMake): compile with `-fprofile-generate` → run the workload → recompile with `-fprofile-use`. CI integration needs to store and reuse the profile across builds.

PGO + LTO stacked is the standard combo for large-project releases.

## References

- GCC docs for `-flto` / `-fprofile-generate` / `-fprofile-use`
- LLVM docs, *ThinLTO* (llvm.org/docs/ThinLTO.html)
- ch04-07 Front-end optimization: code layout, PGO, and BOLT (this volume; first discussion of PGO mechanics and "no payoff on microbenchmarks")
- Code for this article's measurements: `code/volumn_codes/vol6-performance/ch07/lto_main.cpp` + `lto_helper.cpp`; PGO reuses `ch04/pgo_demo.cpp` + `ch04/pgo.sh`

The one-line capstone: **LTO cross-TU inlining measures 3.9×** (cross-file helper call), enable on release and use ThinLTO on large projects, with link time as the only cost; **PGO lays out by profile and has no payoff on microbenchmarks (honest null)**, with the value on large codebases (Chrome/Firefox-class, single-digit to low-teens percent), and that one-time 4× was instrumentation overhead, not PGO. **PGO + LTO** is the standard combo for large-project releases; rollout is engineering work (CMake flag-passing, profile storage), a one-time config for long-term benefit.
