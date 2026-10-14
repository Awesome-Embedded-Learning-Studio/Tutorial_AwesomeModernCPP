---
title: "Template Instantiation Compared: The Code the Compiler Writes for You"
description: "You write a template once; the compiler prints one copy per type used. This topic compares the printed code, the linker’s deduplication, the explicit-instantiation switches, and the unit at which class templates instantiate. The compile-time mechanism of templates comes apart into exactly these pieces."
sidebar_order: 11
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/11-template-instantiation/index.md
  source_hash: 8141a70f48f2fd8348b78a1f089917b054a331f772d784109d7261427e587c3a
  translated_at: '2026-10-14'
  engine: manual
---

# Template Instantiation Compared: The Code the Compiler Writes for You

You write a template once; the compiler prints one copy per type used. This topic compares the printed code, the linker’s deduplication, the explicit-instantiation switches, and the unit at which class templates instantiate. The compile-time mechanism of templates comes apart into exactly these pieces.

<ChapterNav variant="sub">
  <ChapterLink href="01-on-demand-copies-and-linker-dedup">Template Instantiation Compared: On-Demand Copies and Linker Deduplication</ChapterLink>
  <ChapterLink href="02-size-explicit-and-class-templates">Template Instantiation Compared: Size, the Manual Switches, and Class Templates</ChapterLink>
</ChapterNav>
