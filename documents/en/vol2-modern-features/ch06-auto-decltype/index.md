---
title: '`auto` and `decltype`'
description: 'A complete guide to type deduction: `auto`, `decltype`, and CTAD'
translation:
  source: documents/vol2-modern-features/ch06-auto-decltype/index.md
  source_hash: 9778aca544ce6ee96cabf4d80cd863013ef59c4cb1d42247cb1d80d624c1d866
  translated_at: '2026-05-26T11:30:29.430384+00:00'
  engine: anthropic
  token_count: 124
---
# auto and decltype

`auto` is more than just a shortcut to avoid writing types—it has complete deduction rules, proxy-type pitfalls, and consistency with template deduction. `decltype` is the cornerstone of template metaprogramming, allowing us to precisely obtain the type of an expression. C++17's CTAD makes template argument deduction more concise than ever. In this chapter, we thoroughly cover the three dimensions of type deduction: `auto`, `decltype`, and CTAD.

## Chapter Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-auto-deep-dive">A Deep Dive into auto: More Than Just a Shortcut</ChapterLink>
  <ChapterLink href="02-decltype">decltype and Return Type Deduction</ChapterLink>
  <ChapterLink href="03-ctad">Class Template Argument Deduction (CTAD)</ChapterLink>
</ChapterNav>
