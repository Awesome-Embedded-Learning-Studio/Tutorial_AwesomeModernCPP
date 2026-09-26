---
chapter: 7
cpp_standard:
- 17
description: 'Binary size isn''t performance in itself, but it indirectly affects performance through icache/iTLB/download size (especially on embedded). This article covers the three standard size-optimization moves: -Os/-Oz (size-first optimization levels), -ffunction-sections + --gc-sections (reclaiming dead code at link time, with measured savings), and template bloat control (extern template, extracting common logic). It closes with a size <-> performance trade-off checklist'
difficulty: advanced
order: 4
platform: host
prerequisites:
- '-O levels and optimization blockers: what the compiler can and can''t do'
- 'Linking performance, multi-compiler comparison, and compile-time metaprogramming'
reading_time_minutes: 6
related:
- 'LTO, ThinLTO, and PGO: engineering them into your build'
- 'Frontend optimization: code layout, PGO, and BOLT'
tags:
- host
- cpp-modern
- advanced
- 优化
- 链接器
- 工具链
title: 'Binary size optimization: -Os, --gc-sections, and template bloat control'
translation:
  source: documents/vol6-performance/ch07-compiler-and-size/07-04-size-optimization.md
  source_hash: ecb1cd0e9ecac0f7209203d546dffaaf1f248a87def29bc0135e8d8c53791870
  translated_at: '2026-09-26T07:07:15+00:00'
  engine: anthropic
  token_count: 3600
---
# Binary size optimization: -Os, --gc-sections, and template bloat control

## Why binary size affects performance

Binary size is not "speed" in itself, but it **indirectly affects performance** through three main channels:

1. **Limited icache capacity**: bigger code → doesn't fit in the icache → icache misses → Frontend Bound (as covered in ch04-07). This is the main mechanism by which size affects performance.
2. **Limited iTLB**: more code pages → more iTLB entries needed → iTLB misses.
3. **Download/storage**: constrained embedded flash, mobile APK size, network transfer — here size is directly a cost.

So size optimization genuinely matters for **embedded (flash-constrained)**, **mobile (APK size)**, and **large codebases (icache pressure)**. This article walks through the three standard moves.

## First move: -Os / -Oz (size-first optimization levels)

`-Os` means "optimize to a size that doesn't bloat", and `-Oz` (Clang; GCC has it too) pushes size even harder. The difference from `-O2`/`-O3` is the **cost model**:

- `-O2`: the cost model is "speed first, size second".
- `-Os`: the cost model is "size first, but don't get noticeably slower"; it skips the optimizations that grow the code (such as aggressive loop unrolling).
- `-Oz`: leans even further toward size, possibly sacrificing a little speed.

Measured on `size_demo` (which contains dead code + multiple template instantiations):

> ⚠️ Measure code size with the text segment from the `size` command, not with `ls -l`. A whole ELF includes headers/alignment/debug info and gets polluted — and on some compiler versions the `-Os` ELF can even come out bigger than `-O2`. Throughout this article we measure with the text segment (the code segment) reported by `size <binary>`.

```text
            text     data     bss     (local GCC 16, order of magnitude)
-O2:        ~4144    ...      ...     ← baseline
-Os:        ~3740    ...      ...     ← smaller text than -O2
-Oz:        ~3740    ...      ...     ← same ballpark as -Os
--gc-sections ~4017 ...      ...     ← after dead-code reclamation
```

Absolute numbers shift with the compiler version, but the direction is stable: the text of `-Os`/`-Oz` is smaller than `-O2`.

On this small demo the difference is tiny (a few hundred bytes) because the program itself is small. **On large projects, `-Os` typically saves 5-15% size over `-O2`**. The cost of `-Os` is "possibly slightly slower" (it skips the bloat-causing optimizations), so it fits scenarios where "size is a hard constraint" (embedded flash), not speed-first desktop/server workloads.

## Second move: -ffunction-sections + --gc-sections (reclaiming dead code)

The idea of this move is to place every function/data object in its own section and reclaim the **unreferenced sections** at link time. Two steps:

```bash
# Compile: each function in its own section
g++ -ffunction-sections -fdata-sections ...
# Link: reclaim unreferenced sections
g++ ... -Wl,--gc-sections
```

What it solves is **dead code**: functions that are defined but never called (very common: legacy code, old paths disabled by conditional compilation, template members that got instantiated but are never used). In the measurement above, `--gc-sections` saved 200 bytes over `-O2` (that demo contains two `[[maybe_unused]]` dead functions).

