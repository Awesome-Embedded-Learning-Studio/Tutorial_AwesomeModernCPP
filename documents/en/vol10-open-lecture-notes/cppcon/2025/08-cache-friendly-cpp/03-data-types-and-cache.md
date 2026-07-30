---
title: "Data Types Are a Cache Variable Too: Shrinking the Type Isn't Always Faster"
description: "CppCon 2025 notes — Cache-Friendly, part 3. A local benchmark summing 50 million elements: int16_t is fastest, uint8_t is actually slowest. You save space at the cache end, lose throughput at the execution-unit end, and the two cancel out."
chapter: 8
order: 3
conference: cppcon
conference_year: 2025
talk_title: 'Cache-Friendly C++'
speaker: Jonathan Müller
cpp_standard: [20]
difficulty: intermediate
platform: host
reading_time_minutes: 11
tags:
  - cpp-modern
  - host
  - intermediate
  - 优化
related:
  - "Memory Access Being 100x Slower Is Real: Cache Hierarchy and Cache Lines"
  - "Writing Cache-Friendly Code: Layout, Alignment, and Decisions"
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/08-cache-friendly-cpp/03-data-types-and-cache.md
  source_hash: 6e004a6215323b3cc0592c68b6bf7d47faaea62e5f8326880fb272dfa5d3b0a8
  translated_at: '2026-07-29T00:33:58.679864+00:00'
  engine: anthropic
  token_count: 2315
---

# Data Types Are a Cache Variable Too: Shrinking the Type Isn't Always Faster

In the [first two parts](01-complexity-is-not-everything.md) we built up one intuition: cache space is precious, and the more compact your data — the more of it you can stuff into cache — the better the performance. Following that intuition, it's easy to arrive at a conclusion that looks bulletproof: **use smaller data types**. `int` is 4 bytes, `short` is 2 bytes, `uint8_t` is 1 byte; the number of elements that fit in a cache line doubles and doubles again, cache hit rate soars, and the program is obviously faster.

The logic is perfectly self-consistent. Then you run an experiment and reality slaps you in the face. This part takes that counter-intuitive result apart.

## An Optimization That Looks Obvious

The scenario is everyday: an array storing the ages of tens of millions of people, iterated to compute the average. Ages range from 0 to 255; `int` goes up to 2 billion, clearly overkill. Intuitively, storing ages in `uint8_t` saves 4x the memory, gives 4x the cache-line utilization, and is bound to be faster.

Let's compare three storage schemes — `int32_t`, `int16_t`, `uint8_t` — summing 50 million elements, running 7 rounds each on my machine (GCC 16.1.1, `-O2`) and taking the median:

```text
N=50000000, -O2 (with auto-vectorization)
  int32_t  : 10301.1 us  (array 190.7 MB, 16 per 64B cache line)
  int16_t  : 10263.2 us  (array 95.4 MB,  32 per 64B cache line)
  uint8_t  : 11270.6 us  (array 47.7 MB,  64 per 64B cache line)
```

Stare at that result. By the "cache-line utilization" logic, `uint8_t` packs 64 per line and should be fastest; `int32_t` packs only 16 per line and should be slowest. But what actually comes out: `int16_t` is fastest, and `uint8_t` is **actually the slowest**, about 10% slower than `int16_t`. Cache space saved by 4x, and the speed goes down instead of up.

## Won at the Cache End, Lost at the Execution-Unit End

The problem is this: performance isn't a single dimension. Cache hit rate is a bottleneck, but not the only one. Inside the CPU, the execution units have different throughputs for different integer widths.

On x86-64, addition of `int32_t` is the most "native" operation — the registers are 32/64-bit anyway, one `add` instruction does it, throughput is highest. Arithmetic on `int16_t` and `uint8_t` can be done too, but narrow types often need extra **sign-extension** or **zero-extension** instructions to widen the result back to a full register, and x86's instruction encoding for 8-bit arithmetic carries some historical baggage, so its throughput really is a bit lower than for 16-bit.

So the picture becomes: `uint8_t` saves 4x space at the cache end (fewer cache misses), but at the execution-unit end every addition has worse throughput (slower arithmetic). Weighing the two, at this data size (50 million, the 47 MB array still exceeds L3), the cache win couldn't outweigh the arithmetic loss, and overall it ended up slower than `int16_t`. `int16_t` sits in the sweet spot: cheaper on cache than `int32`, without the throughput regression that `uint8` suffers, so it wins.

## Turn Off Vectorization and Confirm Again

To rule out the compiler's auto-vectorization interfering, let's disable vectorization (`-fno-tree-vectorize`) and look at the pure scalar result:

