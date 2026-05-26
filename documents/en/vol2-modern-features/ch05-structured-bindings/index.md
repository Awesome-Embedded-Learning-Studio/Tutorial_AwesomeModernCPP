---
title: Structured Bindings and Initialization
description: Unpack multiple values on a single line, narrowing variable scope
translation:
  source: documents/vol2-modern-features/ch05-structured-bindings/index.md
  source_hash: a2666aa7a0e3ad4117968b082c4339b2311df99308b1615d101d79c0f8b44945
  translated_at: '2026-05-26T11:29:06.359286+00:00'
  engine: anthropic
  token_count: 105
---
# Structured Bindings and Init Statements

C++17 structured bindings let us unpack pairs, tuples, arrays, and structs in a single line—no more ugly ``std::tie`` syntax. Combined with if/switch init statements, we can limit variable scope to exactly where it is needed, preventing variables from leaking into the outer scope. Although this chapter contains only two articles, both cover features we use frequently in day-to-day development.

## Chapter Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-structured-bindings">Structured Bindings: Unpacking Multiple Values in One Line</ChapterLink>
  <ChapterLink href="02-init-statements">if/switch Init Statements: Narrowing Variable Scope</ChapterLink>
</ChapterNav>
