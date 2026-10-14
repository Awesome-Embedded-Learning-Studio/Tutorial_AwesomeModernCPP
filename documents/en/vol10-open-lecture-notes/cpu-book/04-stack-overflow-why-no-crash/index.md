---
title: "Why an Out-of-Bounds Array Write on the Stack Sometimes Doesn't Crash"
description: "An out-of-bounds write smashes the stack frame, and the program often refuses to crash. This topic lays each real outcome on the table: writing into padding, corrupting a tolerant neighbor, getting caught by the canary, and, at -O2, writing into empty space."
sidebar_order: 4
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/04-stack-overflow-why-no-crash/index.md
  source_hash: 3e15bb8874fb14e63dd233874404bfcb02b5b0369b15d474bb75e1c66c12f1cf
  translated_at: '2026-10-14'
  engine: manual
---

# Why an Out-of-Bounds Array Write on the Stack Sometimes Doesn't Crash

An out-of-bounds write smashes the stack frame, and the program often refuses to crash. This topic lays each real outcome on the table: writing into padding, corrupting a tolerant neighbor, getting caught by the canary, and, at -O2, writing into empty space.

<ChapterNav variant="sub">
  <ChapterLink href="01-stack-frame-neighbors">Neighbors in the Stack Frame: Who Takes the Out-of-Bounds Write</ChapterLink>
  <ChapterLink href="02-hitting-canary-saved-rbp-and-return-address">Hitting Farther: The Canary, Saved rbp, and the Return Address</ChapterLink>
  <ChapterLink href="03-canary-defense-and-the-o2-story">The Canary Defense and a Different Story at -O2</ChapterLink>
  <ChapterLink href="04-stack-headroom-asan-and-luck">Stack Headroom, ASan, and Luck</ChapterLink>
</ChapterNav>
