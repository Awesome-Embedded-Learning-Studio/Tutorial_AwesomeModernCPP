# Complete Beginner Tutorial

This directory contains a complete beginner tutorial for the OnceCallback component, consisting of 13 articles that cover the full learning path from a C++ fundamentals review to component implementation and testing.

## Prerequisites

First, master the core C++ features required by OnceCallback:

<ChapterNav variant="sub">
  <ChapterLink href="pre-00-once-callback-cpp-basics-review">OnceCallback Prerequisites Quick Reference: C++11/14/17 Core Features Review</ChapterLink>
  <ChapterLink href="pre-01-once-callback-function-type-and-specialization">OnceCallback Prerequisites (Part 1): Function Types and Template Partial Specialization</ChapterLink>
  <ChapterLink href="pre-02-once-callback-invoke-and-callable">OnceCallback Prerequisites (Part 2): std::invoke and the Uniform Calling Convention</ChapterLink>
  <ChapterLink href="pre-03-once-callback-lambda-advanced">OnceCallback Prerequisites (Part 3): Advanced Lambda Features</ChapterLink>
  <ChapterLink href="pre-04-once-callback-concepts-and-requires">OnceCallback Prerequisites (Part 4): Concepts and requires Constraints</ChapterLink>
  <ChapterLink href="pre-05-once-callback-move-only-function">OnceCallback Prerequisites (Part 5): std::move_only_function (C++23)</ChapterLink>
  <ChapterLink href="pre-06-once-callback-deducing-this">OnceCallback Prerequisites (Part 6): Deducing this (C++23)</ChapterLink>
</ChapterNav>

## Hands-on Practice

After completing the prerequisites, we start implementing OnceCallback:

<ChapterNav variant="sub">
  <ChapterLink href="01-1-once-callback-motivation-and-api-design">OnceCallback in Practice (Part 1): Motivation and API Design</ChapterLink>
  <ChapterLink href="01-2-once-callback-core-skeleton">OnceCallback in Practice (Part 2): Building the Core Skeleton</ChapterLink>
  <ChapterLink href="01-3-once-callback-bind-once">OnceCallback in Practice (Part 3): Implementing bind_once</ChapterLink>
  <ChapterLink href="01-4-once-callback-cancellation-token">OnceCallback in Practice (Part 4): Cancellation Token Design</ChapterLink>
  <ChapterLink href="01-5-once-callback-then-chaining">OnceCallback in Practice (Part 5): then Chaining Composition</ChapterLink>
  <ChapterLink href="01-6-once-callback-testing-and-perf">OnceCallback in Practice (Part 6): Testing and Performance Comparison</ChapterLink>
</ChapterNav>

## Companion Code

The standalone C++ example code from the prerequisite chapters has been extracted into compilable minimal projects, located at:

```
code/volumn_codes/vol9/full_tutorial_codes/chrome_design/
```

| Example | Topic | Source Article | Minimum C++ Standard |
|---------|-------|----------------|----------------------|
| `01_move_semantics.cpp` | Move semantics, perfect forwarding, variadic templates | pre-00 | C++17 |
| `02_smart_pointers.cpp` | unique_ptr, shared_ptr | pre-00 | C++17 |
| `03_atomic_memory_order.cpp` | atomic, memory_order, enum class | pre-00 | C++17 |
| `04_lambda_basics.cpp` | Capture modes, generic lambda, [[nodiscard]] | pre-00 | C++17 |
| `05_lambda_advanced.cpp` | mutable lambda, init capture, C++17/C++20 bind | pre-03 | C++20 |
| `06_type_traits.cpp` | type traits, if constexpr, decltype(auto), ref-qualifier | pre-00 | C++17 |
| `07_function_type_specialization.cpp` | Function types, FuncTraits, primary template + partial specialization | pre-01 | C++17 |
| `08_invoke.cpp` | std::invoke, std::invoke_result_t | pre-02 | C++17 |
| `09_concepts_requires.cpp` | concept, requires, not_the_same_t, template constructor hijacking | pre-04 | C++20 |
| `10_move_only_function.cpp` | std::move_only_function construction/move/null check/SBO | pre-05 | C++23 |
| `11_deducing_this.cpp` | deducing this deduction rules, lvalue interception | pre-06 | C++23 |

How to build:

```bash
cd code/volumn_codes/vol9/full_tutorial_codes/chrome_design
mkdir build && cd build
cmake ..
make -j$(nproc)
```
