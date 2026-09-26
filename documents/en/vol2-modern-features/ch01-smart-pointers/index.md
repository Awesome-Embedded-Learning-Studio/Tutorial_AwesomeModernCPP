---
title: "Smart Pointers and RAII"
description: "Automatic resource management with RAII and smart pointers"
translation:
  source: documents/vol2-modern-features/ch01-smart-pointers/index.md
  source_hash: 4f63d303b554985d7e4a22a64eaa9ecab4a1c3b8117f319ca775db04ee746ca0
  translated_at: '2026-09-25T14:28:01+00:00'
  engine: anthropic
  token_count: 230
---
# Smart Pointers and RAII

Managing resources by hand (new/delete, fopen/fclose, lock/unlock) is the root of C++ programmers' nightmares. The RAII principle tells us: bind acquiring a resource to an object's construction, leave releasing it to the destructor, and let the scope manage the lifetime for you. In this chapter we first build a deep understanding of RAII, then master the design philosophy and correct usage of unique_ptr, shared_ptr, and weak_ptr one by one, and finally see how custom deleters and scope_guard handle more complex resource scenarios.

## Chapter Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-raii-deep-dive">Deep Dive into RAII: The Cornerstone of Resource Management</ChapterLink>
  <ChapterLink href="02-unique-ptr">Deep Dive into unique_ptr: A Zero-Overhead Smart Pointer with Exclusive Ownership</ChapterLink>
  <ChapterLink href="03-shared-ptr">Deep Dive into shared_ptr: Shared Ownership and Reference Counting</ChapterLink>
  <ChapterLink href="04-weak-ptr">weak_ptr and Circular References</ChapterLink>
  <ChapterLink href="05-custom-deleter">Custom Deleters and Intrusive Reference Counting</ChapterLink>
  <ChapterLink href="06-scope-guard">scope_guard and defer: A General-Purpose Scope Guard</ChapterLink>
</ChapterNav>
