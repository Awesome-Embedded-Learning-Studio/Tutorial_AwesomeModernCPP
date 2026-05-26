---
title: Attribute System
description: Use standard attributes to make the compiler your code reviewer
translation:
  source: documents/vol2-modern-features/ch07-attributes/index.md
  source_hash: 8c155b06ab814cf4536eaeaf7446a3289e59844d2cc38749bf6bee7d9e9bad16
  translated_at: '2026-05-26T11:31:18.241779+00:00'
  engine: anthropic
  token_count: 108
---
# Attribute System

C++ attributes let you pass extra information to the compiler without changing code logic—"don't ignore this return value", "this variable might be unused but don't warn", "this branch is more likely to execute"... This information helps us catch bugs, suppress unnecessary warnings, and even optimize performance. In this chapter, we cover the standard attributes from C++11-17 and the performance-oriented new attributes in C++20-23.

## Chapter Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-standard-attributes">Standard Attributes in Depth: Making the Compiler Your Code Reviewer</ChapterLink>
  <ChapterLink href="02-modern-attributes">C++20-23 New Attributes: Performance-Oriented Compiler Hints</ChapterLink>
</ChapterNav>
