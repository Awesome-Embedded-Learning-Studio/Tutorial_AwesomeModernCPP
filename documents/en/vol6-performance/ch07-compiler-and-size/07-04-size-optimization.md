---
chapter: 7
cpp_standard:
- 17
description: 'Binary size isn''t itself performance, but it indirectly affects performance through icache/iTLB/download
  size (especially on embedded). This article covers three size-optimization techniques: -Os/-Oz (size-first optimization
  levels), -ffunction-sections + --gc-sections (link-time dead-code reclamation, measured), and template bloat control
  (extern template, factoring out shared logic), and ends with a "size ↔ performance" tradeoff checklist'
difficulty: advanced
order: 4
platform: host
prerequisites:
- '-O levels and optimization blockers'
- Linking performance, multi-compiler comparison, and compile-time metaprogramming
reading_time_minutes: 6
related:
- LTO, ThinLTO, and the engineering rollout of PGO
- 'Front-end optimization: code layout, PGO, and BOLT'
tags:
- host
- cpp-modern
- advanced
- 优化
- 链接器
- 工具链
title: 'Size optimization: -Os, --gc-sections, and template bloat control'
translation:
  source: documents/vol6-performance/ch07-compiler-and-size/07-04-size-optimization.md
  source_hash: ecb1cd0e9ecac0f7209203d546dffaaf1f248a87def29bc0135e8d8c53791870
  translated_at: '2026-07-06T00:00:00+00:00'
  engine: manual
  token_count: 2900
---
# Size optimization: -Os, --gc-sections, and template bloat control

## Why size affects performance

Binary size itself isn't "speed", but it **indirectly affects performance** along three main paths:

1. **icache is finite**: more code → won't fit in icache → icache misses → Frontend Bound (covered in ch04-07). This is the main mechanism by which size hits performance.
2. **iTLB is finite**: more code pages → more iTLB entries needed → iTLB misses.
3. **Download/storage**: embedded flash constraints, mobile APK size, network transfer; size is directly cost.

So size optimization matters for **embedded (flash-constrained)**, **mobile (APK size)**, and **large codebases (icache pressure)**. This article covers three standard techniques.

## Technique 1: -Os / -Oz (size-first optimization levels)

`-Os` is "optimize down to a size that doesn't bloat", and `-Oz` (Clang, also GCC) prioritizes size even more aggressively. Their difference from `-O2`/`-O3` is a **different cost model**:

- `-O2`: cost model is "speed first, size second".
- `-Os`: cost model is "size first, but don't get noticeably slower"; it skips optimizations that would grow the code (like aggressive loop unrolling).
- `-Oz`: even more size-biased, possibly sacrificing a bit of speed.

Measurement (`size_demo`, with dead code + multiple template instances):

> ⚠️ Measure code size with the `size` command's text segment, not `ls -l`. A whole ELF includes headers/alignment/debug info and gets polluted, and on some compiler versions the `-Os` ELF can even come out larger than `-O2`. Below we consistently use `size <binary>`'s text segment (the code segment).

```text
            text     data     bss     (order of magnitude, local GCC 16)
-O2:        ~4144    ...      ...     ← baseline
-Os:        ~3740    ...      ...     ← smaller text than -O2
-Oz:        ~3740    ...      ...     ← same order as -Os
--gc-sections ~4017 ...      ...     ← after dead-code reclamation
```

Absolute values shift with compiler version, but the direction is stable: `-Os`/`-Oz` text is smaller than `-O2`.

On this small demo the difference is tiny (a few hundred bytes), because the program itself is small. **On large projects, `-Os` typically saves 5-15% over `-O2`.** The cost of `-Os` is "might be slightly slower" (it skips the bloat-inducing optimizations), so it fits scenarios where "size is a hard constraint" (embedded flash), not "speed-first" desktop/server.

## Technique 2: -ffunction-sections + --gc-sections (dead-code reclamation)

The idea here is to put each function/data item in its own section, and reclaim **unreferenced sections** at link time. Two steps:

```bash
# Compile: each function in its own section
g++ -ffunction-sections -fdata-sections ...
# Link: reclaim unreferenced sections
g++ ... -Wl,--gc-sections
```

What it solves is **dead code**: functions that are defined but never called (very common: legacy code, old paths disabled by conditional compilation, template members instantiated but never used). Measurement above shows `--gc-sections` saving 200 bytes over `-O2` (that demo has two `[[maybe_unused]]` dead functions).

