---
title: "Tuning by bottleneck site"
description: "ch04 is the technical body of vol6, aligned to the four TMAM buckets: Backend Memory (cache-friendly, AoS/SoA, prefetch), Backend Core (loop optimization, data types and arithmetic, inline plus devirtualization, SIMD), Bad Speculation (branchless), Frontend (code layout, PGO, BOLT), each article backed by on-machine measurement"
---

# Tuning by bottleneck site

ch03 teaches us to use USE / Roofline / TMAM / flame graphs to attribute the bottleneck to a pipeline bucket. This chapter is the prescription, walking each bucket through how to treat it. It is the technical body of vol6 and the longest chapter in the volume.

The four buckets map to seven articles:

- **Backend Memory** (04-01): cache-friendly, AoS to SoA, prefetch. The biggest single-threaded lever, with AoS to SoA measuring close to a 10x speedup.
- **Backend Core** (04-02 / 04-03 / 04-04 / 04-05): loop optimization, data types and arithmetic, inline plus devirtualization, SIMD. Division is a 5x bottleneck over multiplication, SIMD measures around 20x, virtual calls vs CRTP are 2.5x.
- **Bad Speculation** (04-06): branchless and predication, with the core message "don't go branchless blindly."
- **Frontend** (04-07): code layout, PGO, BOLT.

The single sentence that runs through the whole chapter is this: **modern compilers plus modern hardware already do most of the optimization for you, so your job is not "pile on handwritten tricks," it's "measure the real bottleneck, change precisely, then verify with the same methodology."** Several honest results live in this chapter: switch isn't always faster than if-else, `final` doesn't automatically devirtualize, FP reduction isn't auto-vectorized the way the old textbooks describe, branchless at -O2 is the same speed as the branch version, PGO has no benefit on microbenchmarks. They are all footnotes to that sentence. Performance optimization is measurement-driven precision surgery, not a pile of tricks.

> Boundary note: this chapter only covers "**P** — when running on hardware, how to make it faster." "**D** — why vector/string are designed this way" and "**U** — how to use containers correctly" belong to vol3; "EBO/SSO mechanisms" belong to vol4; "how to write lock-free code and synchronization primitives" belong to vol5. ch04-01 was edge-checked against vol3 before writing: it covers layout for performance and does not redo mechanism explanations.

## In this chapter

<ChapterNav variant="sub">
  <ChapterLink href="04-01-backend-memory">Backend memory bottlenecks: cache-friendly, AoS/SoA, and prefetch</ChapterLink>
  <ChapterLink href="04-02-loop-and-compute">Loop and compute optimization: code motion, eliminating memory references, and multiple accumulators</ChapterLink>
  <ChapterLink href="04-03-types-and-arithmetic">Data types and arithmetic: int vs float, the division bottleneck, and jump tables</ChapterLink>
  <ChapterLink href="04-04-inline-devirt-compiler">inline, devirtualization, and the compiler optimization landscape</ChapterLink>
  <ChapterLink href="04-05-simd">SIMD and vectorization: auto-vectorization conditions, intrinsics, and CPU dispatch</ChapterLink>
  <ChapterLink href="04-06-branch-branchless">Branches: branchless, predication, and "don't go branchless blindly"</ChapterLink>
  <ChapterLink href="04-07-frontend-pgo">Frontend optimization: code layout, PGO, and BOLT</ChapterLink>
</ChapterNav>
