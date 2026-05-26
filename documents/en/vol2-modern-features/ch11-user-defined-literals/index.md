---
title: user-defined literal
description: Implementing type-safe literals and unit systems with `operator""`
translation:
  source: documents/vol2-modern-features/ch11-user-defined-literals/index.md
  source_hash: c5b33b68a24f78da2aec03406e1bc2d355c9db2ad34f8a605c8f8427fc307fa8
  translated_at: '2026-05-26T11:36:08.841930+00:00'
  engine: anthropic
  token_count: 103
---
# User-Defined Literals

User-defined literals (UDLs) let you assign custom semantics to literal values — `100_m` means 100 meters, `1.5_rad` means radians, and `"hello"sv` means `string_view`. Combined with `constexpr` and strong types, UDLs can perform unit checking and type conversions at compile time, making them a powerful tool for implementing type-safe physical unit libraries.

## Chapter Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-udl-basics">User-Defined Literal Basics</ChapterLink>
  <ChapterLink href="02-udl-practice">UDL in Practice: A Type-Safe Unit System</ChapterLink>
</ChapterNav>
