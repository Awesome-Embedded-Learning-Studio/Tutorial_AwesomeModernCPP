# Complete Beginner Tutorial

This directory contains a complete beginner tutorial for the OnceCallback component, consisting of 13 articles that cover the full learning path from reviewing basic C++ features to component implementation and testing.

## Prerequisites

First, master the core C++ features required by OnceCallback:

1. [OnceCallback Prerequisites Quick Reference: C++11/14/17 Core Features Review](pre-00-once-callback-cpp-basics-review.md)
2. [OnceCallback Prerequisites (Part 1): Function Types and Template Partial Specialization](pre-01-once-callback-function-type-and-specialization.md)
3. [OnceCallback Prerequisites (Part 2): std::invoke and the Uniform Calling Convention](pre-02-once-callback-invoke-and-callable.md)
4. [OnceCallback Prerequisites (Part 3): Advanced Lambda Features](pre-03-once-callback-lambda-advanced.md)
5. [OnceCallback Prerequisites (Part 4): Concepts and requires Constraints](pre-04-once-callback-concepts-and-requires.md)
6. [OnceCallback Prerequisites (Part 5): std::move_only_function (C++23)](pre-05-once-callback-move-only-function.md)
7. [OnceCallback Prerequisites (Part 6): Deducing this (C++23)](pre-06-once-callback-deducing-this.md)

## Hands-on Practice

After learning the prerequisites, we start implementing OnceCallback:

1. [OnceCallback in Practice (Part 1): Motivation and API Design](01-1-once-callback-motivation-and-api-design.md)
2. [OnceCallback in Practice (Part 2): Building the Core Skeleton](01-2-once-callback-core-skeleton.md)
3. [OnceCallback in Practice (Part 3): Implementing bind_once](01-3-once-callback-bind-once.md)
4. [OnceCallback in Practice (Part 4): Cancellation Token Design](01-4-once-callback-cancellation-token.md)
5. [OnceCallback in Practice (Part 5): then Chaining Composition](01-5-once-callback-then-chaining.md)
6. [OnceCallback in Practice (Part 6): Testing and Performance Comparison](01-6-once-callback-testing-and-perf.md)

## Accompanying Code

The standalone C++ example code from the prerequisites section has been extracted into compilable minimal projects, located at:

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

Build instructions:

```bash
cd code/volumn_codes/vol9/full_tutorial_codes/chrome_design
mkdir build && cd build
cmake ..
make -j$(nproc)
```
