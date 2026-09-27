---
title: "constexpr and Compile-Time Computation"
description: "Move computation to compile time for true zero-overhead abstraction"
translation:
  source: documents/vol2-modern-features/ch02-constexpr/index.md
  source_hash: 6fdfdc08d6f4b9cec73082dd3191024bff9f22b9b69223cffdcb8df761d76ab4
  translated_at: '2026-09-25T14:54:54+00:00'
  engine: anthropic
  token_count: 200
---
# constexpr and Compile-Time Computation

If a computation's result is already known at compile time, why wait until runtime to produce it? constexpr makes it possible to evaluate functions and variables during compilation, while consteval and constinit go further and give us hard compile-time guarantees. In this chapter we start from the basics of constexpr, work through the design constraints of literal types and constexpr constructors, pick up the new C++20 tools, and finally put everything to work in practice with compile-time lookup tables, string processing, and design patterns.

## Chapter Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-constexpr-basics">constexpr Basics: The Art of Compile-Time Evaluation</ChapterLink>
  <ChapterLink href="02-constexpr-ctor">constexpr Constructors and Literal Types</ChapterLink>
  <ChapterLink href="03-consteval-constinit">consteval and constinit: New Tools for Compile-Time Guarantees</ChapterLink>
  <ChapterLink href="04-compile-time-practice">Compile-Time Computation in Practice: From Lookup Tables to Compile-Time Strings</ChapterLink>
</ChapterNav>
