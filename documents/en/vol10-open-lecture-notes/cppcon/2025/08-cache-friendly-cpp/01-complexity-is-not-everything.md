---
title: "Complexity Is Not Everything: How O(1) Lost to O(n)"
description: "CppCon 2025 notes — Cache-Friendly, part one. Measured on GCC 16.1.1: a 16-element vector linear search (O(n)) beats unordered_set (O(1)); at large sizes a sorted-vector binary search is more than 2x faster than set"
chapter: 8
order: 1
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
  - 'Memory Access Really Is 100x Slower: The Cache Hierarchy and Cache Lines'
  - The Branch Predictor Is Helping You Cheat
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/08-cache-friendly-cpp/01-complexity-is-not-everything.md
  source_hash: abdde253fc39b6a0742d3f8f380a33b97506977a0feaf16ee9dd76b2cbc98684
  translated_at: '2026-09-26T16:40:20+00:00'
  engine: anthropic
  token_count: 4000
---

# Complexity Is Not Everything: How O(1) Lost to O(n)

::: tip Where these notes come from
This series is a riff on Jonathan Müller's CppCon 2025 talk *Cache-Friendly C++*. Jonathan has worked in low-latency C++ for a long time, and in this talk he goes after CPU caching from the roots up. The original talk is on [YouTube](https://www.youtube.com/watch?v=g_X5g3xw43Q). I paired every key claim from the talk with a benchmark run on my own machine — the numbers are ones I ran myself, not screenshots lifted from the slides.
:::

A lot of people learn C++ with a rigid formula for picking containers: `unordered_set` lookup is O(1), `set` is O(log n), `vector` is O(n), so lookups "obviously" belong in `unordered_set`. Textbooks say so, interviews test it, and I used to believe it too. Then I actually wrote a benchmark and found out the formula simply doesn't hold on a real machine. This piece starts from that counterintuitive result and breaks the complexity superstition first — the cache mechanics get their detailed treatment in the [next part](02-memory-is-100x-slower.md).

## Four Contenders, One Lookup Race

The scenario is simple: stuff N integers into a container, then do 100,000 lookups (half hits, half misses, order shuffled) and watch how the time scales with N. Four common choices compete:

- **`std::vector` (unsorted)**: filled with `push_back`, linear scan with `std::find`, O(n).
- **`std::vector` (sorted)**: sorted, binary search with `std::lower_bound`, O(log n).
- **`std::set`**: red-black tree, O(log n).
- **`std::unordered_set`**: hash table, amortized O(1).

By the complexity dogma, the ranking should be `unordered_set` running away with it, `set` and sorted `vector` tied for second, unsorted `vector` dead last. Let's look at the real numbers on my machine (GCC 16.1.1, `-O2`), starting with the unsorted `vector` linear search:

```text
N=    16  vec线性=   482.0 us  set=   603.0 us  unordered=   638.0 us
N=    64  vec线性=   994.0 us  set=   951.0 us  unordered=   622.0 us
N=   256  vec线性=  3099.0 us  set=  1375.0 us  unordered=   660.0 us
N=  1024  vec线性= 12464.0 us  set=  2115.0 us  unordered=   715.0 us
N=  8192  vec线性= 89199.0 us  set=  3556.0 us  unordered=   782.0 us
N= 65536  vec线性=568461.0 us  set= 14071.0 us  unordered=  1161.0 us
```

Stare at the first row. **At N=16, the O(n) `vector` linear search beats the O(1) `unordered_set`, and beats the O(log n) `set` too.** The theoretically slowest option is the fastest on a real machine. This isn't noise — five runs with the median taken, and the result is stable every time.

Keep reading down. As N grows, the unsorted `vector`'s O(n) nature comes out and the time shoots up (568 ms at N=65536), and only then does `unordered_set`'s O(1) advantage establish itself. But even at the N=65536 scale, `unordered_set` is only about an order of magnitude faster than `set` — nowhere near as lopsided as "O(1) vs O(log n)" makes it sound.

## Same O(log n), the Cache-Friendly One Crushes the Other

The unsorted `vector` winning at small sizes is already enough to break the dogma. But the more convincing experiment is pulling the two O(log n) contenders aside for a one-on-one: sorted `vector` binary search against `std::set` red-black tree search. Identical complexity, both O(log n) — by the dogma they should run neck and neck.

```text
N=    64  vec二分= 1059.0 us  set=   949.0 us  unordered=   630.0 us
N=  1024  vec二分= 2041.0 us  set=  2022.0 us  unordered=   713.0 us
N=  8192  vec二分= 3037.0 us  set=  3946.0 us  unordered=   796.0 us
N= 65536  vec二分= 5425.0 us  set= 10766.0 us  unordered=  1092.0 us
N=262144  vec二分= 8090.0 us  set= 19903.0 us  unordered=  1483.0 us
```

