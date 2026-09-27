---
chapter: 7
cpp_standard:
- 11
- 20
description: 'A thorough look at the three sequential-container alternatives to vector:
  deque''s segmented-contiguous double-ended structure, list''s doubly linked nodes
  and splice, and forward_list''s extreme memory frugality — plus the real trade-off
  between traversal cache behavior and front-insertion complexity.'
difficulty: intermediate
order: 5
platform: host
prerequisites:
- 'Deep Dive into vector: Three Pointers, Reallocation, and Iterator Invalidation'
reading_time_minutes: 8
related:
- 'Container Selection Guide: Picking the Right One by Operations, Memory, and Invalidation Rules'
tags:
- host
- cpp-modern
- intermediate
- 容器
title: 'deque, list, and forward_list: Three Alternatives to vector'
translation:
  source: documents/vol3-standard-library/containers/05-deque-list-forward-list.md
  source_hash: 62d31793dc2e51e2e7d56aba00cb2f0511f210d0a7714c8fdc80b4c19a77a080
  translated_at: '2026-09-26T02:11:40+00:00'
  engine: anthropic
  token_count: 4400
---
# deque, list, and forward_list: Three Alternatives to vector

## vector Is Already Good Enough — Why Do We Still Need These Three

We covered vector in [that article](03-vector-deep-dive.md): contiguous memory, O(1) random access, amortized O(1) push_back — for most scenarios it simply is the optimal answer. But it has a few blind spots: insertion at the front is O(n) (the whole block shifts over), insertion in the middle is also O(n), reallocation relocates every element, and iterators/references are invalidated by reallocation. When you run into requirements like "frequently adding things at the front" or "frequent insert/erase at known positions where iterators must not be invalidated", vector is the wrong tool. `deque`, `list`, and `forward_list` exist to fill those blind spots — with different memory layouts they buy capabilities vector cannot offer, at the price of their own weaknesses.

One sentence to remember up front: `deque` is "a vector you can insert into at both ends", `list` is "a linked list with O(1) insertion/removal in the middle", and `forward_list` is "a singly linked list that saves even more memory than list".

## deque: O(1) Insertion at Both Ends, Plus Random Access

deque (pronounced "deck", double-ended queue) resembles vector the most, but it solves vector's O(n) front-insertion problem. Its underlying storage is not one big contiguous block, but **segmented contiguity**: a control array (a set of pointers), where each pointer points to a fixed-size block (a chunk); elements live in these blocks, and each block is contiguous inside.

```cpp
// Simplified skeleton of deque's segmented contiguity (standard-library internals; details differ by vendor)
struct Deque {
    std::vector<Block*> control;   // the control array; each entry points to one block
    // each Block is a stretch of contiguous memory holding several elements
};
// Random access: block = control[i / chunk_size], element = block[i % chunk_size]
```

This structure brings three properties. First, **push/pop at both ends are O(1)**: when the tail block fills up, append a new block; when the head fills up, add a block in front (or fill the head block from the back forward) — either way, no existing element moves. This is its biggest advantage over vector. Second, **random access stays O(1)**: `d[i]` works out which block the element sits in, then takes the offset within the block; there is just one extra "control → block" pointer dereference compared with vector, so it is slightly slower. Third, **growing does not relocate every element**: when a deque runs out of room, only the control array (a set of pointers — tiny) needs to grow, then new blocks are hung on; the addresses of existing elements do not change — far gentler than vector's reallocation (move everything, invalidate every iterator).

The price: the memory is not one contiguous block (unfriendly to scenarios that hand data to a C interface or need a contiguous buffer), and the "control array + multiple blocks" structure carries some space overhead of its own.

## list: Doubly Linked List, O(1) Mid-Sequence Insert/Erase, and splice

`list` is a doubly linked list; each node stores `{prev pointer, data, next pointer}`. Its core selling point: **insertion and erasure at a known position (once you hold an iterator) are O(1)** — just a few pointer tweaks, and no other element moves. What is more, **iterators never invalidate** (an insert/erase only affects the iterator to the node being erased itself) — something even deque and vector cannot manage.

list also has a trick nothing else can pull off: **splice**. `l1.splice(pos, l2)` "grafts" l2's node chain directly into l1; the whole operation is O(1) and copies no elements — a capability unique to linked lists, one that contiguous containers cannot offer. It fits scenarios where you "move a segment of one list into another at zero cost".

But list's weaknesses are just as deadly. First, **no random access**: there is no `operator[]`, and finding the 1000th element means walking 1000 steps from the head (O(n)). Second, **it is brutally cache-unfriendly**: nodes are scattered all over the heap, so during traversal the CPU's prefetching fails and cache misses come in droves. We will run the numbers for you below — list traversal is several times slower than vector, precisely for this reason. So the "O(1) middle insertion" advantage is often canceled out by "first spend O(n) finding the position" plus "slow traversal" — unless you genuinely hold iterators and insert/erase through them constantly, it does not necessarily pay off.

## forward_list: The Singly Linked List That Saves Every Last Byte

`forward_list` is a singly linked list; each node stores only `{next pointer, data}`, one pointer less than list. It was added in C++11 with an unambiguous goal: to match the "zero overhead" of a hand-written C singly linked list — when you only ever traverse forward and are memory-sensitive (embedded systems, say), there is no reason to pay one extra pointer for backward capability you never use.

