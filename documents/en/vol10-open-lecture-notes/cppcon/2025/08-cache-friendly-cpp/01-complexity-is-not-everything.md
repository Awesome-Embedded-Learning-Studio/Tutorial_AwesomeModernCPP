---
title: "Complexity Is Not Everything: How O(1) Lost to O(n)"
description: "CppCon 2025 notes — Cache-Friendly, part one. Tested on GCC 16.1.1: a 16-element vector linear search (O(n)) beats unordered_set (O(1)); at large N, a sorted vector binary search is over 2x faster than set"
chapter: 8
order: 1
conference: cppcon
conference_year: 2025
talk_title: 'Cache-Friendly C++'
speaker: Jonathan Müller
cpp_standard: [20]
difficulty: intermediate
platform: host
reading_time_minutes: 12
tags:
  - cpp-modern
  - host
  - intermediate
  - 优化
related:
  - "Memory Access Being 100x Slower Is Real: Cache Hierarchy and Cache Lines"
  - "The Branch Predictor Is Cheating for You"
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/08-cache-friendly-cpp/01-complexity-is-not-everything.md
  source_hash: 02c4ec7896ed911cd66fbe24c70520a41fe1b97ef8a741a35beb5b021d74ae37
  translated_at: '2026-07-29T00:33:58.679864+00:00'
  engine: anthropic
  token_count: 2311
---

# Complexity Is Not Everything: How O(1) Lost to O(n)

::: tip Where these notes come from
This series is a riff on Jonathan Müller's CppCon 2025 talk *Cache-Friendly C++*. Jonathan has worked in low-latency C++ for a long time, and here he gets down to the roots of CPU caching. The original talk is on [YouTube](https://www.youtube.com/watch?v=g_X5g3xw43Q). I paired every key claim from the talk with a benchmark on my own machine — the numbers are ones I ran myself, not screenshots lifted from the slides.
:::

A lot of people learn C++ with a rigid formula for picking containers: `unordered_set` lookup is O(1), `set` is O(log n), `vector` is O(n), so lookups "obviously" belong in `unordered_set`. Textbooks say so, interviews test it, and I used to believe it too. Then I actually wrote a benchmark and found out the formula doesn't hold on a real machine. This piece starts from that counterintuitive result and breaks the complexity superstition — we'll save the cache mechanics for the [next part](02-memory-is-100x-slower.md).

## Four Contenders, One Lookup Race

The scenario is simple: fill a container with N integers, then do 100,000 lookups (half hit, half miss, order shuffled) and watch how the time scales with N. Four common options compete:

- **`std::vector` (unsorted)**: fill with `push_back`, linear scan with `std::find`, O(n).
- **`std::vector` (sorted)**: sort it, binary search with `std::lower_bound`, O(log n).
- **`std::set`**: red-black tree, O(log n).
- **`std::unordered_set`**: hash table, amortized O(1).

By the complexity dogma, the ranking should be `unordered_set` running away with it, `set` and sorted `vector` tied for second, unsorted `vector` dead last. Let's look at the real numbers on my machine (GCC 16.1.1, `-O2`), starting with the unsorted `vector` linear search:

```text
N=    16  vec_linear=   482.0 us  set=   603.0 us  unordered=   638.0 us
N=    64  vec_linear=   994.0 us  set=   951.0 us  unordered=   622.0 us
N=   256  vec_linear=  3099.0 us  set=  1375.0 us  unordered=   660.0 us
N=  1024  vec_linear= 12464.0 us  set=  2115.0 us  unordered=   715.0 us
N=  8192  vec_linear= 89199.0 us  set=  3556.0 us  unordered=   782.0 us
N= 65536  vec_linear=568461.0 us  set= 14071.0 us  unordered=  1161.0 us
```

Look at the first row. **At N=16, the O(n) `vector` linear search beats the O(1) `unordered_set`, and beats the O(log n) `set` too.** The theoretically slowest option is the fastest on a real machine. This isn't noise — it's stable across five runs with the median taken.

Keep reading down. As N grows, the unsorted `vector`'s O(n) nature shows itself and the time shoots up (568 ms at N=65536), and only then does `unordered_set`'s O(1) advantage establish itself. But even at N=65536, `unordered_set` is only about an order of magnitude faster than `set` — nowhere near as dramatic as "O(1) vs O(log n)" sounds.

## Same O(log n), the Cache-Friendly One Crushes the Other

The unsorted `vector` winning at small sizes is already enough to break the dogma. But the more convincing case is to pull the two O(log n) contenders aside and let them go head to head: sorted `vector` binary search against `std::set` red-black tree search. Same complexity, both O(log n), so by the dogma they should be neck and neck.

