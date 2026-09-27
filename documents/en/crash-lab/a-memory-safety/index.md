---
title: "Memory Safety"
sidebar_order: 1
translation:
  source: documents/crash-lab/a-memory-safety/index.md
  source_hash: 7902292a18d26762974d39099059a6ba8440388af5c456db710e47da73ff52a1
  translated_at: '2026-09-27T03:01:03+00:00'
  engine: anthropic
  token_count: 130
---

# Memory Safety

Most of the murders committed on the heap and the stack fall into a few families: dereferencing a null pointer, still using memory after freeing it, calling `free` on the same block twice, writing past the end of a buffer. They are the most common and most lethal batch of C++ crashes, and ASan bags every single one.

- [01 · Null Pointer Dereference: The Crash That Hides Nothing](01-null-deref)
- [02 · Use-After-Free: The Pointer Outlives the Memory](02-use-after-free)

The remaining cases (heap buffer overflow, double free, stack overflow, memory leaks, alignment violations, and so on) will be moved in over time.
