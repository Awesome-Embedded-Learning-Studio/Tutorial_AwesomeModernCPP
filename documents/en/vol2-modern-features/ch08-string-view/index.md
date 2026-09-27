---
title: "Deep Dive into string_view"
description: "The principles, performance, and pitfalls of non-owning string views"
translation:
  source: documents/vol2-modern-features/ch08-string-view/index.md
  source_hash: 2ad4ae19e561ba3780de6b782d56cfebd8ad5f5fad157b41b083cff49c2b013d
  translated_at: '2026-09-25T15:59:48+00:00'
  engine: anthropic
  token_count: 160
---
# Deep Dive into string_view

`string_view` is one of those types C++17 gave us that looks disarmingly simple but hides real subtleties — it is nothing more than a pointer plus a length, yet it can replace a large number of `const std::string&` parameters and eliminate unnecessary string allocations. Used carelessly, however, it introduces fatal problems such as dangling references and missing null termination. In this chapter we take `string_view` apart from first principles to performance to pitfalls, until every facet of it is thoroughly clear.

## Chapter Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-string-view-internals">string_view Internals: A Non-Owning String View</ChapterLink>
  <ChapterLink href="02-string-view-performance">string_view Performance Analysis</ChapterLink>
  <ChapterLink href="03-string-view-pitfalls">string_view Pitfalls and Best Practices</ChapterLink>
</ChapterNav>
