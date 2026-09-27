---
title: "Type Safety"
description: "Building a safer type system with strong types, variant, optional, and any"
translation:
  source: documents/vol2-modern-features/ch04-type-safety/index.md
  source_hash: 527b00525f8a71088de0d1e6f9b25ae2f60b6d17e240cef8116ce748131292d1
  translated_at: '2026-09-25T15:29:29+00:00'
  engine: anthropic
  token_count: 220
---
# Type Safety

Enums, unions, and raw pointers from the C era left behind far too many type-safety hazards — implicit conversions, undefined behavior, null pointer dereferences... Modern C++ ships a whole toolbox for plugging these holes. In this chapter we look at how enum class ends the nightmare of implicit enum conversions, how strong typedefs prevent parameter mix-ups, how variant safely replaces union, how optional elegantly expresses "a value may be absent", and how any delivers type erasure when you need it.

## Chapter Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-enum-class">enum class and Scoped Enums</ChapterLink>
  <ChapterLink href="02-strong-types">Strong Typedefs: Type Safety That Prevents Mix-Ups</ChapterLink>
  <ChapterLink href="03-variant">std::variant: A Type-Safe Union</ChapterLink>
  <ChapterLink href="04-optional">std::optional: Elegantly Expressing 'A Value May Be Absent'</ChapterLink>
  <ChapterLink href="05-any">std::any and Type Erasure</ChapterLink>
</ChapterNav>
