---
title: "Modern Approaches to Error Handling"
description: "From error codes to expected: the evolution and selection of error handling strategies"
translation:
  source: documents/vol2-modern-features/ch10-error-handling/index.md
  source_hash: bd5dec8d9d20937bec40f35c50389e11fe115a7a1f8adeecc5028d4e6245d9ce
  translated_at: '2026-09-25T16:42:44+00:00'
  engine: anthropic
  token_count: 190
---
# Modern Approaches to Error Handling

Error handling is a core problem every C++ programmer has to face — C-style error codes can be ignored, exceptions are restricted in embedded environments, and bare pointers have unclear semantics for representing "no value." Modern C++ provides type-safe alternatives such as optional, variant, and expected. In this chapter we review the evolution of error handling, master the scenarios where each approach fits, and close with a scenario-based selection guide.

## Chapter Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-error-handling-evolution">Evolution of Error Handling: From Error Codes to Type Safety</ChapterLink>
  <ChapterLink href="02-optional-error">optional for Error Handling</ChapterLink>
  <ChapterLink href="03-expected-error">std::expected&lt;T, E&gt;: Type-Safe Error Propagation</ChapterLink>
  <ChapterLink href="04-error-patterns">Error Handling Patterns: A Selection Guide and Best Practices</ChapterLink>
</ChapterNav>
