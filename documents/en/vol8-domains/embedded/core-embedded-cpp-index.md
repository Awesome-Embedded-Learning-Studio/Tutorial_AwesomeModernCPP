---
chapter: 0
difficulty: intermediate
order: 0
platform: stm32f1
reading_time_minutes: 3
tags:
- cpp-modern
- intermediate
- stm32f1
title: Table of Contents
description: ''
translation:
  source: documents/vol8-domains/embedded/core-embedded-cpp-index.md
  source_hash: 7daaa02127b4788db41483c69a7e2b6c46f6460611fc6c1fc81269f846a7eb24
  translated_at: '2026-06-16T04:12:38.688967+00:00'
  engine: anthropic
  token_count: 864
---
# Table of Contents

This is the table of contents for "Modern C++ for Embedded Systems Tutorial". Click on a link to jump directly to the corresponding chapter.

## Chapter 0 - Preface and Fundamentals

- [Preface](../../vol1-fundamentals/00-preface.md)
- [Resource and Real-Time Constraints in Embedded Systems](./01-resource-and-realtime-constraints.md)
- [C Language Crash Course](../../vol1-fundamentals/02-c-language-crash-course.md)
- [C++98 Intro: Namespaces, References, and Scope Resolution](../../vol1-fundamentals/03A-cpp98-namespace-reference.md)
- [C++98 Function Interfaces: Overloading and Default Arguments](../../vol1-fundamentals/03B-cpp98-function-overload-default-args.md)
- [C++98 OOP: Deep Dive into Classes and Objects](../../vol1-fundamentals/03C-cpp98-classes-and-objects.md)
- [C++98 OOP: Inheritance and Polymorphism](../../vol1-fundamentals/03D-cpp98-inheritance-polymorphism.md)
- [C++98 Operator Overloading](../../vol1-fundamentals/03E-cpp98-operator-overloading.md)
- [C++98 Advanced: Type Casting, Dynamic Memory, and Exception Handling](../../vol1-fundamentals/03F-cpp98-casts-memory-exceptions.md)
- [When to Use C++ and Which Features (Trade-offs and Disabled Features)](../../vol1-fundamentals/04-when-to-use-cpp.md)
- [Language Selection Principles: The Real Trade-off Between Performance and Maintainability](../../vol1-fundamentals/05-language-choice-performance-vs-maintainability.md)
- [Does C++ Necessarily Lead to Code Bloat?](../../vol6-performance/06-evaluating-performance-and-size.md)

## Chapter 1 - Build Toolchain

- [Cross-Compilation and a Simple CMake Guide](../../vol7-engineering/01-cross-compilation-and-cmake.md)
- [Common Compiler Options Guide](../../vol7-engineering/02-compiler-options.md)
- [Linker and Linker Scripts](../../vol7-engineering/03-linker-and-linker-scripts.md)

## Chapter 2 - Zero-Overhead Abstractions

- [Zero-Overhead Abstraction](./01-zero-overhead-abstraction.md)
- [Inlining and Compiler Optimization](../../vol6-performance/02-inline-and-compiler-optimization.md)
- [CRTP vs. Runtime Polymorphism, Did You Know?](./04-crtp-vs-runtime-polymorphism.md)

## Chapter 3 - Memory and Object Management

- [Initializer Lists](../../vol3-standard-library/11-initializer-lists.md)
- [Empty Base Optimization (EBO)](../../vol4-advanced/03-empty-base-optimization.md)
- [Object Size and Trivial Types](../../vol3-standard-library/12-object-size-and-trivial-types.md)

## Chapter 4 - Compile-Time Computation

- [if constexpr](../../vol4-advanced/vol3-metaprogramming-cpp20-23/index.md)

## Chapter 5 - Memory Management Strategies

- [Dynamic Allocation Issues](./01-dynamic-allocation-issues.md)
- [Static Storage and Stack Allocation Strategies](./02-static-and-stack-allocation.md)
- [Object Pool Pattern](./03-object-pool-pattern.md)
- [Alternative Strategies When Heap is Disabled or Restricted: Using Placement New](./04-placement-new.md)
- [Fixed Pool Allocation](./05-fixed-pool-allocation.md)

## Chapter 7 - Containers and Data Structures

- [array](../../vol3-standard-library/02-array.md)
- [span](../../vol3-standard-library/08-span.md)
- [Circular Buffer](./03-circular-buffer.md)
- [Intrusive Container Design](./04-intrusive-containers.md)
- [ETL](./05-etl.md)
- [Custom Allocators](../../vol3-standard-library/13-custom-allocators.md)

## Chapter 8 - Type Safety and Utility Types

- [Type-Safe Register Access](./02-type-safe-register-access.md)

## Chapter 10 - Concurrency and Atomic Operations

- [atomic](../../vol5-concurrency/ch03-atomic-memory-model/01-atomic-operations.md)
- [memory_order](../../vol5-concurrency/ch03-atomic-memory-model/02-memory-ordering.md)
- [Lock-Free Data Structure Design](../../vol5-concurrency/ch04-concurrent-data-structures/03-lock-free-basics.md)
- [mutex and RAII Guards](../../vol5-concurrency/ch02-mutex-condition-sync/01-mutex-and-raii-guards.md)
- [Writing Interrupt-Safe Code](./05-interrupt-safe-coding.md)
- [Critical Section Protection Techniques](./05-interrupt-safe-coding.md)

## Chapter 11 - Modern C++ Features Overview

- [Three-Way Comparison Operator](../../vol4-advanced/05-spaceship-operator.md)

## Chapter 12 - Template Fundamentals

- [Template Fundamentals (C++11-14)](../../vol4-advanced/vol1-basics-cpp11-14/index.md)
- [Modern Template Techniques (C++17)](../../vol4-advanced/vol2-modern-cpp17/index.md)
- [Metaprogramming Essentials (C++20-23)](../../vol4-advanced/vol3-metaprogramming-cpp20-23/index.md)
- [Generic Design Patterns in Practice](../../vol4-advanced/vol4-generics-patterns/index.md)
