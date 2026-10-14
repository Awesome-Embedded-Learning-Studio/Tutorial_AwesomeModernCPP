---
title: "How Function Return Values Get Back: From rax to Return Value Optimization"
description: "A return statement looks like one line of source; underneath it there are three routes: rax, a hidden pointer, and RVO. This topic measures when each route triggers and what it looks like in assembly, so after reading you can judge which route a large object will take."
sidebar_order: 8
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/08-return-value-journey/index.md
  source_hash: 45afa1d0de3bfd0183ed3f67603d6736fedbdbe97f4ea783e3c9b4892c837e64
  translated_at: '2026-10-14'
  engine: manual
---

# How Function Return Values Get Back: From rax to Return Value Optimization

A return statement looks like one line of source; underneath it there are three routes: rax, a hidden pointer, and RVO. This topic measures when each route triggers and what it looks like in assembly, so after reading you can judge which route a large object will take.

<ChapterNav variant="sub">
  <ChapterLink href="01-rax-family-and-register-pairs">Function Return Values: the rax Family and Register Pairs</ChapterLink>
  <ChapterLink href="02-sret-and-routing-criteria">Large Objects Return Through a Hidden Pointer: The sret Route and the Routing Criteria</ChapterLink>
  <ChapterLink href="03-nrvo-copy-elision">NRVO: The Copy on the Return Path Never Existed</ChapterLink>
  <ChapterLink href="04-multivalue-return-and-channel-recap">Returning Multiple Values: pair Packing and the Full Picture of the Three Routes</ChapterLink>
</ChapterNav>
