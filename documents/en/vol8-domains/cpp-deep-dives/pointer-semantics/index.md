---
title: "Pointer Semantics and Weak Reference Design"
description: "From T* to Borrowed, ObserverPtr, and Chrome-like WeakPtr — understanding the semantic boundaries and safe implementations of non-owning pointers"
translation:
  source: documents/vol8-domains/cpp-deep-dives/pointer-semantics/index.md
  source_hash: 5944640b09e9caba4cc076df79bec1fc94ea4abad3117516e41d39672e8240f5
  translated_at: '2026-09-27T02:52:17+00:00'
  engine: anthropic
  token_count: 310
---

# Pointer Semantics and Weak Reference Design

Pointers are everywhere in C++, yet not every pointer *owns* the object it points to. A raw `T*` can be owning or non-owning, `T&` expresses a borrow but can never be null, and `std::weak_ptr` solves the weak-reference problem under shared ownership — but what if you don't manage your objects with `shared_ptr`? How is Chromium's `WeakPtr` designed? And why does `T* + raw Flag*` look like a WeakPtr while not actually being one?

In this series we hand-roll all kinds of non-owning pointer types from scratch, pinning down their semantic boundaries, safety conditions, and engineering trade-offs as we build.

## Chapter Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-non-owning-pointer-overview">Non-Owning Pointers: A Panorama from T* to Borrowed to ObserverPtr</ChapterLink>
  <ChapterLink href="02-unsafe-weakptr-ub">WeakPtr Anti-Pattern: The Fatal Trap of T* + raw Flag*</ChapterLink>
  <ChapterLink href="03-simple-weakptr">SimpleWeakPtr: A Safety Improvement over T* + shared_ptr&lt;Flag&gt;</ChapterLink>
  <ChapterLink href="04-chrome-weakptr">Chrome-like WeakPtr: Reference-Counted Control Block and WeakPtrFactory</ChapterLink>
  <ChapterLink href="05-weakptr-comparison-and-async">std::weak_ptr Compared: Async Callbacks in Practice</ChapterLink>
  <ChapterLink href="06-design-principles">Series Wrap-Up: Cross-Thread Safety, Performance Trade-offs, and Design Principles</ChapterLink>
</ChapterNav>
