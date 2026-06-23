---
chapter: 7
cpp_standard:
- 11
- 17
- 20
- 23
description: 'Combine the sequential and associative containers covered in Volume
  3 into a decision map: we will analyze them along three dimensions—operation complexity,
  memory locality, and iterator invalidation rules—and include a decision tree. We
  will also clarify the pitfalls of choosing the wrong container.'
difficulty: intermediate
order: 1
platform: host
prerequisites:
- array：编译期固定大小的聚合容器
reading_time_minutes: 11
related:
- vector 深入：三指针、扩容与迭代器失效
- deque、list 与 forward_list：vector 之外的三个选择
- map 与 set 深入
- unordered_map 与 set 深入
- span：非拥有的连续视图
tags:
- host
- cpp-modern
- intermediate
- 容器
- 内存管理
title: 'Container Selection Guide: Choosing the Right Container by Operation, Memory,
  and Invalidation Rules'
translation:
  source: documents/vol3-standard-library/containers/01-container-selection-guide.md
  source_hash: d6c0140e79cd61f1773cd5c372b8cbdc497fc918f70e60e3fa0b64f75a7169f2
  translated_at: '2026-06-16T06:08:43.136945+00:00'
  engine: anthropic
  token_count: 1849
---
# Container Selection Guide: Picking the Right Container via Operations, Memory, and Invalidation Rules

## The Goal: Choosing the Wrong Container Hides Performance Bugs

Volume 3 dissected the major containers one by one—`array`, `vector`, `deque`/`list`/`forward_list`, `map`/`set`, `unordered_map`/`unordered_set`, and `span`. Each article focused on "what this container looks like internally and why it is designed this way." This article takes the opposite approach: standing from the perspective of "I have a pile of data to store, which one should I actually pick?" and putting them on the same table for comparison. Choosing the wrong container rarely crashes immediately; it just makes your program slow, causes references to fail mysteriously, and triggers repeated reallocations in hot loops. These are the hardest performance bugs to track down because the code "runs," it just runs sluggishly.

Picking a container really comes down to three things: **what operations you need to perform (complexity), how data is laid out in memory (locality), and whether your iterators remain valid after modification (invalidation rules)**. Once these three are clear, the rest is just details. We will walk through these three lines and wrap up with a decision tree.

## First, Distinguish the Two Major Camps: Sequential vs. Associative Containers

Standard library containers are first divided into two broad categories. This distinction determines the first question you ask. **Sequential containers** (`array`, `vector`, `deque`, `list`, `forward_list`) store data by "position." The order of elements in the container is the order you put them in, and you care about "inserting at which position, deleting at which position." **Associative containers** (`map`/`set` and their `unordered` versions) store data by "key." The order of elements is determined by the key (ordered) or by a hash (unordered), and you care about "what am I querying by."

Associative containers are further divided into two sub-categories. `map`/`set`/`multimap`/`multiset` are **ordered**, typically implemented as red-black trees, sorted by key. Lookup is stable `O(log n)`, and they allow range-based traversal. `unordered_map`/`unordered_set` are **unordered**, typically implemented as hash tables. Lookup is average `O(1)` but worst-case `O(n)` (when everything collides in the same bucket), and they do not support ordered traversal. In a nutshell: **Do you need to traverse in key order? If yes, use a red-black tree; if no, use a hash for average O(1)**. We tested this trade-off in [Deep Dive into map and set](06-map-set-deep-dive.md) and [Deep Dive into unordered_map and set](07-unordered-map-set-deep-dive.md).

## Complexity Cheat Sheet: Picking Containers by Operation

Spread the complexity out into a table to directly match against the operations you need to perform. Note that the table refers to the cost of the "operation itself"; positioning (finding the spot to operate) usually needs to be calculated separately.

| Container | Random Access | Insert/Delete at Head | Insert/Delete at Tail | Insert/Delete in Middle | Lookup by Key |
|-----------|---------------|-----------------------|-----------------------|--------------------------|---------------|
| `array` | O(1) | — | — | — | — |
| `vector` | O(1) | O(n) | Amortized O(1) | O(n) | — |
| `deque` | O(1) | O(1) | O(1) | O(n) | — |
| `list` | O(n) | O(1) | O(1) | O(1) (with existing iterator) | — |
| `forward_list` | O(n) | O(1) | — | O(1) (with existing iterator) | — |
| `map` / `set` | — | — | — | O(log n) | O(log n) |
| `unordered_map` / `set` | — | — | — | Average O(1) | Average O(1), Worst O(n) |

