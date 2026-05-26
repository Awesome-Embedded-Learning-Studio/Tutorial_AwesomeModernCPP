---
title: Type Safety
description: Building a safer type system with strong types, variant, optional, and
  any
translation:
  source: documents/vol2-modern-features/ch04-type-safety/index.md
  source_hash: 527b00525f8a71088de0d1e6f9b25ae2f60b6d17e240cef8116ce748131292d1
  translated_at: '2026-05-26T11:28:22.649143+00:00'
  engine: anthropic
  token_count: 167
---
# Type Safety

C-era enums, unions, and raw pointers leave behind too many type safety pitfalls—implicit conversions, undefined behavior (UB), null pointer dereferences... Modern C++ provides a suite of tools to plug these holes. In this chapter, we explore how `enum class` puts an end to the nightmare of implicit enum conversions, how strong-typed typedefs prevent parameter mix-ups, how `variant` safely replaces unions, how `optional` elegantly expresses "possibly no value," and how `any` achieves type erasure when needed.

## Chapter Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-enum-class">enum class and Strongly-Typed Enums</ChapterLink>
  <ChapterLink href="02-strong-types">Strong-Typed Typedefs: Type Safety Against Mix-ups</ChapterLink>
  <ChapterLink href="03-variant">std::variant: Type-Safe Unions</ChapterLink>
  <ChapterLink href="04-optional">std::optional: Elegantly Expressing "Possibly No Value"</ChapterLink>
  <ChapterLink href="05-any">std::any and Type Erasure</ChapterLink>
</ChapterNav>