On large projects `--gc-sections` pays off significantly: large C++ projects often have a lot of "linked in but never used" code (especially when whole third-party libraries are linked in), and `--gc-sections` can cut tens of percent. The cost is essentially nil (compile/link gets slightly slower, negligible). **Release builds should enable `-ffunction-sections -fdata-sections -Wl,--gc-sections` by default**, near-free size optimization.

> Note: `--gc-sections` reclaims sections whose "entire function/data is unreferenced"; it can't reclaim partial code inside a function (say, an if branch that never executes). That's PGO's job, via code layout. The two are complementary.

## Technique 3: template bloat control

Templates instantiate one copy of the code per type, so they bloat easily. `vector<int>`, `vector<double>`, `vector<MyType>` are three independent copies of `push_back`/`reserve`/growth code. A few control techniques:

- **`extern template` (C++11)**: explicitly declare that "this template instantiation lives in another TU; don't regenerate it here". In large projects, centralize the commonly-used template instantiations in one `.cpp` (instantiate once), and have other TUs declare them with `extern template`, so each TU doesn't generate its own copy and dedupe at link time (deduping takes time too).

  ```cpp
  // common.h
  extern template class std::vector<int>;   // declaration: don't generate here
  // common.cpp
  template class std::vector<int>;           // instantiate once
  ```

- **Factor out shared logic**: pull out the type-independent parts of a template into a non-template base class or shared function, compiled once. For example, `vector<T>`'s memory management can share a non-template `vector_base`.
- **Don't over-generalize**: only instantiate for the types you actually need. If `template<class T>` is used on a function that only ever takes `int`/`double`, instantiate only those two; don't pile on unused specializations for "generality".

Template bloat can contribute a meaningful chunk of size on large projects (especially heavy STL/Boost users). These three are the standard counters.

## The size ↔ performance tradeoff checklist

Putting the three techniques together with the earlier ones, here's a checklist sorted by "size optimization vs performance impact":

| Technique | Size | Speed | When to use |
|---|---|---|---|
| `-ffunction-sections` + `--gc-sections` | ↓↓ | Almost unchanged | **release default** (free)|
| `-Os`/`-Oz` | ↓ | Possibly slightly down | Size is a hard constraint (embedded)|
| `extern template` | ↓ | Unchanged | Heavy-template large projects |
| Factor out shared logic (non-template base) | ↓ | Possibly slightly up (indirect call)| Weigh carefully |
| `-O3` (aggressive vectorization/unrolling) | ↑↑ | Usually ↑ occasionally ↓ | Speed first, size is plentiful |
| Template over-generalization | ↑↑ | n/a | Don't write this way |

The core tradeoff is that **size optimization and speed optimization often pull in opposite directions**: `-Os` saves size but might be slower; `-O3` speeds things up but bloats. **Embedded prioritizes size, desktop/server prioritizes speed, mobile is in between**. Take `--gc-sections` first (the free size win), then choose the `-O` level by scenario, and only then consider things like `extern template` that require code changes.

## References

- Existing vol6 `06-evaluating-performance-and-size.md` (this article's predecessor; already present)
- Agner Fog, *Optimizing Assembly*, §10 *Code size optimization*. Local copy
- GCC/Clang docs for `-Os`/`-Oz`/`-ffunction-sections`/`-Wl,--gc-sections`/`extern template`
- CSAPP Chapter 7, *Linking* (background on the `--gc-sections` link mechanism)
- Code for this article's measurements: `code/volumn_codes/vol6-performance/ch07/size_demo.cpp`

The one-line capstone: **the main mechanism by which size hits performance is icache/iTLB misses** (plus embedded flash and mobile downloads); the three techniques are `-Os`/`-Oz` (size-first optimization levels, measured saving a few hundred bytes to 5-15% over -O2), `--gc-sections` (dead-code reclamation, almost free, on by default in release), and template bloat control (`extern template`, factoring out shared logic); size ↔ speed often trade off in opposite directions: embedded prioritizes size, desktop prioritizes speed, and `--gc-sections` is the free size win to take first.

This is the last article of ch07, and the capstone of vol6's eight-chapter encyclopedia. The volume started from "performance mindset + sanitizer foundation", passed through "measurement methodology", "CPU microarchitecture", "attribution methodology", "per-bottleneck optimization", "multi-core", "C++ abstraction cost", and arrives here at "compiler boundaries and size", a complete performance-engineering methodology from "correct first, measure first" to "the right medicine for the right symptom".
