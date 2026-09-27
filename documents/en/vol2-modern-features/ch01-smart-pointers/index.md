---
title: "Smart Pointers and RAII"
description: "Automatic resource management with RAII and smart pointers"
translation:
  source: documents/vol2-modern-features/ch01-smart-pointers/index.md
  source_hash: 16f5a54103e34dcf1c1ebcf087fbed481f5f7bff11020b308d5808a64c2d5768
  translated_at: '2026-09-27T05:02:06+00:00'
  engine: anthropic
  token_count: 300
---
# Smart Pointers and RAII

Managing resources by hand (new/delete, fopen/fclose, lock/unlock) is the source of C++ programmers' nightmares. The RAII principle tells us: bind acquiring a resource to an object's construction, hand releasing it to the destructor, and let the scope manage the lifetime for you. In this chapter we first build a deep understanding of RAII, then set up the "ownership" model—exclusive, shared, and borrowed, each in its rightful place—then walk through the design philosophy and correct usage of unique_ptr, shared_ptr, and weak_ptr one by one, and finally see how custom deleters and scope_guard handle more complex resource scenarios.

## Chapter Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-raii-deep-dive">Deep Dive into RAII: The Cornerstone of Resource Management</ChapterLink>
  <ChapterLink href="02-ownership-model">Resource Ownership: Exclusive, Shared, and Borrowed</ChapterLink>
  <ChapterLink href="03-unique-ptr">Deep Dive into unique_ptr: The Zero-Overhead Smart Pointer with Exclusive Ownership</ChapterLink>
  <ChapterLink href="04-shared-ptr">Deep Dive into shared_ptr: Shared Ownership and Reference Counting</ChapterLink>
  <ChapterLink href="05-weak-ptr">weak_ptr and Circular References</ChapterLink>
  <ChapterLink href="06-custom-deleter">Custom Deleters and Intrusive Reference Counting</ChapterLink>
  <ChapterLink href="07-scope-guard">scope_guard and defer: A General-Purpose Scope Guard</ChapterLink>
  <ChapterLink href="08-pimpl">The PIMPL Idiom: unique_ptr, shared_ptr, and Incomplete Types</ChapterLink>
</ChapterNav>
