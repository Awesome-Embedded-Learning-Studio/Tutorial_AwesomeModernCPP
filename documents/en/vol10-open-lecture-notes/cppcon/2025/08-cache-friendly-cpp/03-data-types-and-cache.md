---
title: "Data Types Are Part of the Cache Equation Too: Smaller Isn't Always Faster"
description: "CppCon 2025 notes — Cache-Friendly, part three. A 50-million-element sum measured on this machine: int16_t comes in fastest, and uint8_t is actually the slowest. You save space on the cache side but lose throughput on the execution-unit side, and the two cancel each other out"
chapter: 8
order: 3
conference: cppcon
conference_year: 2025
talk_title: 'Cache-Friendly C++'
speaker: Jonathan Müller
cpp_standard: [20]
difficulty: intermediate
platform: host
reading_time_minutes: 8
tags:
  - cpp-modern
  - host
  - intermediate
  - 优化
related:
  - "Memory Access Really Is 100x Slower: The Cache Hierarchy and Cache Lines"
  - "Writing Cache-Friendly Code: Layout, Alignment, and Decisions"
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/08-cache-friendly-cpp/03-data-types-and-cache.md
  source_hash: e38f0b4809995e4dd5b94c9ba28f092ac2798265677cb6fb42fd2d4331f7aa39
  translated_at: '2026-09-26T16:38:13+00:00'
  engine: anthropic
  token_count: 4500
---

# Data Types Are Part of the Cache Equation Too: Smaller Isn't Always Faster

In the [previous two articles](01-complexity-is-not-everything.md) we built up one intuition: cache space is extremely precious, and the more compact your data — the more of it you can pack into cache — the better the performance. Following that intuition, it's easy to derive a conclusion that looks airtight: **use smaller data types**. `int` is 4 bytes, `short` is 2 bytes, `uint8_t` is 1 byte — the number of elements that fit into a cache line doubles and doubles again, the cache hit rate soars, and of course the program gets faster.

The logic is perfectly self-consistent. Then you run the experiment, and reality slaps you in the face. This article is here to take that counterintuitive result apart.

## A Seemingly Obvious Optimization

The scenario is thoroughly everyday: an array storing the ages of tens of millions of people, iterated over to compute the average age. An age's value range is 0 to 255, while `int` represents values up to 2 billion — clearly overkill. Intuitively, storing ages as `uint8_t` cuts memory by 4x and multiplies cache-line utilization by 4x, so it must be faster.

Let's compare all three storage layouts — `int32_t`, `int16_t`, and `uint8_t` — accumulating 50 million elements, with 7 runs each on this machine (GCC 16.1.1, `-O2`), taking the median:

```text
N=50000000, -O2 (with compiler auto-vectorization)
  int32_t  : 10301.1 us  (array 190.7 MB, 16 per 64B cache line)
  int16_t  : 10263.2 us  (array 95.4 MB,  32 per 64B cache line)
  uint8_t  : 11270.6 us  (array 47.7 MB,  64 per 64B cache line)
```

Stare at that result for a moment. By the cache-line-utilization logic, `uint8_t` packs 64 elements per line and should be fastest; `int32_t` fits only 16 per line and should be slowest. But what actually comes out is: `int16_t` is the fastest, and `uint8_t` is **actually the slowest** — about 10% slower than `int16_t`. Cache space shrank 4x, yet speed went down instead of up.

## Won on the Cache Side, Lost on the Execution-Unit Side

The problem is this: performance is not a single dimension. Cache hit rate is a bottleneck, but not the only one. The CPU's internal execution units have different throughputs for integers of different widths.

On x86-64, adding `int32_t` values is the most "native" operation — registers are 32/64-bit to begin with, a single `add` instruction does the job, and throughput is the highest. Arithmetic on `int16_t` and `uint8_t` can be done too, but narrow types often need extra **sign-extension** or **zero-extension** instructions to widen the result back to full register width, and x86's instruction encoding for 8-bit arithmetic carries some historical baggage, so its throughput really is a bit lower even than 16-bit.

So the picture becomes: `uint8_t` saves 4x space on the cache side (fewer cache misses), but on the execution-unit side, every addition gets worse throughput (slower arithmetic). Weighing the two against each other, at this data size (50 million elements, the 47 MB array still exceeding L3), the cache win failed to outweigh the arithmetic loss, and the total ended up slower than `int16_t`. `int16_t` sits at the sweet spot: cheaper on cache than `int32`, without the arithmetic-throughput regression that `uint8` suffers, so it comes out fastest.

## Turn Off Vectorization and Check Again

To rule out interference from the compiler's auto-vectorization, let's turn vectorization off (`-fno-tree-vectorize`) and look at pure scalar performance:

