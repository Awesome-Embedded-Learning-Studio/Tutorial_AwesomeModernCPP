---
title: "Do i++ and ++i Really Produce the Same Instructions?"
description: "i++ and ++i differ by one character in source, but their story at the instruction level is longer. This topic compiles one source file at -O0 and -O2 and compares the outputs, pinning down exactly where the side effect happens."
sidebar_order: 1
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/01-prefix-vs-postfix-increment/index.md
  source_hash: 7c3d8762517f99d2ed09384f7df8c3d23ce9298497818eafcdbefba8cd27b050
  translated_at: '2026-10-14'
  engine: manual
---

# Do i++ and ++i Really Produce the Same Instructions?

i++ and ++i differ by one character in source, but their story at the instruction level is longer. This topic compiles one source file at -O0 and -O2 and compares the outputs, pinning down exactly where the side effect happens.

<ChapterNav variant="sub">
  <ChapterLink href="01-int-instructions">i++ vs ++i: Comparing int Assembly at -O0</ChapterLink>
  <ChapterLink href="02-class-type-cost">i++ vs ++i: The Copy Cost of Class Types</ChapterLink>
  <ChapterLink href="03-vector-and-side-effects">i++ and ++i: Vector Iterators and the Boundary of Side Effects</ChapterLink>
</ChapterNav>
