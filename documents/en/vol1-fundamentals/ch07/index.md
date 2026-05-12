---
title: Operator overloading
description: Make custom types support operators such as arithmetic, comparison, stream,
  and subscript
---
# Operator Overloading

Operator overloading lets custom types participate in operations as naturally as built-in types—we can add two `Vec3` directly, support `[]` indexing for custom matrix types, or even make objects callable like functions. In this chapter, we walk through all the commonly used operator overloads, clarifying which ones can be overloaded, which cannot, and how to avoid the pitfalls of unintended implicit conversions.

## Chapter Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-arithmetic-comparison">Arithmetic and Comparison Operators</ChapterLink>
  <ChapterLink href="02-io-subscript">Stream and Subscript Operators</ChapterLink>
  <ChapterLink href="03-call-and-conversion">Function Call and Type Conversion</ChapterLink>
</ChapterNav>
