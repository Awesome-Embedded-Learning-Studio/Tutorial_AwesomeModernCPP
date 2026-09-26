---
title: Pointer Semantics and Weak Reference Design
description: From `T*` to `Borrowed`, `ObserverPtr`, and Chrome-like `WeakPtr`, understanding
  the semantic boundaries and safe implementations of non-owning pointers
translation:
  source: documents/vol8-domains/cpp-deep-dives/pointer-semantics/index.md
  source_hash: 5944640b09e9caba4cc076df79bec1fc94ea4abad3117516e41d39672e8240f5
  translated_at: '2026-05-26T11:56:13.110580+00:00'
  engine: anthropic
  token_count: 249
---
# Pointer Semantics and Weak Reference Design

Pointers are everywhere in C++, but not all pointers "own" the objects they point to. A raw pointer `T*` can be either owning or non-owning, `T&` expresses a borrow but cannot be null, and `std::weak_ptr` solves the weak reference problem under shared ownership—but what if you don't use `std::shared_ptr` to manage objects? How is Chromium's `base::WeakPtr` designed? And why does `std::observer_ptr` look like a WeakPtr but actually isn't?

In this topic, we build various non-owning pointer types from scratch. As we implement them, we clarify their semantic boundaries, safety conditions, and engineering trade-offs.

## Chapter Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-non-owning-pointer-overview">Non-owning pointers panorama: From T* to Borrowed to ObserverPtr</ChapterLink>
  <ChapterLink href="02-unsafe-weakptr-ub">WeakPtr Anti-pattern: The Fatal Trap of T* + raw Flag*</ChapterLink>
  <ChapterLink href="03-simple-weakptr">SimpleWeakPtr: safe improvements with T* + shared_ptr&lt;Flag&gt;</ChapterLink>
  <ChapterLink href="04-chrome-weakptr">Chrome-like WeakPtr: Reference Counting Control Block and WeakPtrFactory</ChapterLink>
  <ChapterLink href="05-weakptr-comparison-and-async">`std::weak_ptr` Comparison and Asynchronous Callback Practice</ChapterLink>
  <ChapterLink href="06-design-principles">Cross-Thread Safety, Performance Trade-offs, and Design Principles Summary</ChapterLink>
</ChapterNav>