At N=64 and N=1024 the two are about even (at N=1024, 2041 vs 2022 — basically a tie). But from N=8192 on, the sorted `vector` pulls ahead and the gap keeps widening: at N=65536, `vec二分` at 5425 us vs `set` at 10766 us — **nearly twice as fast**; at N=262144, 8090 vs 19903 — **more than twice as fast**.

Identical complexity, and one leaves the other more than two times behind. Big-O notation cannot explain this.

## What Big-O Hid From You

The problem sits in how big-O notation defines an "operation". Big-O counts the **number of operations** — how many comparisons, how many hashes — but it is completely blind to the **cost of each operation**. And on a real machine, the price of the same "operation" can differ by two orders of magnitude.

`std::set` is a red-black tree underneath, and every node is its own `new`-allocated chunk of memory, scattered across the far corners of the heap. A lookup has you chasing pointers the whole way: root → left child → right child's right child... every hop lands on a fresh address far from the previous one, and each is very likely a cache miss. And one cache miss waits tens to over a hundred nanoseconds (exact numbers in the next piece) — the time of dozens to a hundred ordinary operations.

The sorted `vector` is the exact opposite. Its data is laid out **contiguously** in one stretch of memory. In a binary search, each jump may land logically far from the last one (from the middle to the quarter mark), but physically they are all packed into the same contiguous stretch. More importantly, the CPU doesn't move data byte by byte — it moves whole **cache lines** (typically 64 bytes) at a time. When you read the element in the middle of the array, several neighbors on either side ride into the cache as part of the same block, so the next few binary-search jumps very likely land in cache already. On top of that, the hardware prefetcher notices you touching contiguous memory in a pattern and starts pulling later data in ahead of time.

So the picture looks like this: `set` pays for a possible main-memory access on every node hop — fewer operations, but each one expensive; `vec二分` runs a few more operations, but the vast majority hit cache and each costs nearly nothing. Weigh the two sides, and the cache-friendly one wins outright. The bigger N gets, the more spread out `set`'s nodes are and the less cache-friendly it becomes, so the wider the gap.

`unordered_set` wins at large sizes because its hash table is designed to keep the bucket array contiguous where possible, and with O(1) it really does run fewer operations — combined, it comes out fastest. But its O(1) isn't free either: there are still jumps between hash-table nodes, so at small sizes its constant factor is larger than a contiguous-memory `vector`'s, and it gets overtaken. That is exactly what happens in the N=16 row.

## So How Should You Actually Pick a Container

Once you understand the mechanism above, the way you pick containers has to change: complexity alone isn't enough — you have to weigh data scale and access pattern too. A pragmatic decision reference:

**Lookup-heavy, small data (tens to a few hundred)**: prefer an unsorted `vector`. Inserts are fast (`push_back`), the data is contiguous and cache-friendly, and at small sizes a linear search is much faster than you'd think. The code is simpler, too.

**Lookup-heavy, medium data (thousands to a few hundred thousand), sortable**: a sorted `vector` with `std::lower_bound`. Cache-friendliness lets it beat `set` at equal complexity, and the memory overhead is far smaller too (no node pointers).

**Frequent insert + lookup, large data**: `unordered_set` / `unordered_map`. Remember to `reserve` to avoid rehashing. This is where the O(1) advantage truly pays off.

**Need ordered iteration or range queries**: `std::set` or a sorted `vector`. Keep `set`'s cache disadvantage in mind.

This is an opening reference, not a dogma. Jonathan hammers one principle throughout the talk: **always benchmark under your target data scale and access pattern**. Other people's benchmark numbers (including the tables above) can give you a direction, never an answer, because cache behavior correlates strongly with the CPU microarchitecture, the data distribution, and even the memory footprint of the surrounding code.

## The Boundary of This Layer

In this piece we used experiments to break the superstition that "lower complexity is always faster", and watched cache locality crush algorithmic complexity at small data sizes. But on "why caching matters this much" we've only stated the conclusion — the mechanism is still ahead of us. Why does a single cache miss wait that long? What unit does the cache actually move data in? How much do the L1, L2, and L3 levels differ in latency? Why is sequential access more than ten times faster than random access? Those questions star in the next piece, and only once you've got them straight can you design data structures that exploit the cache deliberately instead of by luck.

[Next: Memory Access Really Is 100x Slower →](02-memory-is-100x-slower.md)
