---
title: "Attribute System"
description: "Use standard attributes to make the compiler your code reviewer"
translation:
  source: documents/vol2-modern-features/ch07-attributes/index.md
  source_hash: 8c155b06ab814cf4536eaeaf7446a3289e59844d2cc38749bf6bee7d9e9bad16
  translated_at: '2026-09-25T15:59:52+00:00'
  engine: anthropic
  token_count: 170
---
# Attribute System

C++ attributes let you pass extra information to the compiler without changing your code's logic — "don't ignore this return value", "this variable may go unused, but don't warn about it", "this branch is more likely to execute"... That information helps you catch bugs, silence unnecessary warnings, and even improve performance. In this chapter we cover the standard attributes from C++11-17 and the new performance-oriented attributes from C++20-23.

## Chapter Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-standard-attributes">Deep Dive into Standard Attributes: Making the Compiler Your Code Reviewer</ChapterLink>
  <ChapterLink href="02-modern-attributes">C++20-23 New Attributes: Performance-Oriented Compiler Hints</ChapterLink>
</ChapterNav>