There are a few points in this table that are easily misinterpreted, so let's pull them out. The first is the "O(1) insert in middle" for `list` / `forward_list`—this O(1) only applies to the **insertion action itself** (tweaking two pointers in the list), provided you **already hold an iterator to that position**. If you have to traverse from the head to find the position, that step is O(n), making the total cost O(n). Many people see "list insert O(1)" and assume lists are good for frequent insertions/deletions, but in most "frequent modification" scenarios, the positioning cost and cache unfriendliness drag lists down to be slower than vectors. The second is the "amortized O(1)" for `vector` tail insertion—a single reallocation is indeed O(n), but amortized over N push_backs, it is constant time, so the average is O(1); just remember to use `reserve`, and you can suppress reallocations to nearly zero. The third is `deque`; head and tail insert/delete are both O(1), which looks great, but middle insert/delete is O(n) and is more expensive than `vector` (segmented structure requires moving more), so `deque` is exclusive to "queues with frequent entry/exit at both ends"; don't use it as a general-purpose container.

## Memory Locality: Continuous vs. Node-Based, The Performance Divide

The complexity table only tells you "asymptotic speed," but two containers both labeled "O(1) traversal" can differ by an order of magnitude in real speed—the gap lies in memory locality. Storage method determines how data is laid out in memory, which in turn decides if the CPU cache hits or misses.

Sequential containers fall into three tiers based on storage method. `array` and `vector` use **continuous** memory; elements are packed tightly together. During traversal, a whole cache line enters L1 together, and the prefetcher can fetch the next block. `deque` is **segmented continuous**—internally a group of fixed-size chunks. Continuous within a chunk, discontinuous between chunks, so random access requires calculating "which chunk, which index," and traversal is smooth within a chunk but stutters across chunks. `list` / `forward_list` use **node-based** storage; each element is individually new'd, strung together by pointers. They are scattered all over memory, and traversal almost always jumps to a new address, resulting in terrible cache hit rates. Associative containers are all node-based: a node for a red-black tree, or a chain of nodes in a hash bucket; their locality is inferior to continuous containers.

This gap isn't just theoretical; run it and you will understand.

```cpp
#include <chrono>
#include <cstdio>
#include <list>
#include <vector>

int main()
{
    constexpr int N = 1'000'000;
    std::vector<int> v(N);
    std::list<int> l;
    for (int i = 0; i < N; ++i) {
        v[i] = i;
        l.push_back(i);
    }

    volatile long long sink = 0;

    auto t0 = std::chrono::high_resolution_clock::now();
    long long sv = 0;
    for (auto x : v) { sv += x; }
    sink += sv;
    auto t1 = std::chrono::high_resolution_clock::now();

    long long sl = 0;
    for (auto x : l) { sl += x; }
    sink += sl;
    auto t2 = std::chrono::high_resolution_clock::now();

    auto us_v = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    auto us_l = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
    std::printf("vector 遍历 %lld us, list 遍历 %lld us, list 慢 %.2fx\n",
                us_v, us_l, us_v ? (double)us_l / us_v : 0.0);
    return 0;
}
```

```bash
g++ -std=c++20 -O2 -o /tmp/cache_bench /tmp/cache_bench.cpp && /tmp/cache_bench
```

Running a benchmark shows that iterating over a `vector` is several times faster than a `list` (the exact factor depends on the machine and cache size, but we are talking about orders of magnitude, not single-digit percentages). Both traversals are technically $O(n)$ with $O(1)$ increments, but `vector`'s contiguous memory maximizes cache utilization, whereas `list` requires a separate memory fetch for every node. This is the fundamental reason for "why default to `vector`": in the vast majority of "store a bunch of data and iterate" scenarios, the cache dividends from contiguous memory far outweigh the insertion overhead saved by linked lists. **Only when you genuinely need frequent insertions/deletions at known positions, and the cost of those modifications significantly outweighs traversal costs, can `list` potentially win**—this condition is far stricter than intuition suggests.

## Iterator Invalidation Cheat Sheet: Can I Still Use This Reference?

The third dimension is iterator invalidation. You obtain an iterator or reference, then insert or erase elements from the container. Can that iterator still be used? This directly determines whether you can "erase while iterating" or "store a reference for later use". The table below summarizes the "Iterator invalidation" sections from cppreference and is authoritative enough to be worth memorizing.

| Container | Insertion (`insert` / `push`) | Erasure (`erase` / `pop`) |
|-----------|------------------------------|---------------------------|
| `vector` / `string` | All invalidated if reallocation occurs; otherwise, iterators at and after the insertion point are invalidated | Erasure point and all subsequent iterators are invalidated |
| `deque` | **All invalidated** | **All invalidated** |
| `list` / `forward_list` | Never invalidated | Only the erased element is invalidated |
| `map` / `set` etc. | Never invalidated | Only the erased element is invalidated |
| `unordered_map` / `set` etc. | Invalidated if rehash occurs; otherwise never invalidated | Only the erased element is invalidated |

Pay close attention to the row for `deque`. Many people treat `deque` as a "`vector` with $O(1)$ head/tail operations", but while `vector` only invalidates iterators after the erasure point when no reallocation happens, **any `erase` operation on a `deque` invalidates all iterators**—this is a consequence of its segmented structure shifting internal block pointers. If you "store a `deque` iterator and then `erase`" in your code, you will almost certainly hit a bug. In contrast, node-based containers (`list`, `map`, `set`, and their `unordered` variants) offer a major advantage: **insertion never invalidates iterators, and erasure only invalidates the iterator to the removed element**. This makes them naturally suited for "erasing by iterator while traversing" or "holding long-term references to elements".

