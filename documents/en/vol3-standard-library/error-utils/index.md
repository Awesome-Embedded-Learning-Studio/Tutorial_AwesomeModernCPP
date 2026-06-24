---
title: Error Handling and Tools
description: optional、variant、any、expected、functional、error_code、stacktrace、source_location
sidebar_order: 60
translation:
  source: documents/vol3-standard-library/error-utils/index.md
  source_hash: a7746d26d03c8b07586738540462fa24db2aff33dd81d1f7efab5d42c2154a7d
  translated_at: '2026-06-24T00:41:48.623751+00:00'
  engine: anthropic
  token_count: 231
---
# Error Handling and Utilities

Error handling and runtime utilities: turning "maybe nothing," "closed polymorphism," and "value or error" into types with `optional`/`variant`/`expected`, the cost of function objects and `std::function`, the error code system `error_code`, C++23's standardized stack capture with `stacktrace`, and compile-time code location via `source_location`.

<ChapterNav variant="sub">
  <ChapterLink href="61-optional">optional: Making "Maybe Nothing" a Type</ChapterLink>
  <ChapterLink href="62-variant">variant: Type-Safe Unions and visit</ChapterLink>
  <ChapterLink href="63-any">any: Holding Any Type</ChapterLink>
  <ChapterLink href="64-expected">expected: Value or Error (C++23)</ChapterLink>
  <ChapterLink href="65-functional">functional: The Cost of std::function</ChapterLink>
  <ChapterLink href="66-error-code">error_code: Error Code Systems and Custom category</ChapterLink>
  <ChapterLink href="67-stacktrace">stacktrace: C++23 Stack Capture</ChapterLink>
  <ChapterLink href="68-source-location">source_location: Compile-Time Code Location</ChapterLink>
</ChapterNav>
