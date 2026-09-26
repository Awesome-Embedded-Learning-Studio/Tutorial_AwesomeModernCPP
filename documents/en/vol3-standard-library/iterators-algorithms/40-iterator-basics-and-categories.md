---
chapter: 7
cpp_standard:
- 11
- 20
description: 'A thorough look at iterator categories: iterators are the generalization of pointer usage and the common interface between containers and algorithms; how the five strength tiers (including the C++20 contiguous tier) decide which algorithms work, how compile-time tag dispatch affects std::distance performance, and why std::sort cannot be used on std::list'
difficulty: intermediate
order: 40
platform: host
prerequisites:
- 'Deep Dive into std::vector: Three Pointers, Reallocation, and Iterator Invalidation'
- 'array: A fixed-size aggregate container determined at compile time'
reading_time_minutes: 10
related:
- 'Container Selection Guide: Choosing the Right Container Based on Operations, Memory, and Invalidation Rules'
tags:
- host
- cpp-modern
- intermediate
- Ranges
title: 'Iterator Basics and Categories: The Glue Between Containers and Algorithms'
translation:
  source: documents/vol3-standard-library/iterators-algorithms/40-iterator-basics-and-categories.md
  source_hash: 46e17c556293a15a8b119b95339678f6b32e1497875d81f49cf0dd70c0ba1339
  translated_at: '2026-09-26T01:34:31+00:00'
  engine: anthropic
  token_count: 4900
---
# Iterator Basics and Categories: The Glue Between Containers and Algorithms

We have now walked the container track to its end — `array`, `vector`, `list`, `map` — the data-holding crew has basically all reported in. But the moment you want to hand them over to algorithms like `std::sort`, `std::find`, and `std::transform`, an interesting question pops up: why does `std::sort` work on both `vector` and `array`, yet fail to even compile for `list`? No algorithm hard-codes which container it recognizes.

The answer hides in the thin layer of common interface sitting between containers and algorithms — the iterator. In this article we take the iterator apart: what it actually is, why it has "strength levels" (that is, categories), and how that level decides at compile time whether a piece of code can run at all, and how fast it runs.

## What Is an Iterator: Generalizing Pointer Usage

Start from the tool we know best: the pointer. Given an array, we can read a value with `*p`, step forward with `++p`, and test whether we have reached the end with `p != end` — those three moves are enough to walk from head to tail. What the iterator does is abstract "this set of pointer usages" away: as long as a type supports dereference, increment, and comparison, it can serve as an iterator; whether it is backed by a contiguous array, a linked-list node, or some other structure is none of the algorithm's business.

In other words, a raw pointer is a kind of "native iterator", while `vector::iterator` and `list::iterator` are iterators that "look like pointers but carry their own container on their back". Algorithms recognize only this unified interface, which is why a single `std::find` serves every container. This was the most crucial design decision of the STL in its day: **decouple containers from algorithms, and let the iterator layer be where they meet**.

## category: Iterators Have Strength Levels

"Supports dereference and increment" is only the lowest bar. What different iterators can do differs a lot: some can only move forward and be read once; others can jump at random to any position. The more operations an iterator supports, the higher its "level" — the standard calls this the iterator category.

From weakest to strongest, the classic lineup looks like this, each tier always adding capabilities on top of the one before (these are the old five from before C++20, plus the strongest tier newly added in C++20):

- **input**: can read, can `++`, can compare for equality, but can only make a single forward pass (typical example: `istream_iterator`).
- **forward**: on top of input, allows multi-pass traversal (typical example: `forward_list`).
- **bidirectional**: adds `--`, can step backward (typical examples: `list`, `set`, `map`).
- **random_access**: adds `+n`, `[]`, and ordering comparison, can jump around at random (typical examples: `vector`, `deque`, raw pointers).
- **contiguous** (new in C++20): on top of random_access, additionally guarantees that elements are stored contiguously in memory (typical examples: `vector`, `array`, `string`, raw pointers).

There is also **output**, dedicated to writing and never reading, listed off to the side.

Ranking tiers in words is a bit airy, so let us take C++20 concepts and judge at compile time which tier each container's iterator actually lands in. A concept is a compile-time predicate handed to us by C++20: if `std::random_access_iterator<T>` is true, then `T` satisfies every requirement of a random access iterator — there is no dodging it. The idea is plain: write a `print_row` template that checks, for each container's iterator, the five predicates `input_iterator` / `forward_iterator` / `bidirectional_iterator` / `random_access_iterator` / `contiguous_iterator` in turn and prints each yes/no verdict as one row — open the online demo below and run it right there to see the real judgment:

<OnlineCompilerDemo
  title="Sizing Up Iterator Categories with C++20 concepts"
  source-path="code/examples/vol3/40_iterator_categories.cpp"
  description="print_row checks the five concept predicates one by one for the iterators of vector/array/string/raw pointers/list/set/forward_list and prints a yes/no table — strong versus weak at a glance"
  allow-run
/>

The run results lay the hierarchy out plainly: `vector`, `array`, `string`, and raw pointers light up all five — the strongest tier, able to jump around memory at random and stored contiguously (contiguous); `list` and `set` stop at bidirectional — they can walk forward and back but cannot leap over with `it + 5`; `forward_list` is the weakest, single-direction forward only. Strength is not about "who wrote it better" — it is decided by the data structure itself: a linked list's nodes sit all over memory, one here and one there, so you simply cannot compute the address of the n-th node directly with `it + n`.

