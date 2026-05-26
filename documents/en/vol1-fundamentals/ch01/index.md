---
title: Types and Value Categories
description: Understanding the C++ type system, type conversion rules, and value category
  fundamentals
translation:
  source: documents/vol1-fundamentals/ch01/index.md
  source_hash: 3a6a86c56fa0162b68a0d726f2bd193864a9cb4f96dd0498943c339d60af7dae
  translated_at: '2026-05-26T10:42:54.398895+00:00'
  engine: anthropic
  token_count: 136
---
# Types and Value Categories

The type system is one of the most core designs in C++—it determines how data is laid out in memory, what the compiler checks for you, and what operations you can perform on a piece of data. In this chapter, we start with basic data types to understand exactly what integers, floating-point numbers, characters, and booleans are in C++. Then we discuss the potentially tricky implicit rules of type conversion, and revisit our old friend `const`. Finally, we get an initial taste of the concept of "value categories," laying the groundwork for understanding move semantics later on.

## Chapter Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-basic-types">Basic Data Types</ChapterLink>
  <ChapterLink href="02-type-conversion">Type Conversion</ChapterLink>
  <ChapterLink href="03-const-basics">Introduction to const</ChapterLink>
  <ChapterLink href="04-value-categories">Introduction to Value Categories</ChapterLink>
</ChapterNav>
