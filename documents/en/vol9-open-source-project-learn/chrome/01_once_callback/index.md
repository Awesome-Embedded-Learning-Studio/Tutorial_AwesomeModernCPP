# OnceCallback: Callback Design Lessons from Chromium

This directory systematically covers the design of modern C++ callback systems by implementing a Chromium-style `OnceCallback` component. The content is divided into two learning paths:

## Complete Beginner Tutorial (full/)

Tailored for readers with no prior experience, starting with a review of fundamental C++ features and gradually guiding them to a complete component implementation.

**Prerequisites (7 chapters):**

<ChapterNav variant="sub">
  <ChapterLink href="full/pre-00-once-callback-cpp-basics-review">OnceCallback Prerequisites Quick Reference: Review of C++11/14/17 Core Features</ChapterLink>
  <ChapterLink href="full/pre-01-once-callback-function-type-and-specialization">OnceCallback Prerequisites (Part 1): Function Types and Template Partial Specialization</ChapterLink>
  <ChapterLink href="full/pre-02-once-callback-invoke-and-callable">OnceCallback Prerequisites (Part 2): std::invoke and the Unified Callable Protocol</ChapterLink>
  <ChapterLink href="full/pre-03-once-callback-lambda-advanced">OnceCallback Prerequisites (Part 3): Advanced Lambda Features</ChapterLink>
  <ChapterLink href="full/pre-04-once-callback-concepts-and-requires">OnceCallback Prerequisites (Part 4): Concepts and requires Constraints</ChapterLink>
  <ChapterLink href="full/pre-05-once-callback-move-only-function">OnceCallback Prerequisites (Part 5): std::move_only_function (C++23)</ChapterLink>
  <ChapterLink href="full/pre-06-once-callback-deducing-this">OnceCallback Prerequisites (Part 6): Deducing this (C++23)</ChapterLink>
</ChapterNav>

**Hands-on Practice (6 chapters):**

<ChapterNav variant="sub">
  <ChapterLink href="full/01-1-once-callback-motivation-and-api-design">OnceCallback in Practice (Part 1): Motivation and API Design</ChapterLink>
  <ChapterLink href="full/01-2-once-callback-core-skeleton">OnceCallback in Practice (Part 2): Building the Core Skeleton</ChapterLink>
  <ChapterLink href="full/01-3-once-callback-bind-once">OnceCallback in Practice (Part 3): Implementing bind_once</ChapterLink>
  <ChapterLink href="full/01-4-once-callback-cancellation-token">OnceCallback in Practice (Part 4): Cancellation Token Design</ChapterLink>
  <ChapterLink href="full/01-5-once-callback-then-chaining">OnceCallback in Practice (Part 5): then Chaining Composition</ChapterLink>
  <ChapterLink href="full/01-6-once-callback-testing-and-perf">OnceCallback in Practice (Part 6): Testing and Performance Comparison</ChapterLink>
</ChapterNav>

## Advanced Design Guide (hands_on/)

Tailored for readers experienced with C++ templates, providing a quick walkthrough of the design motivation, implementation strategies, and test verification.

<ChapterNav variant="sub">
  <ChapterLink href="hands_on/01-once-callback-design">once_callback Design Guide (Part 1): Motivation and API Design</ChapterLink>
  <ChapterLink href="hands_on/02-once-callback-implementation">once_callback Design Guide (Part 2): Step-by-Step Implementation</ChapterLink>
  <ChapterLink href="hands_on/03-once-callback-testing">once_callback Design Guide (Part 3): Testing Strategies and Performance Comparison</ChapterLink>
</ChapterNav>