On large projects `--gc-sections` pays off significantly: big C++ projects routinely carry loads of "linked in but never used" code (especially when whole third-party libraries are linked in), and `--gc-sections` can cut away tens of percent. The cost is essentially nil (compile/link get slightly slower, negligible). **Release builds should turn on `-ffunction-sections -fdata-sections -Wl,--gc-sections` by default** — a nearly free size optimization.

> Note: what `--gc-sections` reclaims are sections whose entire function/data object is unreferenced; partial code inside a function (say, an `if` branch that never executes) is beyond its reach — that is a job for PGO's code layout. The two are complementary.

## Third move: template bloat control

A template instantiates a separate copy of the code for each type, so it bloats easily. `vector<int>`, `vector<double>`, and `vector<MyType>` are three independent copies of `push_back`/`reserve`/growth code. A few control techniques:

- **`extern template` (C++11)**: explicitly declares "this template instantiation is instantiated in another TU; don't generate it again here". In a large project, instantiate the frequently used template instances once, centralized in a single `.cpp`, and have the other TUs declare them with `extern template` — avoiding every TU generating its own copy that then has to be deduplicated at link time (deduplication costs time too).

  ```cpp
  // common.h
  extern template class std::vector<int>;   // declaration: don't generate it here
  // common.cpp
  template class std::vector<int>;           // instantiate once
  ```

- **Extract common logic**: pull the type-independent parts of a template out into a non-template base class / common function, compiled only once. For example, `vector<T>`'s memory management can be shared through a non-template `vector_base`.
- **Don't over-generalize**: instantiate only for the types you truly need. If a `template<class T>` is applied to a function that only ever serves `int`/`double`, instantiate just those two — don't add a pile of unused specializations for the sake of "generality".

On large projects (especially heavy users of STL/Boost), template bloat can account for a considerable share of size. These three are the standard countermeasures.

## The size ↔ performance trade-off checklist

Putting the three moves together with what came before, here is a checklist ordered by "size optimization vs performance impact":

| Technique | Size | Speed | When to use |
|---|---|---|---|
| `-ffunction-sections` + `--gc-sections` | ↓↓ | almost unchanged | **on by default in release** (free) |
| `-Os`/`-Oz` | ↓ | may drop slightly | hard size constraint (embedded) |
| `extern template` | ↓ | unchanged | template-heavy large projects |
| Extract common logic (non-template base) | ↓ | may rise slightly (indirect calls) | weigh carefully |
| `-O3` (aggressive vectorization/unrolling) | ↑↑ | usually ↑, occasionally ↓ | speed first, size budget is enough |
| Template over-generalization | ↑↑ | — | don't write it this way |

The core trade-off is that **size optimization and speed optimization often pull in opposite directions**: `-Os` saves size but may be slower; `-O3` speeds things up but bloats. **Embedded goes size-first, desktop/server goes speed-first, mobile sits in between**. Start with `--gc-sections` (the free size dividend), then pick the `-O` level per scenario, and only last consider the moves that require code changes, such as `extern template`.

## References

- The existing vol6 `06-evaluating-performance-and-size.md` (this article is the predecessor of its expanded version; it already exists)
- Agner Fog, *Optimizing assembly*, §10 *Code size optimization*. Local copy
- GCC/Clang documentation for `-Os`/`-Oz`/`-ffunction-sections`/`-Wl,--gc-sections`/`extern template`
- CSAPP chapter 7, *Linking* (background on the linking mechanics behind `--gc-sections`)
- This article's measurement code: `code/volumn_codes/vol6-performance/ch07/size_demo.cpp`

One-sentence wrap-up: **the main mechanism by which size affects performance is icache/iTLB misses** (plus embedded flash and mobile downloads); the three moves are `-Os`/`-Oz` (size-first optimization levels, measured to save a few hundred bytes up to 5-15% over -O2), `--gc-sections` (dead-code reclamation, nearly free, on by default in release), and template bloat control (`extern template`, extracting common logic); size ↔ speed is often a reverse trade-off — embedded goes size-first, desktop goes speed-first — and `--gc-sections` is the free size dividend to collect first.

This is the last article of ch07 and the close of vol6's eight-chapter tour. The volume started from "performance mindset + the sanitizer foundation", passed through "measurement methodology", "CPU microarchitecture", "attribution methodology", "tuning by bottleneck", "multicore", and "the cost of C++ abstractions", and arrived here at "compiler boundaries and size" — a complete performance-engineering methodology running from "correct first, measure first" to "treat the right symptom".
