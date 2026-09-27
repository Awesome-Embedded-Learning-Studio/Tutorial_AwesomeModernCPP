---
title: "Move Semantics and Rvalue References"
description: "Understand the value category system, then master move construction, RVO, and perfect forwarding"
translation:
  source: documents/vol2-modern-features/ch00-move-semantics/index.md
  source_hash: b1f344b67b982d6e4ab96c0aa812f5876ae608b5c318c04437e5dabfc78c3fdc
  translated_at: '2026-09-27T04:41:26+00:00'
  engine: anthropic
  token_count: 400
---
# Move Semantics and Rvalue References

Move semantics is one of the most important features C++11 gave us—it makes "transferring ownership of resources" a first-class citizen and has completely changed how we deal with the cost of copying. In this chapter we start from the value category system, getting clear on what an lvalue is, what an rvalue is, and why we need rvalue references; then we dig into how move construction and move assignment are actually implemented, sort out how the special member functions of the Rule of Five fit together, and see how much work the compiler's RVO optimization saves you; next, perfect forwarding ties everything together; and finally we land on two articles of hands-on practice: the payoff from standard library containers with measured benchmarks, and how to bring moves into your own types. Move semantics isn't just a performance optimization—it is the cornerstone of understanding resource management in modern C++.

## Chapter Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-rvalue-reference">Rvalue References: From Copy to Move</ChapterLink>
  <ChapterLink href="02-move-semantics">Move Construction and Move Assignment</ChapterLink>
  <ChapterLink href="03-rule-of-five">The Rule of Five: How the Special Member Functions Fit Together</ChapterLink>
  <ChapterLink href="04-rvo-nrvo">RVO and NRVO: The Compiler's Return Value Optimization</ChapterLink>
  <ChapterLink href="05-perfect-forwarding">Perfect Forwarding: Keeping Value Categories Intact</ChapterLink>
  <ChapterLink href="06-move-and-stl">Move Semantics in Practice: Standard Library Containers and Performance Benchmarks</ChapterLink>
  <ChapterLink href="07-move-custom-types">Move Semantics in Practice: Custom Types and Resource Handles</ChapterLink>
</ChapterNav>