```text
N=    64  vec_bsearch= 1059.0 us  set=   949.0 us  unordered=   630.0 us
N=  1024  vec_bsearch= 2041.0 us  set=  2022.0 us  unordered=   713.0 us
N=  8192  vec_bsearch= 3037.0 us  set=  3946.0 us  unordered=   796.0 us
N= 65536  vec_bsearch= 5425.0 us  set= 10766.0 us  unordered=  1092.0 us
N=262144  vec_bsearch= 8090.0 us  set= 19903.0 us  unordered=  1483.0 us
```

At N=64 and N=1024 they're about even (at N=1024, 2041 vs 2022 — basically a tie). But from N=8192 on, sorted `vector` pulls ahead and the gap keeps widening: at N=65536, `vec_bsearch` is 5425 us vs `set` at 10766 us — **nearly twice as fast**; at N=262144, 8090 vs 19903 — **more than twice as fast**.

Identical complexity, and one leaves the other more than two times behind. Big-O can't explain this.

## What Big-O Hid From You

The problem is in how big-O defines an "operation." Big-O counts the **number of operations** — how many comparisons, how many hashes — but it's completely blind to the **cost per operation**. And on a real machine, the cost of one "operation" can vary by two orders of magnitude.

`std::set` is a red-black tree underneath, and every node is a separate `new`-allocated chunk of memory scattered across the heap. During a lookup you chase pointers: root → left child → right child's right child... each hop lands on a fresh address far from the last one, and each is very likely a cache miss. A single cache miss costs tens to over a hundred nanoseconds (exact numbers in the next piece) — the equivalent of dozens or hundreds of ordinary operations.

Sorted `vector` is the exact opposite. Its data is laid out **contiguously** in one block of memory. In a binary search, each jump may be logically far from the last (from the middle to the quarter mark), but physically they all sit in that same contiguous block. More importantly, the CPU doesn't move data byte by byte — it moves entire **cache lines** (typically 64 bytes) at once. When you read the element in the middle of the array, several neighbors come into the cache as part of the same block, so the next few binary-search jumps probably hit cache. On top of that, the hardware prefetcher notices you're touching contiguous memory in a pattern and starts pulling in the data ahead of time.

So the picture looks like this: `set` pays a possible main-memory access on every node hop — few operations, but each one is expensive; `vec_bsearch` runs slightly more operations, but the vast majority hit cache and each one is nearly free. Net it out, and the cache-friendly one wins easily. The bigger N gets, the more spread out `set`'s nodes are, the less cache-friendly it is, and the wider the gap.

`unordered_set` wins at large sizes because its hash table is designed to keep the bucket array contiguous, and with genuinely few O(1) operations the combination is fastest. But its O(1) isn't free either — there are still jumps between hash-table nodes, so at small sizes its constant factor is larger than a contiguous `vector`'s, and it gets beaten. That's exactly what happens in the N=16 row.

## So How Do You Actually Pick a Container?

Once you understand the mechanism above, the way you pick containers has to change: you can't look at complexity alone, you have to look at the data scale and the access pattern. A pragmatic decision guide:

**Lookup-heavy, small data (tens to a few hundred):** prefer an unsorted `vector`. Insertions are fast (`push_back`), the data is contiguous and cache-friendly, and at small sizes a linear search is far faster than you'd expect. The code is simpler, too.

**Lookup-heavy, medium data (thousands to a few hundred thousand), sortable:** sorted `vector` with `std::lower_bound`. Cache-friendliness lets it beat `set` at equal complexity, and the memory overhead is far smaller (no node pointers).

**Frequent insert + lookup, large data:** `unordered_set` / `unordered_map`. Remember to `reserve` so you avoid rehashing. This is where the O(1) advantage actually pays off.

**Need ordered iteration or range queries:** `std::set` or a sorted `vector`. Keep `set`'s cache disadvantage in mind.

This is a starting reference, not a dogma. Jonathan hammers one principle throughout the talk: **always benchmark on your target data size and access pattern.** Other people's benchmark numbers (including the tables above) can give you a direction, not an answer — cache behavior depends heavily on the CPU microarchitecture, the data distribution, and even the memory footprint of the surrounding code.

## The Boundary of This Layer

In this piece we used experiments to break the superstition that "lower complexity is always faster," and we saw cache locality crush algorithmic complexity at small sizes. But the "why" of caching — why it matters so much — we've only stated as a conclusion, not explained the mechanism. Why does a single cache miss take so long? What unit does the cache actually move data in? How much do the L1, L2, and L3 latencies differ? Why is sequential access over ten times faster than random? Those are the subject of the next piece, and only by understanding them can you design data structures that exploit the cache on purpose, rather than by luck.

[Next: Memory access being 100x slower is real →](02-memory-is-100x-slower.md)
