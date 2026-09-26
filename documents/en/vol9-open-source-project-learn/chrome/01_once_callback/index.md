---
translation:
  source: documents/vol9-open-source-project-learn/chrome/01_once_callback/index.md
  source_hash: a7258f5ec4566f0fb170d347590fa2bf1adda463986b2e3302c2bb924215aa9d
  translated_at: '2026-09-26T01:10:26+00:00'
  engine: anthropic
  token_count: 700
---
# OnceCallback: callback design lessons from Chromium

This directory systematically teaches the design of a modern C++ callback system by implementing a Chromium-style `OnceCallback` component. The content is split into two learning paths:

## Complete beginner tutorial (full/)

Aimed at readers starting from zero, it opens with a review of fundamental C++ features and gradually leads up to a complete component implementation.

**Prerequisites (7 articles):**

<ChapterNav variant="sub">
  <ChapterLink href="full/pre-00-once-callback-cpp-basics-review">OnceCallback prerequisite cheat sheet: a review of C++11/14/17 core features</ChapterLink>
  <ChapterLink href="full/pre-01-once-callback-function-type-and-specialization">OnceCallback prerequisite (I): function types and template partial specialization</ChapterLink>
  <ChapterLink href="full/pre-02-once-callback-invoke-and-callable">OnceCallback prerequisite (II): std::invoke and the uniform calling protocol</ChapterLink>
  <ChapterLink href="full/pre-03-once-callback-lambda-advanced">OnceCallback prerequisite (III): advanced lambda features</ChapterLink>
  <ChapterLink href="full/pre-04-once-callback-concepts-and-requires">OnceCallback prerequisite (IV): Concepts and requires constraints</ChapterLink>
  <ChapterLink href="full/pre-05-once-callback-move-only-function">OnceCallback prerequisite (V): std::move_only_function (C++23)</ChapterLink>
  <ChapterLink href="full/pre-06-once-callback-deducing-this">OnceCallback prerequisite (VI): Deducing this (C++23)</ChapterLink>
</ChapterNav>

**Hands-on practice (6 articles):**

<ChapterNav variant="sub">
  <ChapterLink href="full/01-1-once-callback-motivation-and-api-design">OnceCallback hands-on (I): motivation and API design</ChapterLink>
  <ChapterLink href="full/01-2-once-callback-core-skeleton">OnceCallback hands-on (II): building the core skeleton</ChapterLink>
  <ChapterLink href="full/01-3-once-callback-bind-once">OnceCallback hands-on (III): implementing bind_once</ChapterLink>
  <ChapterLink href="full/01-4-once-callback-cancellation-token">OnceCallback hands-on (IV): designing the cancellation token</ChapterLink>
  <ChapterLink href="full/01-5-once-callback-then-chaining">OnceCallback hands-on (V): chaining with then</ChapterLink>
  <ChapterLink href="full/01-6-once-callback-testing-and-perf">OnceCallback hands-on (VI): tests and performance comparison</ChapterLink>
</ChapterNav>

## Advanced design guide (hands_on/)

Aimed at readers who already have C++ template experience, it is a fast-paced walkthrough of the design motivation, the implementation strategy, and the test verification.

<ChapterNav variant="sub">
  <ChapterLink href="hands_on/01-once-callback-design">once_callback Design Guide (I): motivation and API design</ChapterLink>
  <ChapterLink href="hands_on/02-once-callback-implementation">once_callback Design Guide (II): step-by-step implementation</ChapterLink>
  <ChapterLink href="hands_on/03-once-callback-testing">once_callback Design Guide (III): test strategy and performance comparison</ChapterLink>
</ChapterNav>
