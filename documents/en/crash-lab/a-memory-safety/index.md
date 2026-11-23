---
title: "Memory Safety"
sidebar_order: 1
translation:
  source: documents/crash-lab/a-memory-safety/index.md
  source_hash: c9199f14df1598766f715450b5f1bd87fef3110cb7a0ba8e48c68f25d8ff7f21
  translated_at: '2026-11-22T16:38:48+00:00'
  engine: glm-5.3-refresh
  token_count: 313
---

# Memory Safety

Most of the murders committed on the heap and the stack fall into a few families: dereferencing a null pointer, still using memory after freeing it, calling `free` on the same block twice, writing past the end of a buffer. They are the most common and most lethal batch of C++ crashes, and ASan bags every single one.

- [01 · Null Pointer Dereference: The Crash That Hides Nothing](01-null-deref)
- [02 · Use-After-Free: The Pointer Outlives the Memory](02-use-after-free)
- [03 · Heap Buffer Overflow: One Extra Write Is Okay, Two Extra Writes Are Fatal](03-heap-buffer-overflow)

The remaining cases (double free, stack overflow, memory leaks, alignment violations, and so on) will be moved in over time.
