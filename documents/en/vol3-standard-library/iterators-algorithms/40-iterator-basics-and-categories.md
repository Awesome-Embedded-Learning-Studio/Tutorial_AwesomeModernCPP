---
chapter: 7
cpp_standard:
- 11
- 20
description: 'Deep Dive into Iterators: Iterators are a generalization of pointers,
  serving as the common interface between containers and algorithms. We explore the
  five hierarchy levels (including C++20 contiguous iterators) to determine which
  algorithms are applicable, how compile-time tag dispatching affects `std::distance`
  performance, and why `std::sort` cannot be used with `std::list`.'
difficulty: intermediate
order: 40
platform: host
prerequisites:
- vector 深入：三指针、扩容与迭代器失效
- array：编译期固定大小的聚合容器
reading_time_minutes: 10
related:
- 容器选择指南：按操作、内存与失效规则挑对容器
tags:
- host
- cpp-modern
- intermediate
- Ranges
title: 'Iterator Basics and Categories: How Containers and Algorithms Interact'
translation:
  source: documents/vol3-standard-library/iterators-algorithms/40-iterator-basics-and-categories.md
  source_hash: 46e17c556293a15a8b119b95339678f6b32e1497875d81f49cf0dd70c0ba1339
  translated_at: '2026-06-23T15:38:42.060207+00:00'
  engine: anthropic
  token_count: 1387
---
# Iterator Basics and Categories: How Containers and Algorithms Connect

We have covered the container journey—`array`, `vector`, `list`, `map`—the data storage crew is basically here. But once we try to hand them over to algorithms like `std::sort`, `std::find`, and `std::transform`, an interesting question pops up: Why does `std::sort` work on both `vector` and `array`, but fails to compile for `list`? The algorithm code doesn't hardcode specific containers.

The answer lies in that thin layer of generic interface between containers and algorithms—the iterator. In this post, we will dissect the iterator: what it actually is, why there are "strength levels" (categories), and how this level determines at compile-time whether code runs and how fast it runs.

## What is an Iterator: Generalizing Pointer Usage

Let's go back to the most familiar concept: the pointer. Given an array, we can use `*p` to get the value, `++p` to move forward, and `p != end` to check if we have reached the end—these three moves are enough to traverse from start to finish. What an iterator does is abstract this "set of pointer usages": as long as a type supports dereferencing, incrementing, and comparison, it can act as an iterator. The algorithm doesn't care whether it's backed by a contiguous array, a linked list node, or some other structure.

In other words, a raw pointer is a "native iterator," while `vector::iterator`, `list::iterator`, and others are "objects that look like pointers but are attached to their respective containers." Algorithms only recognize this unified interface, so a single `std::find` works across all containers. This was one of the most critical design decisions of the STL: **decoupling containers from algorithms and connecting them via the iterator interface**.

## Categories: Iterators Have Strength Levels

"Supporting dereference and increment" is just the minimum bar. Different iterators can do vastly different things: some can only move forward and can only be read once; others can jump to arbitrary positions. The more operations available, the higher the "rank" of the iterator, which the standard calls the iterator category.

From weak to strong, the classic layers are as follows (the old five categories pre-C++20, plus the strongest category added in C++20):

- **input**: Can read, `++`, and compare equality, but only moves forward in a single pass (typical: `istream_iterator`).
- **forward**: Adds multi-pass traversal on top of input (typical: `forward_list`).
- **bidirectional**: Adds `--`, allowing backward movement (typical: `list`, `set`, `map`).
- **random_access**: Adds `+n`, `[]`, and comparison, allowing random jumps (typical: `vector`, `deque`, raw pointers).
- **contiguous** (Added in C++20): On top of random_access, guarantees elements are stored contiguously in memory (typical: `vector`, `array`, `string`, raw pointers).

There is also **output**, which is write-only and read-only, listed separately.

Describing layers is a bit abstract. Let's directly use C++20 concepts to check at compile-time which category various container iterators fall into. A concept is a compile-time predicate provided by C++20; if `std::random_access_iterator<T>` is true, it means `T` meets all requirements of a random access iterator. The approach is straightforward: write a `print_row` template that checks five predicates—`input_iterator`, `forward_iterator`, `bidirectional_iterator`, `random_access_iterator`, `contiguous_iterator`—for each container's iterator, and prints a row of Yes/No. Click the online demo below to run it and see the actual results:

<OnlineCompilerDemo
  title="Measuring Iterator Levels with C++20 Concepts"
  source-path="code/examples/vol3/40_iterator_categories.cpp"
  description="print_row checks five concept predicates for iterators of vector/array/string/raw pointer/list/set/forward_list, printing a table of Yes/No to show strength levels clearly"
  allow-run
/>

The result makes the hierarchy very clear: `vector`, `array`, `string`, and raw pointers light up all five, making them the strongest class (contiguous) that can jump randomly in memory and are stored contiguously; `list` and `set` stop at bidirectional—they can move back and forth but cannot `it + 5` to jump; `forward_list` is the weakest, moving only forward. The strength isn't about "who wrote it better," but is determined by the data structure itself: linked list nodes are scattered all over memory, so you simply cannot calculate the address of the nth node with `it + n`.