There is also a detail specific to `unordered` containers: rehashing. When the load factor exceeds `max_load_factor` (default 1.0), an `unordered_map` will rehash (expand buckets). This invalidates all iterators (but references and pointers are **not** invalidated, as explicitly guaranteed by the standard). The countermeasure is to call `reserve(n)` beforehand to allocate enough buckets, which avoids repeated rehashing in hot loops and prevents sudden iterator invalidation.

## Selection Decision Tree

Let's combine these three dimensions into a decision tree, starting with the most important questions.

The first cut is "Is the size known at compile time?": If yes and constant, use `array` directly—zero heap allocation, `constexpr` capable, saves RAM by placing data in the static storage area, and nothing is cheaper. If no or variable length, proceed to the second cut. The second cut is "Is it key-based lookup?": If yes, go to the associative container branch—use `map`/`set` ($O(\log n)$) if you need ordered traversal by key, or `unordered_map`/`unordered_set` (average $O(1)$) if you just need fast lookup (remember to `reserve`). If not key-based, go to the sequence container branch. The third cut is "Where do insertions/deletions happen frequently?": Frequent at both ends, `deque`; only growth at the end, `vector` (be sure to `reserve`); frequent at known middle positions and no random access needed, `list`; if none of the above apply, default to `vector`.

```text
大小编译期已知且不变?
├─ 是 → array
└─ 否
   ├─ 按键查找?
   │  ├─ 要按 key 有序遍历 → map / set           (O(log n))
   │  └─ 只要平均 O(1) 查找   → unordered_map/set (记得 reserve)
   └─ 按位置存
      ├─ 频繁头尾进出     → deque
      ├─ 主要尾部增长     → vector (+ reserve)
      ├─ 已知位置频繁增删 → list (确认定位+cache 不是瓶颈)
      └─ 其余             → vector (默认)
```

Here are two additional points. First, if we just need to "borrow for a while" and don't want to transfer ownership, use `span`—it is a "unified read-only view for arrays/vectors/C arrays" and the standard for zero-copy argument passing. See [Deep Dive into span](08-span.md) for details. Second, since C++23, we have new options: if we want an "ordered + cache-friendly" map, look at `flat_map` (backed by a sorted vector); if we want a variable-length container with "fixed capacity and never heap-allocates," look at C++26's `inplace_vector`—we will cover these two in [New Standard Containers](10-new-containers-cpp23-26.md).

## Common Pitfalls

Let's list the frequent mistakes so we can self-check when picking containers. First, **"I use list because of frequent insertions/deletions"**—this ignores the cost of positioning and cache unfriendliness. In the vast majority of cases, a `vector` combined with `erase` is actually faster. `list` is only worth it when you truly hold a large number of iterators for a long time, and insertions/deletions far outnumber traversals. Second, **not reserving for unordered containers**—inserting N elements without `reserve(N)` triggers multiple rehashes. Every rehash re-hashes all elements, wasting cycles on the hot path. Third, **repeated `push_back` on vector without reserve**—similarly, expansion moves the entire block; a single `reserve` eliminates most copies. Fourth, **passing references across containers without checking invalidation rules**—especially storing iterators to a `deque` then modifying the container, or iterating over a `vector` while erasing without updating the iterator. The compiler won't warn you about these bugs; they crash at runtime.

## Wrapping Up

When picking a container, clarify three things first: operation complexity, memory locality, and iterator invalidation. If these align, you are 90% there. For details (exception safety, custom allocators, heterogeneous lookup), refer to the deep-dive articles for each container. A simple but useful default: **when in doubt, just use `vector`**. It is contiguous, has amortized O(1) push-back, and the most complete interface. It is the safest bet with the broadest coverage. Wait until you measure that it is actually a bottleneck before switching. In the next article, we will look at container adapters—`stack`, `queue`, and `priority_queue`. These aren't new containers, but interface wrappers that "package" underlying containers into stacks, queues, or heaps.

Want to try it out and see the results immediately? Open the online example below (you can run it and view the assembly):

<OnlineCompilerDemo
  title="Container Selection: Indexed vs. Keyed"
  source-path="code/examples/vol3/01_container_selection.cpp"
  description="Different operation costs for sequential containers (vector/list) vs. associative containers (map/unordered_map), reflecting the decision tree"
  allow-run
/>

## References

- [Container library overview (includes iterator invalidation rules) — cppreference](https://en.cppreference.com/w/cpp/container)
- [Container iterator invalidation rules (by operation) — cppreference](https://en.cppreference.com/w/cpp/container#Iterator_invalidation)
- [std::vector Iterator invalidation section — cppreference](https://en.cppreference.com/w/cpp/container/vector#Iterator_invalidation)
