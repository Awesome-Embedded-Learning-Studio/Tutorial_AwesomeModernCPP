---
title: "mini STL Introduction: Why Hand-Rolling Containers Is Worth It"
description: "Starting from the old line that 'data structures are really best done in C,' this piece explains why this standalone mini project hand-rolls its containers with templates: how the industrial implementations were picked as mirrors (std implementations, libstdc++, absl, and Chromium base, each reflecting a different piece), the roadmap and boundaries of the thirteen hand-rolled pieces, and why red-black trees and small-object vectors are deliberately left unrolled."
chapter: 0
order: 0
tags:
  - host
  - cpp-modern
  - intermediate
  - 容器
  - 内存管理
difficulty: intermediate
platform: host
reading_time_minutes: 9
cpp_standard: [17, 20, 23]
prerequisites:
  - "Container Selection Guide: Choosing the Right Container Based on Operations, Memory, and Invalidation Rules"
  - "Deep Dive into std::vector: Three Pointers, Reallocation, and Iterator Invalidation"
related:
  - "mini STL in Practice (Part 1): RawBuffer — Capacity, Not Objects"
  - "Comprehensive Project: A mini-STL Algorithm Library with Concepts"
translation:
  source: documents/vol8-domains/data-structure/pre-00-mini-stl-why-handroll.md
  source_hash: e82419bd7e2b8fdabda80673f154c2441720a654cd3b5aaa4246d62e0e2c8f5a
  translated_at: '2026-09-25T08:48:24+00:00'
  engine: anthropic
  token_count: 6300
---

# mini STL Introduction: Why Hand-Rolling Containers Is Worth It

We write `std::vector` every single day, but if its implementation were deleted from the compiler, could you write one out of thin air? Chromium's engineers actually did it in 2017: the copyright year on `base/containers/vector_buffer.h` is 2017 — 179 lines, a raw buffer that "manages capacity, not objects," and it remains the foundation of `base::circular_deque` to this day. A browser-kernel team, with the standard library sitting right there, rewrote their container foundation by hand. Their reason sits at the top of the file: "Unlike `std::vector`, VectorBuffer never constructs or destructs its arguments". The standard library's vector refuses to unbundle "memory" from "object lifetime", and they needed them unbundled.

This series walks you through doing exactly that. It is a standalone mini project: we build our own mini container library from scratch (namespace `tamcpp::ministl`), code and tests all ours; each time we finish a piece, we hold it up against a real industrial implementation for a mirror check — the dynamic array against `std::vector`'s internals, the classic linked list against libstdc++'s `stl_list`, hashing against absl's swiss table, the ring buffer and LRU against Chromium base. Which mirror each piece faces is decided by "where this container's most valuable lesson lies".

## Hand-Roll It in C, or in Templates

The proposal for this series contains one line: "Data structures, honestly, are best done in C." There is truth to it. Write a dynamic array in C: `malloc` a block of raw memory, cast it through `void*`, stuff elements in one by one — no constructors, no destructors, no type system debating "move semantics" with you — and the skeleton of the data structure (capacity, growth, relocation, indexing) lies spread out in front of you in its rawest form. That is how university data-structure courses teach it, too.

But the C approach starts to hurt on the question of "what type does it hold". If you want an array that holds `int` one day and `struct Task*` the next, C offers exactly two roads: macros, or `void*` plus a pile of casts. The former hands type safety over to the preprocessor's string substitution; the latter hands it to the caller's discipline. Follow either road to the end and you will find yourself simulating a crippled template system in C.

Templates exist precisely for this. One line — `template <typename T> class Vector` — hands type parameterization to compile time, and under type checking at that. Forgetting to destroy an element is a memory error in C; in C++ it can be a compile-time dispatch (`if constexpr`) plus a static assertion. So this series lands on both ends. The skeleton gets rolled with C instincts: we will constantly look at memory layouts and real output, nitpicking details the way you would when writing C. Interfaces and type safety go to the templates. The proposal author said so too — this is "a fine occasion for templates": learning concepts, `if constexpr`, template parameter injection with no real container to write always feels a bit like war-gaming on paper.

## Which Mirrors Are We Holding It Up To

Our own mini library stays the protagonist throughout; the industrial implementations are reference objects. The internals of `std::vector` and `std::list` are permanent fixtures: what the standard promises, and how the implementation on this machine actually does it — the libstdc++ headers are just lying there. The hashing piece's reference is absl's swiss table design doc, the clearest account of how hash tables evolved into what they are today; the ring buffer, intrusive containers, and LRU mirror Chromium's `base/containers/`. The README in that directory introduces itself in its first line as "some stdlib-like containers" — and of the thirty-odd headers, only these few form a system of their own:

| Container               | One-line identity                        | Its exclusive edge                                                                |
| ----------------------- | ---------------------------------------- | --------------------------------------------------------------------------------- |
| `flat_map` / `flat_set` | Associative containers over sorted vectors | Zero heap allocation at small sizes, cache-friendly (covered in depth in vol9)     |
| `circular_deque`        | Ring-buffer deque                        | Stands in for `std::deque` — huge implementation variance, wasted memory           |
| `small_map`             | Hybrid: array when small, real map when big | In a browser, the most common map size is just 4                                 |
| `intrusive_heap`        | A heap with handles                      | Backs the browser task scheduler's delayed queues                                 |
| `LinkedList`            | Intrusive doubly-linked list             | Zero-allocation inserts, O(1) erase of a known node                                |
| `LRUCache`              | A list-plus-map composite                | New code from 2026, concepts in active service                                     |
| `RingBuffer`            | Fixed-capacity ring window               | Keeps only the most recent N samples — familiar sight to anyone who has written for MCUs |

