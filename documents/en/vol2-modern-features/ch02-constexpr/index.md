---
title: "constexpr and Compile-Time Computation"
description: "Move computation to compile time for true zero-overhead abstraction"
translation:
  source: documents/vol2-modern-features/ch02-constexpr/index.md
  source_hash: fa6ed52411dac379ea3c285356b26e92476e20cc64bac848b4ec5049d9626728
  translated_at: '2026-09-27T10:17:18+00:00'
  engine: anthropic
  token_count: 450
---
# constexpr and Compile-Time Computation

If a computation's result can already be determined at compile time, why wait until runtime to do it? constexpr makes it possible to evaluate functions and variables during compilation, and consteval and constinit go further, providing hard compile-time guarantees. In this chapter we start from the basics of constexpr, work through the design constraints of literal types and constexpr constructors, pick up the new C++20 tools, and finally put everything to work in practice with compile-time lookup tables, string processing, state machines, and design patterns, before landing it all in embedded scenarios.

## Chapter Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-constexpr-basics">constexpr Basics: The Art of Compile-Time Evaluation</ChapterLink>
  <ChapterLink href="02-constexpr-ctor">constexpr Constructors and Literal Types</ChapterLink>
  <ChapterLink href="03-consteval-constinit">consteval and constinit: New Tools for Compile-Time Guarantees</ChapterLink>
  <ChapterLink href="04-compile-time-practice">Compile-Time Computation in Practice: From Lookup Tables to Compile-Time Strings</ChapterLink>
</ChapterNav>
