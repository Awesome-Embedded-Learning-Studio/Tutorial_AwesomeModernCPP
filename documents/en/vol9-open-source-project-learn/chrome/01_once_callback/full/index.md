---
translation:
  source: documents/vol9-open-source-project-learn/chrome/01_once_callback/full/index.md
  source_hash: f4ab993cf84b44ddc2d5465090d17da281cd969d08a359cfb37fe11e5d3828b3
  translated_at: '2026-09-26T00:43:03+00:00'
  engine: anthropic
  token_count: 850
---
# Complete Beginner Tutorial

This directory contains the complete beginner tutorial for the OnceCallback component — 13 articles in total, covering the full learning path from a review of core C++ features to implementing and testing the component.

## Prerequisites

First, master the core C++ features that OnceCallback relies on:

<ChapterNav variant="sub">
  <ChapterLink href="pre-00-once-callback-cpp-basics-review">OnceCallback prerequisite cheat sheet: a review of C++11/14/17 core features</ChapterLink>
  <ChapterLink href="pre-01-once-callback-function-type-and-specialization">OnceCallback prerequisite (I): function types and template partial specialization</ChapterLink>
  <ChapterLink href="pre-02-once-callback-invoke-and-callable">OnceCallback prerequisite (II): std::invoke and the uniform calling protocol</ChapterLink>
  <ChapterLink href="pre-03-once-callback-lambda-advanced">OnceCallback prerequisite (III): advanced lambda features</ChapterLink>
  <ChapterLink href="pre-04-once-callback-concepts-and-requires">OnceCallback prerequisite (IV): Concepts and requires constraints</ChapterLink>
  <ChapterLink href="pre-05-once-callback-move-only-function">OnceCallback prerequisite (V): std::move_only_function (C++23)</ChapterLink>
  <ChapterLink href="pre-06-once-callback-deducing-this">OnceCallback prerequisite (VI): Deducing this (C++23)</ChapterLink>
</ChapterNav>

## Hands-on Practice

With the prerequisites covered, we move on to implementing OnceCallback:

<ChapterNav variant="sub">
  <ChapterLink href="01-1-once-callback-motivation-and-api-design">OnceCallback hands-on (I): motivation and API design</ChapterLink>
  <ChapterLink href="01-2-once-callback-core-skeleton">OnceCallback hands-on (II): building the core skeleton</ChapterLink>
  <ChapterLink href="01-3-once-callback-bind-once">OnceCallback hands-on (III): implementing bind_once</ChapterLink>
  <ChapterLink href="01-4-once-callback-cancellation-token">OnceCallback hands-on (IV): designing the cancellation token</ChapterLink>
  <ChapterLink href="01-5-once-callback-then-chaining">OnceCallback hands-on (V): chaining with then</ChapterLink>
  <ChapterLink href="01-6-once-callback-testing-and-perf">OnceCallback hands-on (VI): tests and performance comparison</ChapterLink>
</ChapterNav>

## Companion Code

The standalone C++ example code covered in the prerequisite chapters has been distilled into a compilable minimal project, located at:

```text
code/volumn_codes/vol9/full_tutorial_codes/chrome_design/
```

| Example | Topic | Source Article | Minimum C++ Standard |
|------|------|----------|-------------|
| `01_move_semantics.cpp` | Move semantics, perfect forwarding, variadic templates | pre-00 | C++17 |
| `02_smart_pointers.cpp` | unique_ptr, shared_ptr | pre-00 | C++17 |
| `03_atomic_memory_order.cpp` | atomic, memory_order, enum class | pre-00 | C++17 |
| `04_lambda_basics.cpp` | Capture modes, generic lambda, [[nodiscard]] | pre-00 | C++17 |
| `05_lambda_advanced.cpp` | mutable lambda, init capture, C++17/C++20 bind | pre-03 | C++20 |
| `06_type_traits.cpp` | type traits, if constexpr, decltype(auto), ref-qualifier | pre-00 | C++17 |
| `07_function_type_specialization.cpp` | Function types, FuncTraits, primary template + partial specialization | pre-01 | C++17 |
| `08_invoke.cpp` | std::invoke, std::invoke_result_t | pre-02 | C++17 |
| `09_concepts_requires.cpp` | concept, requires, not_the_same_t, template constructor hijacking | pre-04 | C++20 |
| `10_move_only_function.cpp` | std::move_only_function construction/move/empty check/SBO | pre-05 | C++23 |
| `11_deducing_this.cpp` | deducing this deduction rules, lvalue interception | pre-06 | C++23 |

Build instructions:

```bash
cd code/volumn_codes/vol9/full_tutorial_codes/chrome_design
mkdir build && cd build
cmake ..
make -j$(nproc)
```
