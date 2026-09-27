---
title: "Compiler optimization boundaries and binary size"
description: "ch07 closes the volume from the compiler's perspective: -O levels and optimization blockers (cross-TU / aliasing / volatile), LTO cross-TU inlining (measured 3.9×) and PGO (no gain on microbenchmarks, real on large codebases), linking performance, multi-compiler comparison, and compile-time metaprogramming, plus size optimization (-Os / --gc-sections / template bloat). The volume's engineering capstone"
translation:
  source: documents/vol6-performance/ch07-compiler-and-size/index.md
  source_hash: 59f19b53cbce930d4e34516e4e2057b2b307663b9281d61a390d9e0fdbe87629
  translated_at: '2026-09-26T07:15:10+00:00'
  engine: anthropic
  token_count: 400
---

# Compiler optimization boundaries and binary size

This is vol6's last chapter, closing things out from the **compiler and linker** perspective. The earlier chapters covered how to write "C++ code that runs fast on the hardware"; this one covers **what the compiler can do for you and what it can't**, and how to work with it (give it line of sight, stay out of its way).

Four articles:

- **07-01 -O levels and blockers**: `-O2` is the release sweet spot (measured: -O0→-O2 is 4× faster), **-O3 is occasionally slower than -O2** (honest); three kinds of blockers (cross-TU / aliasing / volatile).
- **07-02 LTO and PGO**: LTO cross-TU inlining measured at **3.9×**; PGO shows no gain on microbenchmarks (an honest null — that one-time 4× was instrumentation overhead), its value is on large codebases.
- **07-03 Linking, multi-compiler, metaprogramming**: the PIC cost of dynamic linking (CSAPP ch7), the small gap between GCC/Clang/MSVC, and the size side of compile-time metaprogramming.
- **07-04 Size optimization**: `-Os` / `--gc-sections` / template bloat control; size ↔ speed is often a reverse tradeoff.

The spirit running through this chapter matches ch04: **the compiler is your performance teammate, and your job is to "stay out of its way"** — let it see the implementations (LTO), trust your no-aliasing promise (`__restrict`), and don't use `volatile` to disable its optimizations. Turning on LTO + `--gc-sections` by default in release is a free lunch; PGO is the extra course on large projects.

> Local measurements cover 07-01/02/04; 07-03 is mostly conceptual (linking mechanism follows CSAPP ch7, multi-compiler comparison follows Agner vol. 1 §8), so we don't reinvent those experiments.

## In this chapter

<ChapterNav variant="sub">
  <ChapterLink href="07-01-opt-levels-and-blockers">-O levels and optimization blockers</ChapterLink>
  <ChapterLink href="07-02-lto-pgo">LTO, ThinLTO, and PGO: engineering them into your build</ChapterLink>
  <ChapterLink href="07-03-linking-and-compilers">Linking performance, multi-compiler comparison, and compile-time metaprogramming</ChapterLink>
  <ChapterLink href="07-04-size-optimization">Binary size optimization: -Os, --gc-sections, and template bloat control</ChapterLink>
</ChapterNav>
