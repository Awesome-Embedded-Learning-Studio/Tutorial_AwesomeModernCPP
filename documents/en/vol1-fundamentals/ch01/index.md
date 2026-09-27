---
title: "Types and Value Categories"
description: "Understand the C++ type system, the rules of type conversion, and the fundamentals of value categories"
translation:
  source: documents/vol1-fundamentals/ch01/index.md
  source_hash: 3a6a86c56fa0162b68a0d726f2bd193864a9cb4f96dd0498943c339d60af7dae
  translated_at: '2026-09-25T09:56:45+00:00'
  engine: anthropic
  token_count: 270
---
# Types and Value Categories

The type system is one of the most fundamental pieces of C++'s design — it determines how data is laid out in memory, what the compiler checks on your behalf, and what operations you can perform on a given piece of data. In this chapter we start from the basic data types and work out what integers, floating-point numbers, characters, and booleans in C++ really are; then we talk through the trap-ridden implicit rules of type conversion and catch up with our old friend `const`. Finally, we get a first taste of the idea of "value categories," laying the groundwork for understanding move semantics later on.

## Chapter Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-basic-types">Basic Data Types</ChapterLink>
  <ChapterLink href="02-type-conversion">Type Conversion</ChapterLink>
  <ChapterLink href="03-const-basics">A First Look at const</ChapterLink>
  <ChapterLink href="04-value-categories">Introduction to Value Categories</ChapterLink>
</ChapterNav>
