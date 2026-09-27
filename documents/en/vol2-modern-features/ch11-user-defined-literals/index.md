---
title: "User-Defined Literals"
description: "Type-safe literals and a unit system with operator\"\""
translation:
  source: documents/vol2-modern-features/ch11-user-defined-literals/index.md
  source_hash: c5b33b68a24f78da2aec03406e1bc2d355c9db2ad34f8a605c8f8427fc307fa8
  translated_at: '2026-09-25T16:56:06+00:00'
  engine: anthropic
  token_count: 300
---
# User-Defined Literals

User-defined literals (UDLs) let you attach custom semantics to literal values — `100_m` means 100 meters, `3.14_rad` means radians, and `"hello"_sv` means a string_view. Combined with constexpr and strong types, UDLs can perform unit checking and type conversion at compile time, making them a powerful tool for building type-safe physical unit libraries.

## Chapter Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-udl-basics">User-Defined Literals: The Basics</ChapterLink>
  <ChapterLink href="02-udl-practice">UDL in Practice: A Type-Safe Unit System</ChapterLink>
</ChapterNav>
