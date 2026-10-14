---
title: "Function Calls and Returns: Stack Frames, call, and ret"
description: "To the CPU, a function call is a small set of agreed-upon moves: call jumps in, the prologue sets the stage, the epilogue cleans up, and ret jumps back. This topic walks each move instruction by instruction; what leaf functions save, which way the stack grows, and what -O2 rewrites are all settled by assembly."
sidebar_order: 2
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/02-function-calls-and-stack-frames/index.md
  source_hash: 2b4a39483369088f937c72e237287db95e8c9feb78ca3587f574cdee52f421e6
  translated_at: '2026-10-14'
  engine: manual
---

# Function Calls and Returns: Stack Frames, call, and ret

To the CPU, a function call is a small set of agreed-upon moves: call jumps in, the prologue sets the stage, the epilogue cleans up, and ret jumps back. This topic walks each move instruction by instruction; what leaf functions save, which way the stack grows, and what -O2 rewrites are all settled by assembly.

<ChapterNav variant="sub">
  <ChapterLink href="01-call-and-stack-frame-walkthrough">One Function Call, as the CPU Sees It</ChapterLink>
  <ChapterLink href="02-leaf-functions-stack-direction-and-o2">Leaf Functions, Stack Direction, and Call Sites at -O2</ChapterLink>
  <ChapterLink href="03-kept-frames-and-stack-exhaustion">Functions That Keep Their Stack Frames, and Recursion That Fills the Stack</ChapterLink>
</ChapterNav>