```text
-O2 -fno-tree-vectorize (pure scalar)
  int32_t  : 11537.6 us   ← fastest
  int16_t  : 12988.0 us
  uint8_t  : 12450.8 us
```

Under pure scalar code, `int32_t` is now the fastest — because scalar `add` is the most direct for 32-bit, while narrow types need extra instructions to handle. This further confirms the explanation above: the arithmetic-throughput disadvantage of narrow types is real. It's just that in the vectorized version, the compiler unrolled the loop and partially canceled out the difference, so the gap there was less dramatic — but the direction never changed.

::: warning Optimization is never single-dimensional
This is the one lesson from this cache series most worth remembering: **improving cache behavior can hand the gains right back through instruction-execution efficiency**. You squeeze the data tighter, the cache hit rate goes up, but if that same change drags down the throughput of the CPU's arithmetic units, the net effect can be zero or even negative. So measure, measure, measure — never decide based on the "cache-friendly must mean fast" intuition.
:::

## So How Do You Actually Choose a Data Type

Don't swing to the other extreme and conclude that "shrinking types is useless." The experiment above only shows that "blindly shrinking to the smallest type isn't necessarily fastest" — not that "shrinking types is useless." The right way to think about it is scenario by scenario:

**For fields that are pure storage and barely touch arithmetic, shrink without hesitation.** The most typical case is enumerations. C++ gives enumerations a default underlying type of `int` (4 bytes), yet most enums have only a handful of values — spending 4 bytes storing a 0/1/2 is pure waste. And you basically never do arithmetic on enum values, so there's no arithmetic-throughput regression to worry about; it's pure space savings:

```cpp
enum class Color : uint8_t { Red, Green, Blue };       // 1 byte
enum class Month : uint8_t { Jan = 1, Feb, Mar, /*...*/ Dec };
```

When a struct has several enum fields, the space saved by this one trick stacks up to something considerable: the array gets more compact, and the cache hit rate rises for real.

**For fields that participate in arithmetic and whose data isn't big enough to blow out the cache, don't shrink blindly.** Computing an average age, accumulating statistics — for that kind of work, `int32_t` is usually the sweet spot, or at most `int16_t`. Here, arithmetic throughput outweighs cache space.

**Only when data volume is huge and arithmetic is very light does shrinking become a sure win.** For example, if you're just walking a pile of flag bits counting how many are true — almost no arithmetic at all — then the cache advantage of `uint8_t` or `int8_t` gets to fully shine, and the execution-unit regression barely matters.

But every one of these judgments ultimately comes back to a single rule: **measure under your own target data scale and access pattern**. The numbers from this machine, the rankings we got here — they only give you a direction.

## An Easy Trap: Don't Store Numbers in `char`

While we're here, a related language trap. If you decide to use a 1-byte type for numeric values, **don't use `char`**. In the C++ standard, `char`, `signed char`, and `unsigned char` are **three distinct types**, and the signedness of `char` is implementation-defined (usually signed on x86, often unsigned on ARM). Write `char x = 200;` and the behavior can differ from platform to platform — a landmine for cross-platform compatibility.

For numeric scenarios, write `int8_t` or `uint8_t` explicitly (they are aliases of `signed char` and `unsigned char` respectively, but their semantics are unambiguous), and leave `char` for characters. `signed int` and `int` are the same thing; `char` is the lone exception — that's historical baggage, and remembering the conclusion is enough.

As for `char8_t` (the UTF-8 character type introduced in C++20): its underlying type is `unsigned char`, but it stands alone in the type system, and it has one peculiar side effect — it is not on the strict aliasing rule's "universal alias" exception list, so compilers are more "at ease" with it and can sometimes optimize more aggressively. Jonathan showed a case in the talk where `char8_t` was more than twice as fast as `char`. But that phenomenon depends on compiler version and context; we couldn't reproduce it reliably on this machine, so it gets a mention here only as a deep-cut easter egg showing "type choice affects optimization" — switching numeric types to `char8_t` over this is not recommended: the semantics would be wrong, and readability suffers.

## The Boundary of This Layer

In this article we've seen that choosing a data type is not merely about "saving memory" — it's a trade-off between cache hit rate and arithmetic throughput. Shrinking a type scores points on the cache side and can lose points on the execution-unit side; only measurement tells you the net effect.

The next article gathers the scattered conclusions from the earlier articles into a deployable engineering method: how to design data layout, how to handle hot/cold data separation, when to align by hand, and — given a concrete scenario — how to make cache-friendly decisions step by step.

[Next: Writing Cache-Friendly Code →](04-writing-cache-friendly-code.md)
