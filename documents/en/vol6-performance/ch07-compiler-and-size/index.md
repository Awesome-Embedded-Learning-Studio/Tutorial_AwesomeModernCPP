---
title: "Compiler optimization boundaries and size evaluation"
description: "ch07 closes the volume from the compiler's perspective: -O levels and optimization blockers (cross-TU / aliasing / volatile), LTO cross-TU inlining (measured 3.9×) and PGO (null on microbenchmarks, real on large codebases), linking performance and multi-compiler comparison and compile-time metaprogramming, and size optimization (-Os / --gc-sections / template bloat). The volume's engineering capstone"
---

# Compiler optimization boundaries and size evaluation

This is vol6's last chapter, closing things out from the **compiler and linker** side. The earlier chapters were about how to write "C++ code that the hardware runs fast"; this one is about **what the compiler can do for you, what it can't, and how to cooperate with it** (give it visibility, don't get in its way).

Four articles:

- **07-01 -O levels and blockers**: `-O2` is the release sweet spot (measured -O0→-O2 4× faster), **-O3 is occasionally slower than -O2** (honestly); three blockers (cross-TU / aliasing / volatile).
- **07-02 LTO and PGO**: LTO cross-TU inlining measured **3.9×**; PGO has no payoff on microbenchmarks (honest null, that one-time 4× was instrumentation overhead), value is on large codebases.
- **07-03 Linking, multi-compiler, metaprogramming**: dynamic-link PIC cost (CSAPP ch7), GCC/Clang/MSVC gap is small, the size face of compile-time metaprogramming.
- **07-04 Size optimization**: `-Os` / `--gc-sections` / template bloat control; the size ↔ speed tradeoff is often opposing.

The thread running through this chapter is the same as ch04: **the compiler is your performance teammate, your job is to get out of its way**; let it see implementations (LTO), trust your no-alias promise (`__restrict`), and don't use `volatile` to disable its optimizations. release defaults to LTO + `--gc-sections`, that's free lunch; PGO is the extra course on large projects.

> Local measurement covers 07-01/02/04; 07-03 is conceptual (linking mechanism from CSAPP ch7, multi-compiler from Agner vol1 §8), no reinvented experiments.

## In this chapter

<ChapterNav variant="sub">
  <ChapterLink href="07-01-opt-levels-and-blockers">-O levels and optimization blockers</ChapterLink>
  <ChapterLink href="07-02-lto-pgo">LTO, ThinLTO, and the engineering rollout of PGO</ChapterLink>
  <ChapterLink href="07-03-linking-and-compilers">Linking performance, multi-compiler comparison, and compile-time metaprogramming</ChapterLink>
  <ChapterLink href="07-04-size-optimization">Size optimization: -Os, --gc-sections, and template bloat control</ChapterLink>
</ChapterNav>