"What's absent" is a mirror too — we look at what this directory lacks: no `small_vector` (a vector with inline storage; Chromium uses absl's `InlinedVector` instead of building its own), no homegrown hash table (for general-purpose maps the direct recommendation is `absl::flat_hash_map`), and no classic linked list (when you need stable iterators, the official advice is to just use `std::list`; the house only builds the intrusive variant). Why some get built and others don't — each one is a real engineering trade-off, and the closing piece will walk through these choices one by one.

::: warning
A word on expectations: this series is not a source-code reading. The hand-rolled pieces average a hundred-odd lines — the industrial versions' safety macros, performance annotations, and compatibility layers are cut away, leaving only the bones of the data structure. When you cross-check against sources, line numbers refer to the local snapshot; if the version in your hands differs and the line numbers do not line up, that is normal.
:::

## The Method: Hand-Roll, Mirror-Check, Make Trade-Offs

Every container follows the same route. Before looking at any source, we write the minimal working version and get the tests passing — mistakes are what stick; that old truth has been re-verified by every hands-on learning project. Then we open the corresponding industrial implementation and compare fork by fork in the road: how is capacity represented? What growth factor? Do iterators carry a parent pointer? The same problem, and they spent hundreds of lines on it — the differences themselves are the textbook.

Finally we spell out in plain words "why our teaching version differs from theirs". For example, our library's subscript bounds check goes through a crash-style `Check` (following Chromium), while `std::vector::at` throws (so the caller can catch and recover); both sides have their reasons, and a reason only earns its keep once it is spoken aloud.

Thirteen hand-rolled pieces plus one closing piece, ordered by dependency: the common foundations first, the ones with industrial backstory later. The foundation is a raw buffer plus the dynamic array `Vector`. Then we go circular: fixed-capacity `RingBuffer`, dynamic `CircularDeque`, with a page of stack/queue adapters along the way. Next, linked lists in three forms: the simplest singly-linked `ForwardList`, the classic doubly-linked `List` where the container owns its nodes, and the Chromium-style intrusive `LinkedList` variant. Once you have seen all three, the question "where do the nodes actually live" clicks fully into place. Above that sit the associative structures: assembling a `FlatMap`, hand-rolling an open-addressing `HashMap`, and the mixed-strategy `SmallMap`. The finale is the heap (`IntrusiveHeap`) and the graduation composite (`LRUCache`). Each layer's knowledge points get reused by the next — the relocation technique from the foundation piece reappears verbatim in ring growth.

## What We Deliberately Don't Hand-Roll

No red-black tree. That is what `std::map` is built on, and vol3's conceptual layer already covered its node layout thoroughly; the reason we skip it here is blunt: its difficulty is a combinatorial explosion of a dozen-plus rotations and recolorings, and the pedagogical payoff does not match that difficulty. One widely circulated hand-written STL tutorial has the author admitting, in the red-black-tree lesson, that "the edge cases were so numerous I wrote countless bugs". The main takeaway from finishing it is awe for the red-black tree. For the ordered-structure slot we substitute a sorted vector: same lookup semantics, an order of magnitude less mental load.

`small_vector` (the kind of vector that stores a few elements inside the object body itself, never touching the heap) does not get rolled either — it becomes a discussion question in the closing piece: Chromium never built this one themselves, and the reason alone is worth a paragraph.

`std::string`, `std::span`, and allocator policy each have dedicated pieces in vol3 and vol4; we will not repeat them.

One last disambiguation: the vol4 metaprogramming sub-volume contains a "mini-STL algorithm library" piece that hand-rolls algorithms like `transform` and `accumulate`; what we roll here is all containers — put the two halves together and you get the complete mini STL. The algorithms sub-domain within this same volume, the one covering sorting and searching, does not step on this ground either.

## Where to Start

The first piece is `RawBuffer`, a buffer that manages capacity and nothing about objects — the common starting point of every container in our library, and the foundation on the Chromium side too. The companion code lives in `code/volumn_codes/vol8-labs/ministl/`, growing as stage directories: right now it is `stage1_rawbuf_vector/`, and each new container opens a new stage directory, so reviewers can take them one stage at a time. That piece contains a pit that stopped the author dead the first time he stepped in it — caught by UBSan — and we will run it for real, live, for you to watch.

## References

- The Chromium `base/containers/` directory and its README (the official container-selection guide)
- `base/containers/vector_buffer.h` (2017, this series' first mirror)
- The local libstdc++ `bits/stl_list.h` and `bits/forward_list.h` (mirrors for the linked-list pieces); [the absl swiss table design doc](https://abseil.io/about/design/swisstables) (mirror for the hashing piece)
- [stl1weekend (Xiao Peng's hand-written STL series)](https://github.com/parallel101/stl1weekend) (a fellow traveler on the "implement the STL yourself" route; the source of the red-black-tree blood-and-tears quote)
- [Comprehensive Project: A mini-STL Algorithm Library with Concepts](../../vol4-advanced/vol3-metaprogramming-cpp20-23/09-mini-stl-with-concepts.md) (vol4, the algorithms half)
- [Container Selection Guide](../../vol3-standard-library/containers/01-container-selection-guide.md) (vol3, the conceptual-layer entry point)