The costs are, naturally, no backward traversal, and **no O(1) `push_back`** (you first have to walk O(n) to the tail); only `push_front` is O(1). The interface is leaner than list's too: it **deliberately omits `size()`** — the standard requires `size()` to be O(1), a singly linked list cannot maintain that in O(1), so it simply is not provided; if you need it, count the elements yourself.

## Let's Run It: Traversal vs Front Insertion — Two Completely Opposite Faces

Just asserting that list traverses slowly and vector inserts slowly at the front is too abstract — let's simply run it. First, traversal: `vector`, `deque`, and `list` each hold a million ints; we traverse and sum them.

```cpp
#include <iostream>
#include <vector>
#include <deque>
#include <list>
#include <chrono>

int main()
{
    const int N = 1000000;
    std::vector<int> v(N);
    std::deque<int> d(N);
    std::list<int> l;
    for (int i = 0; i < N; ++i) {
        v[i] = i;
        d[i] = i;
        l.push_back(i);
    }

    volatile long long sink = 0;
    auto bench = [&](auto& c, const char* name) {
        auto t0 = std::chrono::high_resolution_clock::now();
        long long s = 0;
        for (auto x : c) {
            s += x;
        }
        sink = s;
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << name << ": "
                  << std::chrono::duration<double, std::milli>(t1 - t0).count() << " ms\n";
    };

    bench(v, "vector ");
    bench(d, "deque  ");
    bench(l, "list   ");
    return 0;
}
```

```bash
g++ -std=c++20 -O2 -o /tmp/traversal /tmp/traversal.cpp && /tmp/traversal
```

```text
vector : 0.3 ms
deque  : 0.44 ms
list   : 1.9 ms
```

(GCC 16.1.1, on my machine; the order-of-magnitude relationship is stable.) list is six times slower than vector and four times slower than deque — that is the true cost of scattered nodes and cache-unfriendliness. Because deque is segmented-contiguous, there is still locality inside each block, so it beats list handily, yet it remains a touch slower than vector with its single fully contiguous block.

Now for the opposite scenario: inserting a hundred thousand elements at the front.

```cpp
#include <iostream>
#include <vector>
#include <deque>
#include <list>
#include <chrono>

int main()
{
    const int N = 100000;
    volatile int sink = 0;

    {
        std::vector<int> v;
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < N; ++i) {
            v.insert(v.begin(), i);   // O(n) every time
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "vector front insert: "
                  << std::chrono::duration<double, std::milli>(t1 - t0).count() << " ms\n";
        sink = v.size();
    }
    {
        std::deque<int> d;
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < N; ++i) {
            d.push_front(i);   // O(1)
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "deque  front insert: "
                  << std::chrono::duration<double, std::milli>(t1 - t0).count() << " ms\n";
        sink = d.size();
    }
    {
        std::list<int> l;
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < N; ++i) {
            l.push_front(i);   // O(1)
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "list   front insert: "
                  << std::chrono::duration<double, std::milli>(t1 - t0).count() << " ms\n";
        sink = l.size();
    }
    return 0;
}
```

```bash
g++ -std=c++20 -O2 -o /tmp/front_insert /tmp/front_insert.cpp && /tmp/front_insert
```

```text
vector front insert: 246 ms
deque  front insert: 0.2 ms
list   front insert: 4.8 ms
```

Now everything flips: vector front insertion takes 246 ms while deque needs just 0.2 ms — a gap of more than a thousandfold. Every `insert(begin)` on a vector shifts all existing elements back by one, and a hundred thousand rounds of that add up to O(n²). deque's and list's front insertion are both O(1). Note that deque is even faster than list (list mallocs a node every single time, while deque just fills within a block and occasionally adds one) — which is also why deque beats list in "insert/erase at both ends" scenarios.

Put the two sets of numbers side by side and the moral is plain: **there is no silver bullet**. Traversal-heavy work? vector/deque. Frequent front or middle insertion? deque/list. Pick wrong, and the penalty is an order-of-magnitude performance gap.

## A Few Parting Words: How to Choose

| Need | Pick |
|------|----|
| Random access + mostly tail-end insert/erase | `vector` |
| Insert/erase at both ends (queues / double-ended use) | `deque` |
| Frequent insert/erase at known positions / need splice / iterators must not invalidate | `list` |
| Extreme memory frugality + forward-only traversal (embedded) | `forward_list` |

A one-line mantra: if vector can do it, use vector; when you truly need both ends, take deque; only reach for list / forward_list when you genuinely need linked-list properties. Among the sequential containers, vector is almost always the default answer — the other three are special-purpose tools you swap in "only when a concrete need exists". On the associative side we have already covered map and unordered_map; in the next article we leave containers behind and turn to the standard library's iterators and algorithms.

Want to get hands-on and see for yourself? Open the online example below (it runs, and you can inspect the assembly too):

<OnlineCompilerDemo
  title="deque / list / forward_list: O(1) Front Insertion and splice"
  source-path="code/examples/vol3/05_deque_list_forward_list.cpp"
  description="Front-insertion complexity of the three, a sizeof memory-overhead comparison, and zero-copy node moving with list::splice"
  allow-run
/>

## References

- [std::deque — cppreference](https://en.cppreference.com/w/cpp/container/deque)
- [std::list — cppreference](https://en.cppreference.com/w/cpp/container/list)
- [std::forward_list — cppreference](https://en.cppreference.com/w/cpp/container/forward_list)
- [Container Iterator Invalidation Rules, Full Table — cppreference](https://en.cppreference.com/w/cpp/container#Iterator_invalidation)
