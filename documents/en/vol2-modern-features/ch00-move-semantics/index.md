---
title: Move Semantics and Rvalue References
description: Understand the value category system, and master move construction, RVO,
  and perfect forwarding.
translation:
  source: documents/vol2-modern-features/ch00-move-semantics/index.md
  source_hash: 51998633f4cedaadbb53e1af4e55b95dbd68d6a3cd5e654d8e997518b1b44a3d
  translated_at: '2026-05-26T11:18:52.201809+00:00'
  engine: anthropic
  token_count: 166
---
# Move Semantics and Rvalue References

Move semantics is one of the most important features in C++11—it makes "transferring resource ownership" a first-class citizen, fundamentally changing how we handle copy overhead. In this chapter, we start from the value category system to understand what lvalues and rvalues are, and why we need rvalue references. We then dive into the implementation principles of move constructors and move assignments, see how much the compiler's RVO optimization saves you, and finally tie everything together with perfect forwarding. Move semantics is not just a performance optimization; it is the cornerstone of understanding resource management in modern C++.

## Chapter Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-rvalue-reference">Rvalue References: From Copy to Move</ChapterLink>
  <ChapterLink href="02-move-semantics">Move Constructor and Move Assignment</ChapterLink>
  <ChapterLink href="03-rvo-nrvo">RVO and NRVO: Compiler Return Value Optimization</ChapterLink>
  <ChapterLink href="04-perfect-forwarding">Perfect Forwarding: Preserving Value Categories in Passing</ChapterLink>
  <ChapterLink href="05-move-in-practice">Move Semantics in Practice: From STL to Custom Types</ChapterLink>
</ChapterNav>
