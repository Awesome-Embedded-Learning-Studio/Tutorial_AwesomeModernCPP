---
chapter: 7
cpp_standard:
- 11
- 20
description: 'Deep dive into iterator categories: iterators are the generalization
  of pointers and the common interface between containers and algorithms. We explain
  how the five hierarchy levels (including C++20 `contiguous`) determine which algorithms
  can be used, how compile-time tag dispatching impacts `std::distance` performance,
  and why `std::sort` cannot be used with `std::list`.'
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
title: 'Iterator Basics and Categories: How Containers and Algorithms Interconnect'
translation:
  source: documents/vol3-standard-library/40-iterator-basics-and-categories.md
  source_hash: 71258af1cf6dad2060c013b74c1b4d2d7355575642e6fd4da0b2fc5dc353b672
  translated_at: '2026-06-16T04:01:42.182467+00:00'
  engine: anthropic
  token_count: 1543
---
# Iterator Basics and Categories: How Containers and Algorithms Connect

We have now covered the container journey—`std::array`, `std::vector`, `std::deque`, and `std::list`—so the tools for storing data are basically here. But once we try to pass them to algorithms like `std::sort`, `std::find`, or `std::copy`, an interesting question pops up: Why does `std::sort` work fine on `std::vector` and `std::array`, but fails to compile on `std::list`? The algorithm doesn't hardcode specific containers.

The answer lies in a thin layer of generic interface between containers and algorithms—the iterator. In this post, we'll dissect iterators: what they really are, why they have "strength levels" (categories), and how these levels determine at compile time whether code runs and how fast it runs.

## What is an Iterator: Generalizing Pointer Usage

Let's go back to the most familiar concept: the pointer. Given an array, we can use `*` to dereference, `++` to advance, and `!=` to check if we've reached the end—these three moves allow us to traverse from start to finish. What an iterator does is abstract this "set of pointer behaviors": as long as a type supports dereferencing, incrementing, and comparison, it can act as an iterator. Whether it backs a contiguous array, a linked list node, or some other structure, the algorithm doesn't care.

In other words, a raw pointer is a "native iterator," while types like `std::vector<int>::iterator` and `std::list<int>::iterator` are "objects that look like pointers but are attached to their respective containers." Algorithms only recognize this unified interface, so a single `std::find` can handle all containers. This was one of the STL's most critical design decisions: **decoupling containers from algorithms and connecting them via the iterator interface**.

## Category: Iterators Have Strength Levels

"Supporting dereference and increment" is just the minimum bar. Different iterators can do very different things: some can only move forward and can only be read once; others can jump to arbitrary positions. The more operations available, the higher the "rank" of the iterator, known in the standard as the iterator category.

From weak to strong, the classic layers are as follows (the old five categories pre-C++20, plus the strongest new category added in C++20):

- **input**: Can read, can `++`, can compare equality, but only moves forward in a single pass (typical: `std::istream_iterator`).
- **forward**: Adds multi-pass capability to input (typical: `std::forward_list`).
- **bidirectional**: Adds `--`, can move backward (typical: `std::list`, `std::set`, `std::map`).
- **random_access**: Adds `+ n`, `- n`, size comparison, can jump randomly (typical: `std::vector`, `std::deque`, raw pointers).
- **contiguous** (New in C++20): Builds on random_access by guaranteeing elements are stored contiguously in memory (typical: `std::array`, `std::vector`, `std::string`, raw pointers).

There is also **output**, which is write-only and read-never, listed separately.

Just listing the hierarchy is a bit abstract. Let's use C++20 concepts to check at compile time exactly which tier various container iterators fall into. Concepts are compile-time predicates provided by C++20; if `std::random_access_iterator` is true, it means `It` satisfies all requirements for a random access iterator:

