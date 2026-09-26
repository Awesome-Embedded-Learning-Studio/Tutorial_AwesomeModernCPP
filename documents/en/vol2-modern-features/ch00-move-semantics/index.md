---
title: "Move Semantics and Rvalue References"
description: "Understand the value category system, then master move construction, RVO, and perfect forwarding"
translation:
  source: documents/vol2-modern-features/ch00-move-semantics/index.md
  source_hash: 51998633f4cedaadbb53e1af4e55b95dbd68d6a3cd5e654d8e997518b1b44a3d
  translated_at: '2026-09-25T14:10:20+00:00'
  engine: anthropic
  token_count: 250
---
# Move Semantics and Rvalue References

Move semantics is one of the most important features C++11 gave us—it makes "transferring ownership of resources" a first-class citizen and has completely changed how we deal with the cost of copying. In this chapter we start from the value category system, getting clear on what an lvalue is, what an rvalue is, and why we need rvalue references; then we dig into how move construction and move assignment actually work, see how much work the compiler's RVO optimization saves you; and finally we tie everything together with perfect forwarding. Move semantics isn't just a performance optimization—it is the cornerstone of understanding resource management in modern C++.

## Chapter Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-rvalue-reference">Rvalue References: From Copy to Move</ChapterLink>
  <ChapterLink href="02-move-semantics">Move Construction and Move Assignment</ChapterLink>
  <ChapterLink href="03-rvo-nrvo">RVO and NRVO: The Compiler's Return Value Optimization</ChapterLink>
  <ChapterLink href="04-perfect-forwarding">Perfect Forwarding: Preserving Value Categories Exactly</ChapterLink>
  <ChapterLink href="05-move-in-practice">Move Semantics in Practice: From STL to Custom Types</ChapterLink>
</ChapterNav>