## Why Category Matters: It Determines Which Algorithms Are Available

Back to the opening question. The standard specifies the iterator category requirements for algorithms: `std::find` only needs input (just scan forward), `std::reverse` needs bidirectional (must go backward), and `std::sort` needs random_access (quicksort needs random jumps to pick a pivot and partition). These requirements aren't just documentation notes—if the passed iterator doesn't meet them, compilation fails directly.

So, applying `std::sort` to `std::list` will hit a wall:

```text
=== std::sort 要求 random_access_iterator ===
  vector::iterator 是 random-access? 是
  list::iterator   是 random-access? 否
```

`std::list` iterators are only bidirectional, not random access, so we cannot use `std::sort`. Does this mean linked lists cannot be sorted? They can, but they take a different approach—the member function `list::sort()`. Internally, it uses merge sort, which is naturally suited for linked lists (merge sort does not require random access, only the ability to traverse forward and backward and split the list). The complexity remains O(n log n):

```text
  vector 用 std::sort 后: 1 1 2 3 4 5 6 9
  list 用 list::sort() 后: 1 1 2 3 4 5 6 9
```

This is actually a common pitfall: beginners are used to calling `std::sort(c.begin(), c.end())` on any container, but it fails to compile on a `list`. Remember this rule—**algorithms choose iterators, not containers; the category of iterator a container provides determines which generic algorithms it can use**.

## Category also secretly affects performance: compile-time tag dispatching

Category doesn't just dictate "usability," it also dictates "speed." Consider `std::distance`. It returns the distance between two iterators, yielding the same result for all, but the complexity varies:

```text
=== std::distance(begin, end)（值相同，复杂度不同）===
  vector(10): 10   [random-access -> O(1)]
  list(10):   10   [bidirectional -> O(n)]
```

With ten elements, the `vector` version is O(1), while the `list` version is O(n). What accounts for the difference? The `vector` iterator is a `random_access` iterator, so `std::distance` simply calculates `last - first` in a single step. The `list` iterator is merely `bidirectional`, so it must honestly increment from start to finish, stepping once for every element.

How is this achieved in a way that is completely transparent to the caller and incurs zero runtime overhead? It relies on a classic C++ template technique—**tag dispatch**. Every iterator type carries a "category tag," accessible via `std::iterator_traits<It>::iterator_category`. Internally, `std::distance` selects different function overloads based on this tag: the `random_access` version uses subtraction, while the others use a loop. This selection happens at **compile time**; at runtime, the overhead of "checking the category first" does not exist. Facilities like `std::advance` and `std::iter_swap` all work this way.

::: warning Common Pitfall
On non-random access containers like `list` or `set`, any operation that relies on "calculating distance" or "jumping n steps" (such as `std::distance` or `std::advance(it, n)`) is O(n). Don't treat them as constant-time operations and use them carelessly, or their true nature will be revealed as data volume grows.
:::

## The C++20 Perspective: Moving Requirements from Docs to the Type System

Finally, a word on the changes brought by C++20. Before concepts arrived, algorithm requirements on iterators could only be written in documentation (e.g., "requires ForwardIterator"). The compiler didn't check them—if you passed an iterator that didn't meet the requirements, you'd get a long string of template instantiation errors that made it hard to see what went wrong.

C++20 uses concepts to move these requirements into the type system: `std::forward_iterator`, `std::random_access_iterator`, and others are compile-time predicates themselves. The reason we could generate that table earlier with code is precisely because concepts turn "documentation requirements" into "facts checkable at compile time." We can even use `static_assert(std::random_access_iterator<It>);` in our own code to constrain template parameters. If the wrong type is passed, the error occurs at the call site with a clear message—the `print_row` template in the online example above essentially uses concepts to "grade" the iterator.

## Summary

We've walked through iterators and their categories from start to finish. Let's recap the key takeaways:

- Iterators are a generalization of pointer usage and serve as the unified interface between containers and algorithms. Algorithms recognize iterators, not specific containers.
- Iterators are categorized by strength (category): input → forward → bidirectional → random_access → contiguous (the strongest in C++20), determined by the underlying data structure.
- The category determines two things: which generic algorithms can be used (compilation fails if requirements aren't met) and the complexity of certain operations (achieved via compile-time tag dispatch with zero runtime overhead).
- Two common pitfalls: `std::sort` requires `random_access`, so it can't be used with `list` (use `list::sort()` instead); `std::distance` / `std::advance` are O(n) on non-random access containers.

In the next post, we will continue with **iterator adapters** (like `reverse_iterator` and `insert_iterator`) and see how to use existing tools to "modify" iterator behavior.

## References

- [cppreference: Iterator library](https://en.cppreference.com/w/cpp/iterator) — Iterator overview and category definitions
- [cppreference: std::iterator_traits](https://en.cppreference.com/w/cpp/iterator/iterator_traits) — The cornerstone of `iterator_category` and tag dispatch
- [cppreference: std::distance](https://en.cppreference.com/w/cpp/iterator/distance) — Official documentation on complexity varying by category
- [cppreference: std::contiguous_iterator (C++20)](https://en.cppreference.com/w/cpp/iterator#Iterator_concepts) — C++20 iterator concepts and the strongest category, contiguous