```cpp
#include <iostream>
#include <array>
#include <vector>
#include <deque>
#include <list>
#include <forward_list>
#include <set>

// C++20 iterator concepts
template<typename It>
void test_iterator_category(It) {
    std::cout << "input:          " << std::input_iterator          << '\n';
    std::cout << "forward:        " << std::forward_iterator        << '\n';
    std::cout << "bidirectional:  " << std::bidirectional_iterator  << '\n';
    std::cout << "random_access: " << std::random_access_iterator << '\n';
    std::cout << "contiguous:     " << std::contiguous_iterator     << '\n';
}

int main() {
    std::cout << "int*:\n";
    test_iterator_category(static_cast<int*>(nullptr));

    std::cout << "\nstd::array<int>::iterator:\n";
    test_iterator_category(std::array<int, 10>{}.begin());

    std::cout << "\nstd::vector<int>::iterator:\n";
    test_iterator_category(std::vector<int>{}.begin());

    std::cout << "\nstd::deque<int>::iterator:\n";
    test_iterator_category(std::deque<int>{}.begin());

    std::cout << "\nstd::list<int>::iterator:\n";
    test_iterator_category(std::list<int>{}.begin());

    std::cout << "\nstd::forward_list<int>::iterator:\n";
    test_iterator_category(std::forward_list<int>{}.begin());

    std::cout << "\nstd::set<int>::iterator:\n";
    test_iterator_category(std::set<int>{}.begin());
}
```

Running this with `g++ -std=c++20 main.cpp && ./a.out` (local GCC 16.1.1) produces:

```text
int*:
input:          1
forward:        1
bidirectional:  1
random_access: 1
contiguous:     1

std::array<int>::iterator:
input:          1
forward:        1
bidirectional:  1
random_access: 1
contiguous:     1

std::vector<int>::iterator:
input:          1
forward:        1
bidirectional:  1
random_access: 1
contiguous:     1

std::deque<int>::iterator:
input:          1
forward:        1
bidirectional:  1
random_access: 1
contiguous:     0

std::list<int>::iterator:
input:          1
forward:        1
bidirectional:  1
random_access: 0
contiguous:     0

std::forward_list<int>::iterator:
input:          1
forward:        1
bidirectional:  0
random_access: 0
contiguous:     0

std::set<int>::iterator:
input:          1
forward:        1
bidirectional:  1
random_access: 0
contiguous:     0
```

This table makes the hierarchy very clear: `int*`, `std::array`, `std::vector`, and `std::deque` light up all five categories—they are the strongest class (contiguous) that can jump randomly in memory and are stored contiguously; `std::list` and `std::set` stop at bidirectional—they can move forward and backward, but cannot `+ n` to jump; `std::forward_list` is the weakest, moving only forward. The strength isn't about "who wrote it better," but is determined by the data structure itself: linked list nodes are scattered all over memory, so you simply cannot use `+ n` to calculate the address of the nth node.

## Why Category Matters: It Determines Which Algorithms Are Available

Back to the opening question. Algorithms in the standard specify their requirements for iterator categories: `std::find` only needs input (just scan forward), `std::reverse` needs bidirectional (must move backward), and `std::sort` needs random_access (quicksort needs to jump randomly to pick a pivot and partition). These requirements aren't just documentation notes—if the passed iterator doesn't meet them, compilation fails directly.

So, trying to slap `std::sort` on `std::list` will hit a wall:

```cpp
std::list<int> l = {3, 1, 4, 1, 5, 9};
std::sort(l.begin(), l.end()); // Error: std::list iterator is not random_access
```

