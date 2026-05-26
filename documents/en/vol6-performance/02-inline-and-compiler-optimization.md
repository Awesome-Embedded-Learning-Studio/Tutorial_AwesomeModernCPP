---
chapter: 2
cpp_standard:
- 11
- 14
- 17
- 20
description: Exploring how inline functions work
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
title: Inlining and Compiler Optimization
translation:
  source: documents/vol6-performance/02-inline-and-compiler-optimization.md
  source_hash: 06a9317198c969abccdf3e2276bbeee56856b754f7c4c957a30a250c86969747
  translated_at: '2026-05-26T11:49:35.014843+00:00'
  engine: anthropic
  token_count: 530
---
# Modern C++ for Embedded Systems—Inline Functions and Compiler Optimization

In embedded development, `inline` is a keyword almost every engineer uses. It seems simple and direct, even carrying a hint of "performance guarantee": if a function is short, called frequently, and timing-sensitive, just `inline` it—seems like a no-brainer.

True, in the past, the `inline` keyword did serve this purpose. But in reality, in the era of modern C++ and with today's highly advanced compiler optimizations, **`inline` is not a performance optimization button, and it often does nothing at all.**

## Inline was never born to be "fast"

At the language level, the core purpose of `inline` is actually quite restrained. The modern C++ standard does not promise that "if you write `inline`, the compiler will definitely expand the function body." The only thing it truly guarantees is this: **it allows the function to have a definition in multiple translation units without violating the ODR.**

This is why a large number of small functions in header files, template functions, and `traits` utility functions naturally carry an `inline` vibe. It solves a "linkage-level problem," not a "performance problem." As for whether inlining actually occurs, that is entirely at the compiler's discretion. In the face of modern compilers, `inline` is more like a suggestion—"I think you might consider expanding this"—rather than a command.

------

## So, are function calls really slow in embedded?

Much of the intuition surrounding `inline` stems from a fear of function call overhead. On architectures like Cortex-M, a function call does indeed mean a jump, saving LR, parameter passing, and restoring the return path. If you stare at the assembly line by line, it's easy to conclude: this doesn't look cheap.

The problem is whether this cost **actually lands on your performance bottleneck path.**

In real-world embedded engineering, the time consumption of many functions doesn't lie in the "call itself" at all, but rather in peripheral access, bus waits, Flash reads, cache misses, or even interrupt preemption. Agonizing over whether to `inline` a GPIO read function is often optimizing at a completely irrelevant level.

More critically, after enabling optimizations (even just `-O2`), short, side-effect-free functions with clear semantics **will almost certainly be automatically inlined by the compiler, even if you don't write `inline`.**

When deciding whether to inline a function today, compilers consider a combination of factors: function body size, number of call sites, register pressure, instruction cache behavior, and even cross-file call graph analysis when LTO is enabled. The information it has far exceeds the little context you see when writing the code. This is why you often see this situation: you explicitly write `inline`, but the disassembly reveals the function still exists; and when you write nothing at all, the function is silently expanded.

------

## Inline that expands into a call isn't very safe

If the biggest risk of `inline` in PC development is "doing nothing," then in embedded systems, its real risk is often **code bloat**.

The essence of inlining is copying. A frequently called small function, if expanded in multiple places, will have its instructions physically duplicated. On MCUs with tight Flash resources, this duplication cannot be ignored. A more subtle point is that larger code doesn't just consume Flash—it also affects instruction cache locality. Even on cores with an I-Cache, excessive inlining can lead to more cache misses, ultimately manifesting as a performance decrease rather than an improvement.

------

## So, when is inline truly valuable?

In practice, the scenarios where `inline` truly shows its value are often not "to save one function call," but to **eliminate the cost of abstraction boundaries**.

Examples include template functions, type-safe register access wrappers, and compile-time calculations involving `constexpr`. The `inline` in these places allows us to write highly expressive C++ code, while at the generated assembly level, it is almost indistinguishable from hand-written C.

This is the most fascinating aspect of modern C++ in the embedded domain: **abstraction is not a burden, but a semantic tool that can be completely optimized away.**

In an interrupt service routine (ISR) or an extreme hot path, `inline` might also be reasonable, but there is only ever one prerequisite: you have actually looked at the assembly and confirmed it solves a real problem.

## Run Online

Online comparison of C-style function calls versus C++ template zero-overhead abstractions, observing compiler inline optimization effects:

<OnlineCompilerDemo
  title="内联与编译器优化"
  source-path="code/examples/vol34567/12_inline_optimization.cpp"
  description="对比 C 风格函数与 C++ 模板的内联优化效果，观察 constexpr 编译期计算"
  allow-run
  allow-x86-asm
/>
