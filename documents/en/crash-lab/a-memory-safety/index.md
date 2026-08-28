---
title: "Memory Safety"
sidebar_order: 1
---

# Memory Safety

Most of the murders on the heap and the stack fall into a few families: dereferencing a null pointer, still using memory after freeing it, double-freeing the same block, writing past a buffer. They are the most common and most lethal batch of C++ crashes, and ASan bags them one after another.

- [01 · Null Pointer Dereference: The Crash That Hides Nothing](01-null-deref)
- [02 · Use-After-Free: The Pointer Outlives the Memory](02-use-after-free)

The remaining cases (double free, stack overflow, memory leaks, alignment violations, etc.) will be moved in over time.
