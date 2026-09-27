---
title: "Operator Overloading"
description: "Making custom types support arithmetic, comparison, stream, subscript, and other operators"
translation:
  source: documents/vol1-fundamentals/ch07/index.md
  source_hash: f3f3e8ae80e6f520f001c9934825589b8a23abc2898cf0f5e1bc46d25503d674
  translated_at: '2026-09-25T11:20:53+00:00'
  engine: anthropic
  token_count: 200
---
# Operator Overloading

Operator overloading lets custom types take part in operations as naturally as built-in types do—we can add two `Vec3` objects directly, give a custom matrix type `[]` indexing, or even make objects callable like functions. In this chapter we walk through all the commonly used operator overloads, figure out which operators can be overloaded and which cannot, and learn how to do it without digging ourselves into a pile of implicit-conversion pitfalls.

## Chapter Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-arithmetic-comparison">Arithmetic and Comparison Operators</ChapterLink>
  <ChapterLink href="02-io-subscript">Stream and Subscript Operators</ChapterLink>
  <ChapterLink href="03-call-and-conversion">Function Calls and Type Conversion</ChapterLink>
</ChapterNav>
