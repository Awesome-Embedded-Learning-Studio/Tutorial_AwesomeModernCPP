---
title: constexpr and Compile-Time Computation
description: Make computations happen at compile time, achieving true zero-overhead
  abstraction.
translation:
  source: documents/vol2-modern-features/ch02-constexpr/index.md
  source_hash: 6fdfdc08d6f4b9cec73082dd3191024bff9f22b9b69223cffdcb8df761d76ab4
  translated_at: '2026-05-26T11:24:26.377491+00:00'
  engine: anthropic
  token_count: 163
---
# constexpr and Compile-Time Computation

If a computation's result can be determined at compile time, why wait until runtime? `constexpr` makes it possible to evaluate functions and variables at compile time, while `consteval` and `constinit` provide further hard guarantees for compile-time evaluation. In this chapter, we start with the basics of `constexpr`, understand the design constraints of literal types and `constexpr` constructors, master the new tools in C++20, and finally apply compile-time lookup tables, string processing, and design patterns in practice.

## Chapter Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-constexpr-basics">constexpr Basics: The Art of Compile-Time Evaluation</ChapterLink>
  <ChapterLink href="02-constexpr-ctor">constexpr Constructors and Literal Types</ChapterLink>
  <ChapterLink href="03-consteval-constinit">consteval and constinit: New Tools for Compile-Time Guarantees</ChapterLink>
  <ChapterLink href="04-compile-time-practice">Compile-Time Computation in Practice: From Lookup Tables to Compile-Time Strings</ChapterLink>
</ChapterNav>