```text
-O2 -fno-tree-vectorize (scalar, no vectorization)
  int32_t  : 11537.6 us   ← fastest
  int16_t  : 12988.0 us
  uint8_t  : 12450.8 us
```

Under pure scalar, `int32_t` is actually fastest — because scalar `add` is most direct for 32-bit, and narrow types need extra instructions. This further confirms the explanation above: the arithmetic throughput penalty for narrow types is real. It's just that in the vectorized version the compiler unrolled the loop and partially canceled out the difference, so the gap is less dramatic — but the direction didn't change.

::: warning Optimization is never a single dimension
This is the one lesson from the cache series most worth remembering: **improving cache behavior can hand the gains right back on instruction-execution efficiency**. You pack the data more tightly, cache hit rate goes up, but if that same change drags down the throughput of the CPU's arithmetic units, the net effect can be zero or even negative. So measure, measure, measure — don't just decide based on the "cache-friendly must be fast" intuition.
:::

## So How Should You Actually Choose a Data Type

Don't swing to the other extreme and decide "shrinking the type is useless." The experiment above only shows that "blindly shrinking to the smallest isn't necessarily fastest," not that "shrinking is useless." The right approach is to split by scenario:

**For pure-storage fields that rarely do arithmetic, shrink without hesitation.** The canonical case is enums. C++ gives enums a default underlying type of `int` (4 bytes), but most enums have only a handful of values — spending 4 bytes on a 0/1/2 is pure waste. And you basically never do arithmetic on enum values, so there's no arithmetic-throughput regression to worry about; it's pure space savings:

```cpp
enum class Color : uint8_t { Red, Green, Blue };       // 1 byte
enum class Month : uint8_t { Jan = 1, Feb, Mar, /*...*/ Dec };
```

When a struct has several enum fields, the space this one trick saves stacks up considerably, the array gets more compact, and cache hit rate genuinely rises.

**For fields that do arithmetic and whose data isn't large enough to blow out the cache, don't shrink blindly.** Averaging ages, accumulating statistics — for this kind of work `int32_t` is usually the sweet spot, or at most `int16_t`. Here the arithmetic-throughput weight matters more than cache space.

**For scenarios with huge data volumes and very light arithmetic, shrinking is a sure win.** For example, if you're just walking a pile of flag bits counting how many are true, there's almost no arithmetic — then the cache advantage of `uint8_t` or `int8_t` can fully shine, and the execution-unit regression barely matters.

But all of these judgments ultimately come back to one thing: **measure under your target data size and access pattern**. The numbers from my machine, the ranking I got here — they only give you a direction.

## A Trap to Avoid: Don't Use `char` to Store Numbers

A related language trap, while we're here. If you decide to use a 1-byte type for numbers, **don't use `char`**. In the C++ standard, `char`, `signed char`, and `unsigned char` are **three distinct types**, and the signedness of `char` is implementation-defined (usually signed on x86, often unsigned on ARM). You write `char x = 200;` and its behavior can differ across platforms — a latent cross-platform landmine.

For numeric cases, write `int8_t` or `uint8_t` explicitly (they're aliases for `signed char` and `unsigned char` respectively, but the semantics are clear), and leave `char` for characters. `signed int` and `int` are the same thing; `char` is the lone exception — that's historical baggage, just remember the conclusion.

As for `char8_t` (the UTF-8 character type introduced in C++20), its underlying type is `unsigned char` but it's a distinct type in the type system, and it has one notable side effect: it's not on the strict-aliasing rule's "universal alias" exception list, so the compiler is more "comfortable" with it and can sometimes do more aggressive optimization. Jonathan showed a case in the talk where `char8_t` was more than twice as fast as `char`. But this phenomenon depends on compiler version and context; I couldn't reproduce it stably on my machine, so I'm only mentioning it here as a deep-cut easter egg about "type choice affects optimization" — I do not recommend you switch numeric types to `char8_t` for this: the semantics are wrong, and readability suffers.

## The Boundary of This Layer

In this part we saw that choosing a data type isn't simply "save memory" — it's a trade-off between cache hit rate and arithmetic throughput. Shrinking the type scores on the cache end and can lose on the execution-unit end; the net effect, you only know by measuring.

The next part pulls the scattered conclusions from the previous parts together into a deployable engineering method: how to design data layout, how to separate hot and cold data, when to align by hand, and — facing a concrete scenario — how to make cache-friendly decisions step by step.

[Next: Writing cache-friendly code →](04-writing-cache-friendly-code.md)
