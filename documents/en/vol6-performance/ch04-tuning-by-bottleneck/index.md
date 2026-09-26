---
title: "Tuning by bottleneck site"
description: "ch04 is the technical core of vol6, aligned with the four TMAM buckets: Backend Memory (cache-friendly, AoS/SoA, prefetch), Backend Core (loop optimization, data types and arithmetic, inline plus devirtualization, SIMD), Bad Speculation (branchless), and Frontend (code layout, PGO, BOLT) — every article backed by measurements on our own machine"
translation:
  source: documents/vol6-performance/ch04-tuning-by-bottleneck/index.md
  source_hash: 0bd269b8362b1e5d32d9d314d7d9e025e272fc5051aa3aed75514d8cc0e1ab13
  translated_at: '2026-09-26T06:30:14+00:00'
  engine: anthropic
  token_count: 1250
---

# Tuning by bottleneck site

ch03 taught us to use USE / Roofline / TMAM / flame graphs to attribute the bottleneck to a specific pipeline bucket. This chapter is the prescription: we take the four buckets one by one and walk through how to treat each. This is the **technical core** of vol6 and the longest chapter in the volume.

The four buckets map to seven articles:

- **Backend Memory** (04-01): cache-friendly, AoS→SoA, prefetch. The single biggest single-threaded lever — AoS→SoA measured nearly 10x faster.
- **Backend Core** (04-02 / 04-03 / 04-04 / 04-05): loop optimization, data types and arithmetic, inline plus devirtualization, SIMD. Division is a bottleneck at 5x the cost of multiplication, SIMD measured ~20x, virtual calls vs CRTP 2.5x.
- **Bad Speculation** (04-06): branchless and predication, with the core message "don't go branchless blindly".
- **Frontend** (04-07): code layout, PGO, BOLT.

One sentence captures the spirit running through the whole chapter: **modern compilers plus the hardware already do most of the optimization for you, so your job is not "hand-write tricks as hard as you can" but "measure the real bottleneck, fix it precisely, then verify the fix with the same methodology"**. This chapter holds several honest results: switch is not always faster than if-else, final doesn't buy automatic devirtualization, FP reductions don't auto-vectorize, branchless at -O2 runs at the same speed as the plain if, and PGO brings no gain on microbenchmarks. Each of them is a footnote to that sentence. Performance optimization is measurement-driven precision surgery, not a pile of tricks.

> Boundary note: this chapter covers only "**P** — how to change the code so it runs faster on the hardware". "**D** — why vector/string are designed the way they are" and "**U** — how to pick the right container" belong to vol3; "EBO/SSO mechanisms" belong to vol4; "how to write lock-free code and synchronization primitives" belong to vol5. ch04-01 was edge-checked against vol3 before writing: it covers layout for performance and does not repeat the mechanism story.

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
