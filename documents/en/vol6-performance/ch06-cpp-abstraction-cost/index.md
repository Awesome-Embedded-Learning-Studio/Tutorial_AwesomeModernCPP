---
title: "The performance cost of C++ abstractions"
description: "ch06 is the performance mirror of vol3's 'why components are designed this way' — what happens on the hardware after you use them. We measure each one: virtual functions (devirtualization often makes them free), exceptions (the zero-cost model, free on the normal path / throwing costs 3400x), std::function (SBO and heap allocation), a cost cheat sheet (sizeof plus three supplements), and RVO/NRVO and move (return by value at zero cost). The thesis: there are no zero-cost abstractions, but measure first, then optimize"
translation:
  source: documents/vol6-performance/ch06-cpp-abstraction-cost/index.md
  source_hash: bbca2529e610707ef686d890803dc0ada55706860c4c50710c535250fca65aa4
  translated_at: '2026-09-26T07:05:14+00:00'
  engine: anthropic
  token_count: 1400
---

# The performance cost of C++ abstractions

This chapter is vol3's **performance mirror**. vol3 covers "**why components like vector/string/function are designed the way they are**" (design motivation); this vol6 chapter covers "**what happens on the hardware after you use them that makes things fast or slow**". The thesis is Carruth's *There Are No Zero-Cost Abstractions*: **there are no zero-cost abstractions** — every C++ abstraction maps to a hardware cost.

But a contrarian spirit runs through the whole chapter: "has a cost" **does not mean** "paid every time". The compiler often eliminates the cost for you — devirtualization turns a virtual call into a direct call, the zero-cost model keeps the normal path of exceptions free, and RVO makes return-by-value zero-copy. So the advice this chapter repeats over and over is "**measure first; don't hand-write around it in advance**".

Five articles:

- **06-01 Virtual functions and devirtualization**: a virtual call through a pointer costs 0.55ns (2.5x CRTP), but the compiler often devirtualizes. Don't CRTP-ify ahead of time.
- **06-02 The zero-cost model of exceptions**: the normal path costs 0.25ns (zero cost nailed down), throwing costs 857ns (3400x). Use exceptions only for truly exceptional cases.
- **06-03 std::function's SBO**: calls are 6x slower; construction goes through SBO when small and heap-allocates when large (8.5x). Watch out for repeated construction plus a large capture on hot paths.
- **06-04 The cost cheat sheet**: a roll-up plus variable storage types, bit-fields, enum class being zero-cost, and sizeof.
- **06-05 RVO, NRVO, and move**: return by value is zero-copy and zero-move (verified with the copy/move counter method); `return std::move(local)` is an anti-pattern.

> Boundary: the **design mechanisms** of components (vector's three pointers, the SSO implementation, EBO) belong to vol3/vol4; vol6 only covers "the cost of running them on the hardware". High-frequency Zhihu questions (are virtual functions slow / are exceptions slow / function heap allocation / the move counter-example / return std::move) are folded into each article's entry point, none gets a standalone article.

## In this chapter

<ChapterNav variant="sub">
  <ChapterLink href="06-01-virtual-devirtualization">Virtual functions and devirtualization: don't rush to rewrite virtuals as templates</ChapterLink>
  <ChapterLink href="06-02-exceptions-zero-cost">The zero-cost model of exceptions: free on the normal path, expensive on the exception path</ChapterLink>
  <ChapterLink href="06-03-std-function-sbo">std::function's small buffer optimization: the cost of type erasure</ChapterLink>
  <ChapterLink href="06-04-abstraction-cost-cheatsheet">C++ abstraction costs: a cheat sheet</ChapterLink>
  <ChapterLink href="06-05-rvo-move">The real cost of RVO, NRVO, and move</ChapterLink>
</ChapterNav>
