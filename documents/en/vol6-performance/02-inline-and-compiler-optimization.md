---
chapter: 2
cpp_standard:
- 11
- 14
- 17
- 20
description: Explore how inline functions work
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'Chapter 1: 构建工具链'
reading_time_minutes: 4
tags:
- cpp-modern
- host
- intermediate
title: Inline Functions and Compiler Optimization
translation:
  source: documents/vol6-performance/02-inline-and-compiler-optimization.md
  source_hash: 3f6541902c3cba588da0ebd8adc6d5680fdb6824410e0d69b8f4e9203b1bebfd
  translated_at: '2026-06-15T09:27:41.696941+00:00'
  engine: anthropic
  token_count: 529
---
# Modern Embedded C++ Tutorial — Inline Functions and Compiler Optimization

In embedded development, the `inline` keyword is something almost every engineer uses. It looks simple and direct, even carrying a hint of "performance guarantee": if a function is short, called frequently, or timing-sensitive, just `inline` it; it seems like the natural thing to do.

While in the past, the `inline` keyword indeed served this purpose, in reality, with modern C++ and today's highly advanced compiler optimizations, **`inline` is not a performance optimization button; in fact, it often does nothing at all.**

## `inline` Was Never Born to Be "Fast"

From a language perspective, the core purpose of `inline` is actually very restrained. Modern C++ standards do not promise that "if you write `inline`, the compiler will definitely expand the function body." The only thing it truly guarantees is this: **it allows the function to have definitions in multiple translation units without violating the ODR.**

This is why small functions in header files, template functions, and traits utility functions naturally possess an `inline` "character." It solves a "linkage problem," not a "performance problem." As for whether inlining actually happens, that is entirely the compiler's discretion. In the face of modern compilers, `inline` is more like a suggestion—"I think you might consider expanding this"—rather than a command.

------

## So, Are Function Calls Really Slow in Embedded Systems?

Much of the intuition regarding `inline` stems from a fear of the cost of function calls. On architectures like Cortex-M, a function call indeed implies a jump, saving LR (Link Register), parameter passing, and restoring the return path. If you stare at the assembly line by line, it's easy to conclude: this doesn't look cheap.

The problem is, **does this cost actually fall on your performance bottleneck path?**

In real-world embedded engineering, the vast majority of time consumption in functions isn't in the "call itself," but in peripheral access, bus waiting, Flash reads, Cache misses, or even interrupt preemption. Worrying about whether to inline a GPIO read function is often optimizing at a completely unimportant level.

More critically, with optimizations enabled (even just `-O2`), short functions with no side effects and clear semantics **will almost certainly be auto-inlined by the compiler, even if you don't write `inline`.**

When deciding whether to inline a function today, compilers comprehensively consider function body size, the number of call sites, register pressure, instruction Cache behavior, and even analyze call relationships across files when LTO (Link Time Optimization) is enabled. The information it holds far exceeds the little context you see when writing code. This is why you often see this situation: you explicitly wrote `inline`, but upon disassembly, the function still exists; whereas when you wrote nothing, the function was silently expanded.

------

## `inline` That Expands Into Calls Isn't Very Safe

If the biggest risk of `inline` in PC development is "ineffectiveness," then in embedded systems, its real risk is often **code bloat**.

The essence of inlining is copying. A small function that is called frequently, if expanded in multiple locations, will have its instructions实实在在地 copied multiple times. On MCUs with tight Flash resources, this copying cannot be ignored. A more subtle point is that larger code not only consumes Flash but also affects the locality of the instruction Cache. Even on cores with I-Cache, excessive inlining can lead to more Cache misses, ultimately manifesting as a performance drop, not an improvement.

------

## So, When Does `inline` Really Have Value?

In practice, scenarios where `inline` truly demonstrates value are often not "to save a function call," but rather to **eliminate the cost of abstraction boundaries**.

Examples include template functions, type-safe register access wrappers, and compile-time calculations involving `constexpr`. In these places, `inline` allows you to write highly expressive C++ code, while at the generated assembly level, it is almost indistinguishable from hand-written C.

This is the most charming aspect of modern C++ in the embedded field: **abstraction is not a burden, but a semantic tool that can be completely optimized away.**

In interrupt service routines (ISRs) or extreme hot paths, `inline` may also be reasonable, but the prerequisite is always just one: you have actually looked at the assembly and confirmed that it solves a real problem.

## Run Online

Online comparison of C-style function calls and C++ template zero-overhead abstractions, observing compiler inline optimization effects:

<OnlineCompilerDemo
  title="Inline and Compiler Optimization"
  source-path="code/examples/vol6/12_inline_optimization.cpp"
  description="Compare the inline optimization effects of C-style functions and C++ templates, and observe constexpr compile-time calculation"
  allow-run
  allow-x86-asm
/>
