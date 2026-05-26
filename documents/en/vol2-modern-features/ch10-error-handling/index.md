---
title: Modern Approaches to Error Handling
description: 'From Error Codes to expected: The Evolution and Selection of Error Handling
  Strategies'
translation:
  source: documents/vol2-modern-features/ch10-error-handling/index.md
  source_hash: bd5dec8d9d20937bec40f35c50389e11fe115a7a1f8adeecc5028d4e6245d9ce
  translated_at: '2026-05-26T11:35:58.366489+00:00'
  engine: anthropic
  token_count: 151
---
## Modern Error Handling

Error handling is a core challenge every C++ programmer must face—C-style error codes can be silently ignored, exceptions are restricted in embedded environments, and bare pointers lack clear semantics for representing "no value." Modern C++ provides type-safe alternatives like `optional`, `variant`, and `expected`. In this chapter, we review the evolution of error handling, master the use cases for each approach, and finally provide a scenario-based selection guide.

## Chapter Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-error-handling-evolution">Error Handling Evolution: From Error Codes to Type Safety</ChapterLink>
  <ChapterLink href="02-optional-error">Using optional for Error Handling</ChapterLink>
  <ChapterLink href="03-expected-error">std::expected&lt;T, E&gt;: Type-Safe Error Propagation</ChapterLink>
  <ChapterLink href="04-error-patterns">Error Handling Patterns Summary: Selection Guide and Best Practices</ChapterLink>
</ChapterNav>
