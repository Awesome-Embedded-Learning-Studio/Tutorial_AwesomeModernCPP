---
title: "How Arguments Get Into Registers: Passing Under the System V AMD64 ABI"
description: "You write parameters between parentheses; the CPU reads them out of registers. This topic verifies the routes of the first integer and floating-point arguments one by one under the System V AMD64 convention, plus where extra arguments go, how structs get split, and how values come back."
sidebar_order: 3
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/03-arguments-in-registers/index.md
  source_hash: 4a66c239ad918486ffcc883100e63355b2110c71a5a8c10fec1dc65bbbfad063
  translated_at: '2026-10-14'
  engine: manual
---

# How Arguments Get Into Registers: Passing Under the System V AMD64 ABI

You write parameters between parentheses; the CPU reads them out of registers. This topic verifies the routes of the first integer and floating-point arguments one by one under the System V AMD64 convention, plus where extra arguments go, how structs get split, and how values come back.

<ChapterNav variant="sub">
  <ChapterLink href="01-integer-args-register-allocation">How Arguments Get Into Registers: The Integer Allocation Table</ChapterLink>
  <ChapterLink href="02-stack-args-and-return-values">The 7th Argument Goes on the Stack, and the Two Routes a Return Value Takes</ChapterLink>
  <ChapterLink href="03-floats-structs-and-abi-docs">Float Arguments, Struct Packing, and Where the ABI Document Comes From</ChapterLink>
</ChapterNav>