## Why category Matters: It Decides Which Algorithms You Can Use

Back to the question from the opening. The standard spells out each algorithm's requirement on the iterator category: `std::find` needs only input (scanning ahead is enough), `std::reverse` needs bidirectional (it must walk backward), `std::sort` needs random_access (quicksort has to jump around at random to grab a pivot and partition). These requirements are not just words in a document — hand the algorithm an iterator that falls short and compilation fails on the spot.

So throwing `std::sort` at a `std::list` hits a wall:

```text
=== std::sort 要求 random_access_iterator ===
  vector::iterator 是 random-access? 是
  list::iterator   是 random-access? 否
```

A `list` iterator tops out at bidirectional, never reaches random_access, so `std::sort` is out of the question. Does that mean a linked list cannot be sorted at all? It can — it just goes its own way: the member function `list::sort()`, which runs a merge sort internally and fits linked lists naturally (merging needs no random access, only walking both ways and splitting), with the same O(n log n) complexity:

```text
  vector 用 std::sort 后: 1 1 2 3 4 5 6 9
  list 用 list::sort() 后: 1 1 2 3 4 5 6 9
```

This is in fact a fairly common trap: beginners get into the habit of calling `std::sort(c.begin(), c.end())` on whatever container they hold, and on a `list` it does not compile. Remember one line — **algorithms pick iterators, not containers; whichever level of iterator a container provides decides which generic algorithms it can use**.

## category Also Quietly Affects Performance: Compile-Time Tag Dispatch

category governs not only "whether it can be used" but also "how fast it runs". Look at `std::distance`, which returns the distance between two iterators: the answer is the same for everyone, but the complexity is not:

```text
=== std::distance(begin, end)（值相同，复杂度不同）===
  vector(10): 10   [random-access -> O(1)]
  list(10):   10   [bidirectional -> O(n)]
```

Both lines hold 10 elements, yet the `vector` line is O(1) and the `list` line is O(n). Where is the difference? `vector`'s iterator is random_access, so `std::distance` simply computes `last - first`, done in one step; `list` only reaches bidirectional, so all it can do is dutifully `++` from head to tail — one step per element.

How is this kept completely transparent to the caller while costing zero runtime overhead? Through a classic template technique in C++ — tag dispatch. Every iterator type carries a "category tag", which you can retrieve through `std::iterator_traits<It>::iterator_category`; internally, `std::distance` picks different function overloads by that tag: the random_access version does subtraction, the other versions loop. This choice happens at **compile time**; at runtime, the step "first check the category" simply does not exist. `std::advance`, `std::iter_swap`, and a whole pile of facilities work this way.

::: warning A common pitfall
On non-random-access containers such as `list` and `set`, any operation that internally leans on "computing a distance" or "jumping n steps" (for instance `std::distance`, `std::advance(it, n)`) is O(n). Do not toss these around as constant-time conveniences; once the data volume grows, the true cost shows its face.
:::

## The C++20 Perspective: Moving Requirements from Documentation into the Type System

Finally, a word about what C++20 changed. Before concepts existed, an algorithm's requirements on iterators could only be written in documentation ("requires ForwardIterator"), and the compiler did not check them — pass in an iterator that falls short, and what came back was a long string of template instantiation errors, from which it was hard to tell what exactly went wrong.

C++20 moves these requirements into the type system with concepts: `std::forward_iterator`, `std::random_access_iterator`, and the rest are themselves compile-time-checkable predicates. The reason the table above could be printed by code is precisely that concepts turned "requirements in the documentation" into "facts you can check at compile time". We can even pin down template parameters directly in our own code with `static_assert(std::random_access_iterator<It>);` — pass the wrong type and the error fires at the call site, with far clearer information. That `print_row` template in the online demo above is, in effect, using concepts to "measure the level" of iterators.

## Summary

We have followed iterators and their categories from end to end; let us gather the key conclusions:

- Iterators are the generalization of pointer usage, the unified interface layer between containers and algorithms; algorithms recognize iterators, not specific containers.
- Iterators come in strength tiers (categories): input → forward → bidirectional → random_access → contiguous (the strongest, from C++20), decided by the data structure itself.
- category determines two things: which generic algorithms can be used (falling short means a compile failure), and the complexity of certain operations (via compile-time tag dispatch, with zero runtime overhead).
- Two high-frequency traps: `std::sort` requires random_access, so `list` cannot use it (switch to `list::sort()`); `std::distance` / `std::advance` are O(n) on non-random-access containers.

In the next article we go on to iterator adapters (`reverse_iterator`, `insert_iterator`, and the like) — how to use ready-made tools to "retrofit" iterators with new behavior.

## References

- [cppreference: Iterator library](https://en.cppreference.com/w/cpp/iterator) — the overview of iterators and the category definitions
- [cppreference: std::iterator_traits](https://en.cppreference.com/w/cpp/iterator/iterator_traits) — the foundation of `iterator_category` and tag dispatch
- [cppreference: std::distance](https://en.cppreference.com/w/cpp/iterator/distance) — the official statement of how complexity varies with category
- [cppreference: std::contiguous_iterator (C++20)](https://en.cppreference.com/w/cpp/iterator#Iterator_concepts) — C++20 iterator concepts and contiguous, the strongest tier
