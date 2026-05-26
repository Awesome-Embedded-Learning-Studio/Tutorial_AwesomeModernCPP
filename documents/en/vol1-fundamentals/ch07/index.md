---
title: Operator Overloading
description: Making custom types support arithmetic, comparison, stream, and subscript
  operators
translation:
  source: documents/vol1-fundamentals/ch07/index.md
  source_hash: 31ed9708468487bc63d4e4d8923c64467fe157bbd6f53adbff143c65b10aa72c
  translated_at: '2026-05-26T10:52:38.117693+00:00'
  engine: anthropic
  token_count: 113
---
# Operator Overloading

Operator overloading lets custom types participate in operations as naturally as built-in types—we can add two `Vec3` directly, support `[]` indexing for custom matrix types, or even make objects callable like functions. In this chapter, we walk through all commonly used operator overloads, clarifying which ones can be overloaded, which cannot, and how to avoid the pitfalls of unintended implicit conversions.

## Chapter Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-arithmetic-comparison">Arithmetic and Comparison Operators</ChapterLink>
  <ChapterLink href="02-io-subscript">Stream and Subscript Operators</ChapterLink>
  <ChapterLink href="03-call-and-conversion">Function Call and Type Conversion</ChapterLink>
</ChapterNav>
