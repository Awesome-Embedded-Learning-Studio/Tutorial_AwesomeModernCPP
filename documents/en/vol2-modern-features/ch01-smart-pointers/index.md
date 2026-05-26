---
title: Smart Pointers and RAII
description: Implementing automatic resource management with RAII and smart pointers
translation:
  source: documents/vol2-modern-features/ch01-smart-pointers/index.md
  source_hash: 4f63d303b554985d7e4a22a64eaa9ecab4a1c3b8117f319ca775db04ee746ca0
  translated_at: '2026-05-26T11:22:42.661092+00:00'
  engine: anthropic
  token_count: 193
---
# Smart Pointers and RAII

Manually managing resources (`new`/`delete`, `fopen`/`fclose`, `lock`/`unlock`) is a nightmare source of bugs for C++ programmers. The RAII (Resource Acquisition Is Initialization) principle tells us to bind resource acquisition to object construction, leave release to destructors, and let scope manage the lifetime for you. In this chapter, we first dive deep into RAII, then master the design philosophy and correct usage of `unique_ptr`, `shared_ptr`, and `weak_ptr` one by one, and finally see how custom deleters and scope guards handle more complex resource scenarios.

## Chapter Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-raii-deep-dive">Deep Dive into RAII: The Cornerstone of Resource Management</ChapterLink>
  <ChapterLink href="02-unique-ptr">Understanding unique_ptr: Zero-Overhead Smart Pointer with Exclusive Ownership</ChapterLink>
  <ChapterLink href="03-shared-ptr">Understanding shared_ptr: Shared Ownership and Reference Counting</ChapterLink>
  <ChapterLink href="04-weak-ptr">weak_ptr and Circular References</ChapterLink>
  <ChapterLink href="05-custom-deleter">Custom Deleters and Intrusive Reference Counting</ChapterLink>
  <ChapterLink href="06-scope-guard">scope_guard and defer: Generic Scope Guards</ChapterLink>
</ChapterNav>