`std::list`'s iterator only reaches bidirectional, falling short of random_access, so `std::sort` is unusable. Does that mean linked lists can't be sorted? They can, but they take their own path—the member function `std::list::sort`, which uses merge sort internally. Merge sort is naturally suited for linked lists (it doesn't need random access, just the ability to move forward/backward and split), and it also has O(n log n) complexity:

```cpp
l.sort(); // OK: Uses merge sort internally
```

This is a common pitfall: beginners are used to calling `std::sort` on everything, but it won't compile on `std::list`. Remember this—**algorithms pick iterators, not containers; the category of iterator a container provides determines which generic algorithms it can use**.

## Category Also Secretly Affects Performance: Compile-Time Tag Dispatch

Category doesn't just govern "usability," it also governs "speed." Look at `std::distance`, which returns the distance between two iterators. It gives the same result for everyone, but the complexity differs:

```cpp
#include <iterator>
#include <vector>
#include <list>

int main() {
    std::vector<int> v(10);
    std::list<int> l(10);

    auto d1 = std::distance(v.begin(), v.end()); // O(1)
    auto d2 = std::distance(l.begin(), l.end()); // O(n)
}
```

For the same 10 elements, the `std::vector` line is O(1), while the `std::list` line is O(n). Where's the difference? `std::vector`'s iterator is random_access, so `std::distance` simply calculates `last - first` in one step. `std::list` is only bidirectional, so it must honestly increment from start to finish, taking as many steps as there are elements.

How is this done transparently to the caller with zero runtime overhead? It relies on a classic C++ template technique—tag dispatch. Every iterator type carries a "category tag" accessible via `std::iterator_traits`. `std::distance` internally selects different function overloads based on this tag: the random_access version uses subtraction, while others use a loop. This selection happens at **compile time**, so there is no runtime overhead for "checking the category." `std::advance`, `std::sort`, and many other facilities work this way.

::: warning Common Pitfall
On non-random access containers like `std::list` or `std::map`, any operation relying on "calculating distance" or "jumping n steps" (like `std::distance`, `std::advance`) is O(n). Don't treat them as constant-time operations, or they will bite you when data volumes grow.
:::

## The C++20 Perspective: Moving Requirements from Docs to the Type System

Finally, a word on changes brought by C++20. Before concepts, algorithm requirements for iterators could only be written in documentation ("requires ForwardIterator"). The compiler didn't check them—if you passed an iterator that didn't meet the requirements, you'd get a long string of template instantiation errors that made it hard to see what went wrong.

C++20 moves these requirements into the type system using concepts: `std::input_iterator`, `std::random_access_iterator`, and others are compile-time checkable predicates. The reason we could print that table earlier is precisely because concepts turned "documentation requirements" into "facts checkable at compile time." We can even use `requires` directly in our code to constrain template parameters, causing errors at the call site with much clearer messages—the `test_iterator_category` template above is essentially using concepts to "measure the rank" of an iterator.

## Summary

We've gone through iterators and their categories from start to finish. Let's wrap up with a few key conclusions:

- Iterators are a generalization of pointer usage, serving as the unified interface between containers and algorithms. Algorithms recognize iterators, not specific containers.
- Iterators are ranked by strength (category): input → forward → bidirectional → random_access → contiguous (strongest in C++20), determined by the data structure itself.
- Category determines two things: which generic algorithms can be used (compilation fails if requirements aren't met), and the complexity of certain operations (via compile-time tag dispatch with zero runtime overhead).
- Two high-frequency pitfalls: `std::sort` requires random_access, so it doesn't work on `std::list` (use `list::sort` instead); `std::distance` / `std::advance` are O(n) on non-random access containers.

In the next post, we'll continue with iterator adapters (`std::reverse_iterator`, `std::move_iterator`, etc.) to see how to use existing tools to "modify" iterator behavior.

## Reference Resources

- [cppreference: Iterator library](https://en.cppreference.com/w/cpp/iterator) — Iterator overview and category definitions
- [cppreference: std::iterator_traits](https://en.cppreference.com/w/cpp/iterator/iterator_traits) — The cornerstone of `std::iterator_traits` and tag dispatch
- [cppreference: std::distance](https://en.cppreference.com/w/cpp/iterator/distance) — Official documentation on complexity varying by category
- [cppreference: std::contiguous_iterator (C++20)](https://en.cppreference.com/w/cpp/iterator#Iterator_concepts) — C++20 iterator concepts and the strongest category, contiguous
