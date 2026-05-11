# OnceCallback: Callback Design Lessons from Chromium

This directory systematically covers the design of modern C++ callback systems by implementing a Chromium-style `OnceCallback` component. The content is divided into two learning paths:

## Complete Beginner Tutorial (full/)

Tailored for readers with no prior experience, starting with a review of fundamental C++ features and gradually guiding them to a complete component implementation.

**Prerequisites (7 articles):**

- [OnceCallback Prerequisites Quick Reference: C++11/14/17 Core Features Review](full/pre-00-once-callback-cpp-basics-review.md)
- [OnceCallback Prerequisites (Part 1): Function Types and Template Partial Specialization](full/pre-01-once-callback-function-type-and-specialization.md)
- [OnceCallback Prerequisites (Part 2): std::invoke and the Uniform Calling Convention](full/pre-02-once-callback-invoke-and-callable.md)
- [OnceCallback Prerequisites (Part 3): Advanced Lambda Features](full/pre-03-once-callback-lambda-advanced.md)
- [OnceCallback Prerequisites (Part 4): Concepts and requires Constraints](full/pre-04-once-callback-concepts-and-requires.md)
- [OnceCallback Prerequisites (Part 5): std::move_only_function (C++23)](full/pre-05-once-callback-move-only-function.md)
- [OnceCallback Prerequisites (Part 6): Deducing this (C++23)](full/pre-06-once-callback-deducing-this.md)

**Hands-on Practice (6 articles):**

- [OnceCallback in Practice (Part 1): Motivation and API Design](full/01-1-once-callback-motivation-and-api-design.md)
- [OnceCallback in Practice (Part 2): Building the Core Skeleton](full/01-2-once-callback-core-skeleton.md)
- [OnceCallback in Practice (Part 3): Implementing bind_once](full/01-3-once-callback-bind-once.md)
- [OnceCallback in Practice (Part 4): Cancellation Token Design](full/01-4-once-callback-cancellation-token.md)
- [OnceCallback in Practice (Part 5): then Chaining](full/01-5-once-callback-then-chaining.md)
- [OnceCallback in Practice (Part 6): Testing and Performance Comparison](full/01-6-once-callback-testing-and-perf.md)

## Advanced Design Guide (hands_on/)

Tailored for readers experienced with C++ templates, providing a quick walkthrough of the design motivation, implementation strategies, and test verification.

- [once_callback Design Guide (Part 1): Motivation and API Design](hands_on/01-once-callback-design.md)
- [once_callback Design Guide (Part 2): Step-by-Step Implementation](hands_on/02-once-callback-implementation.md)
- [once_callback Design Guide (Part 3): Testing Strategy and Performance Comparison](hands_on/03-once-callback-testing.md)
