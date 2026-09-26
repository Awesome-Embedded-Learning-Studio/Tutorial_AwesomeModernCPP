---
title: "auto and decltype"
description: "A complete guide to type deduction: auto, decltype, and CTAD"
translation:
  source: documents/vol2-modern-features/ch06-auto-decltype/index.md
  source_hash: 9778aca544ce6ee96cabf4d80cd863013ef59c4cb1d42247cb1d80d624c1d866
  translated_at: '2026-09-25T15:37:45+00:00'
  engine: anthropic
  token_count: 190
---
# auto and decltype

auto is not just a lazy shortcut for skipping type names — it has complete deduction rules of its own, proxy-type pitfalls, and semantics that stay consistent with template argument deduction. decltype is the cornerstone of template metaprogramming, letting us capture an expression's type exactly. C++17's CTAD then makes template argument deduction more concise than ever. In this chapter we thoroughly sort out the three dimensions of type deduction: auto, decltype, and CTAD.

## Chapter Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-auto-deep-dive">Deep Dive into auto Deduction: More Than Just Laziness</ChapterLink>
  <ChapterLink href="02-decltype">decltype and Return Type Deduction</ChapterLink>
  <ChapterLink href="03-ctad">Class Template Argument Deduction (CTAD)</ChapterLink>
</ChapterNav>
