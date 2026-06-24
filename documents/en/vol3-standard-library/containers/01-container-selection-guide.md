---
chapter: 7
cpp_standard:
- 11
- 17
- 20
- 23
description: 'Combine the sequential and associative containers covered in Volume
  3 into a decision map: categorize them by operation complexity, memory locality,
  and iterator invalidation rules, and include a decision tree to clarify the pitfalls
  of choosing the wrong container.'
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
title: 'Container Selection Guide: Choosing the Right Container Based on Operations,
  Memory, and Invalidation Rules'
translation:
  source: documents/vol3-standard-library/containers/01-container-selection-guide.md
  source_hash: 603293a987409d52c432eabe9e72f8988c41c5727fdb8a2d42ce90290811b5c2
  translated_at: '2026-06-24T00:34:27.493019+00:00'
  engine: anthropic
  token_count: 1917
---
# Container Selection Guide: Pick the Right Container via Operations, Memory, and Invalidation Rules

## The Goal: Choosing the Wrong Container Hides Performance Bugs

Volume 3 dissected the major containers one by one—`array`, `vector`, `deque`/`list`/`forward_list`, `map`/`set`, `unordered_map`/`unordered_set`, and `span`. Each article focused on "what this container looks like internally and why it is designed this way." This article flips the perspective: standing from the angle of "I have a pile of data to store, which one should I pick," we place them on the same table for comparison. Choosing the wrong container rarely crashes the program immediately; it only makes your program slow, causes references to fail mysteriously, and triggers repeated reallocations in hot loops. These are the hardest performance bugs to debug because the code "runs," it just runs frustratingly slow.

Picking a container really comes down to three things: **what operations you need to perform (complexity), how data is laid out in memory (locality), and whether iterators remain valid after modification (invalidation rules)**. Once these three are clear, the rest is just details. We will walk through these three lines and wrap up with a decision tree.

## First, Distinguish the Two Major Camps: Sequential vs. Associative Containers

Standard library containers are first divided into two broad categories. This distinction determines the first question you ask. **Sequential containers** (`array`, `vector`, `deque`, `list`, `forward_list`) store data by "position." The order of elements in the container is the order you put them in, and you care about "inserting at which position, deleting at which position." **Associative containers** (`map`/`set` and their `unordered` versions) store data by "key." The order of elements is determined by the key (ordered) or by hash (unordered), and you care about "what criteria I use to look up."

Associative containers are further divided into two sub-categories. `map`/`set`/`multimap`/`multiset` are **ordered**, implemented via red-black trees, sorted by key, lookup is stable `O(log n)`, and they support range traversal. `unordered_map`/`unordered_set` are **unordered**, implemented via hash tables, lookup is average `O(1)` but worst-case `O(n)` (when everything collides in the same bucket), and cannot be traversed in order. In a nutshell: **Do you need to traverse in sorted order by key? If yes, use a red-black tree; if no, use a hash for average O(1)**. We tested this tradeoff in the articles [Deep Dive into map and set](06-map-set-deep-dive.md) and [Deep Dive into unordered_map and set](07-unordered-map-set-deep-dive.md).

## Complexity Cheat Sheet: Picking Containers by Operation

Let's spread the complexity out into a table. When picking a container, compare it directly against the operations you need to perform. Note that the table refers to the cost of the "operation itself"; positioning (finding the location to operate on) usually counts separately.

| Container | Random Access | Insert/Delete at Head | Insert/Delete at Tail | Insert/Delete in Middle | Lookup by Key |
|-----------|---------------|-----------------------|-----------------------|--------------------------|---------------|
| `array` | O(1) | — | — | — | — |
| `vector` | O(1) | O(n) | Amortized O(1) | O(n) | — |
| `deque` | O(1) | O(1) | O(1) | O(n) | — |
| `list` | O(n) | O(1) | O(1) | O(1) (given iterator) | — |
| `forward_list` | O(n) | O(1) | — | O(1) (given iterator) | — |
| `map` / `set` | — | — | — | O(log n) | O(log n) |
| `unordered_map` / `set` | — | — | — | Average O(1) | Average O(1), Worst O(n) |

There are a few points in this table that are easily misinterpreted, so let's pull them out. The first is the "O(1) middle insertion" for `list` / `forward_list`—this O(1) only applies to the **insertion action itself** (swapping two pointers in the linked list), provided you **already hold an iterator to that position**. If you have to traverse from the head to find the position first, that positioning step is O(n), making the total cost O(n). Many people see "list insertion O(1)" and assume list is suitable for frequent insertions and deletions, but in most "frequent modification" scenarios, the positioning cost and cache unfriendliness drag list down to be slower than vector. The second is the "amortized O(1)" for `vector` tail insertion—a single reallocation is indeed O(n), but amortized over N push_backs, each operation is constant, so the average is O(1); just remember to use `reserve`, and you can suppress reallocations to nearly zero. The third is `deque`—its O(1) insertion/deletion at both ends looks great, but middle insertion/deletion is O(n) and more expensive than `vector` (segmented structure has to move more stuff), so deque is exclusive to "queues with frequent entry/exit at both ends"; don't use it as a general-purpose container.

## Memory Locality: Continuous vs. Node-based, The Performance Divide

The complexity table can only tell you "asymptotic speed," but two containers both labeled "O(1) traversal" can differ by an order of magnitude in real speed—the gap lies in memory locality. The storage method determines how data is laid out in memory, which in turn decides if the CPU cache hits or misses.

Sequential containers fall into three tiers based on storage method. `array` and `vector` use **continuous** memory; elements are placed next to each other. During traversal, an entire cache line enters L1 together, and the prefetcher can fetch the next block. `deque` is **segmented continuous**—internally it is a group of fixed-size chunks; continuous within a chunk, discontinuous between chunks. So random access requires calculating "which element of which chunk," traversal is smooth within a chunk but stutters when crossing chunks. `list` / `forward_list` use **node-based** storage; each element is new'd separately as a node, strung together by pointers. They are scattered all over memory, and traversal jumps to a new address almost every time, resulting in terrible cache hit rates. Associative containers are all node-based: a node in a red-black tree, or a string of nodes in a hash bucket. Their locality is inferior to continuous containers.

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

Don't want to set up an environment? Just open the online example below to run this benchmark and see how much faster contiguous memory really is:

<OnlineCompilerDemo
  title="Contiguous vs. Node-based: Measuring vector vs list Traversal Performance"
  source-path="code/examples/vol3/01_container_cache_benchmark.cpp"
  description="Both are O(n) traversals, but the contiguous memory of vector saturates the cache, while the node-based list requires a memory access for each element—empirical tests show a several-fold difference in runtime."
  allow-run
/>

You will find that `vector` traversal is several times faster than `list` (the exact factor depends on your machine and cache size, but we are talking about orders of magnitude, not a few percent). Both traversals are O(n), and every addition is O(1), but `vector`'s contiguous memory maximizes cache utilization, whereas `list` requires a separate memory access for every node. This is the fundamental reason for "why `vector` should be the default": in the vast majority of "store a chunk of data and iterate" scenarios, the cache benefits of contiguous memory far outweigh the insertion overhead saved by linked lists. **Only when you truly need frequent insertions and deletions at known positions, and the cost of modification significantly outweighs the cost of traversal, can `list` potentially win**—and this condition is much stricter than intuition suggests.

## Iterator Invalidation Cheat Sheet: After Modifying a Container, Are Your References Still Valid?

The third dimension is iterator invalidation. You obtain an iterator or reference, then perform an insertion or deletion on the container. Can that iterator still be used? This directly determines whether you can "erase while iterating" or "store a reference for later use." The following table summarizes the "Iterator invalidation" sections for various containers from cppreference. It is authoritative and worth memorizing.

| Container | Insertion (insert / push) | Erasure (erase / pop) |
|-----------|---------------------------|-----------------------|
| `vector` / `string` | All invalidated if reallocation occurs; otherwise, iterators at and after the insertion point are invalidated | Iterators at and after the erase point are invalidated |
| `deque` | **All invalidated** | **All invalidated** |
| `list` / `forward_list` | Never invalidated | Only the erased element is invalidated |
| `map` / `set` etc. | Never invalidated | Only the erased element is invalidated |
| `unordered_map` / `set` etc. | Invalidated if rehash occurs; otherwise never invalidated | Only the erased element is invalidated |

Pay special attention to the row for `deque`. Many people treat `deque` as a "`vector` that supports O(1) at the head and tail," but while `vector` only invalidates iterators after the point of erasure when no reallocation happens, **any `erase` operation on a `deque` invalidates all iterators**. This is caused by `deque`'s segmented structure shifting internal block pointers. If you "store a `deque` iterator and then perform an `erase`," you will almost certainly run into issues. In contrast, the biggest advantage of node-based containers (`list`, `map`, `set`, and their `unordered` variants) is that **insertion never invalidates iterators, and erasure only invalidates the iterator to the erased element**. This makes them naturally suitable for "erasing by iterator while traversing" or "holding long-term references to elements."

There is also a detail specific to `unordered` containers: rehashing. When the load factor of an `unordered_map` exceeds `max_load_factor` (default 1.0), it rehashes (increases the bucket count). This invalidates all iterators (but references and pointers are **not** invalidated, as explicitly guaranteed by the standard). The countermeasure is to call `reserve(n)` beforehand to allocate enough buckets, which avoids repeated rehashing in hot loops and prevents sudden iterator invalidation.

## Selection Decision Tree

Let's twist these three criteria into a decision tree, starting with the question we should ask first.

The first cut is "Is the size known at compile time?": If yes and constant, use `array` directly—zero heap allocation, `constexpr` capable, saves RAM by residing in static storage, nothing is cheaper. If no or variable length, proceed to the second cut. The second cut is "Is it key-based lookup?": If yes, go to the associative container branch—if you need ordered traversal by key, use `map`/`set` (O(log n)); if you only need average O(1) lookup, use `unordered_map`/`unordered_set` (remember to `reserve`). If not key-based, go to the sequence container branch. The third cut is "Where do frequent insertions and deletions occur?": Frequent insertion/deletion at both ends, `deque`; growth only at the end, `vector` (be sure to `reserve`); frequent insertions/deletions at known middle positions and no random access needed, `list`; if none of the above apply, default to `vector`.

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

Here are two additional points. First, if we just need to "borrow for a moment" and don't want to transfer ownership, use `span`—it is a "unified read-only view for arrays/vectors/C arrays" and the standard for zero-copy parameter passing. See [Deep Dive into span](08-span.md) for details. Second, since C++23, we have new options: if we want an "ordered + cache-friendly" map, look at `flat_map` (backed by a sorted vector); if we want a variable-length container with "fixed capacity and no heap allocation," look at C++26's `inplace_vector`. We'll cover these two in the dedicated [New Standard Containers](10-new-containers-cpp23-26.md) article.

## Common Pitfalls

Let's list the high-frequency mistakes to check against when selecting containers. First, **"I use list because of frequent insertions/deletions"**—this ignores the cost of positioning and cache unfriendliness. In the vast majority of cases, a `vector` combined with `erase` is actually faster. `list` is only worth it when you genuinely hold many iterators long-term, and insertions/deletions vastly outnumber traversals. Second, **not calling `reserve` on unordered containers**—inserting N elements without `reserve(N)` triggers multiple rehashes. Each rehash re-hashes every element, wasting cycles on the hot path. Third, **repeated `push_back` on vector without `reserve`**—similarly, reallocation moves the entire block. A single `reserve` eliminates most copies. Fourth, **passing references across containers ignoring invalidation rules**—especially storing iterators to a `deque` then modifying the container, or iterating and erasing a `vector` without updating the iterator. The compiler won't warn you about these bugs; they crash at runtime.

## Wrapping Up

When choosing a container, clarify three things first: operation complexity, memory locality, and iterator invalidation. If these align, you are 90% there. For details (exception safety, custom allocators, heterogeneous lookup), refer to the deep-dive articles for each container. A simple but effective default: **when in doubt, just use `vector`**. It is contiguous, has amortized O(1) push-back, and the most complete interface. It is the safest bet with the broadest coverage. Switch only when you have measured it as a bottleneck. In the next article, we will look at container adapters—`stack`, `queue`, and `priority_queue`. These aren't new containers, but interface wrappers that turn underlying containers into stacks, queues, or heaps.

Want to try running it yourself to see the results? Check out the online example below (you can run it and view the assembly):

<OnlineCompilerDemo
  title="Container Selection: Index-based vs Key-based"
  source-path="code/examples/vol3/01_container_selection.cpp"
  description="Different operation costs between sequential containers (vector/list) and associative containers (map/unordered_map), echoing the decision tree"
  allow-run
/>

## References

- [Container library overview (with iterator invalidation rules) — cppreference](https://en.cppreference.com/w/cpp/container)
- [Container iterator invalidation rules (by operation) — cppreference](https://en.cppreference.com/w/cpp/container#Iterator_invalidation)
- [std::vector Iterator invalidation section — cppreference](https://en.cppreference.com/w/cpp/container/vector#Iterator_invalidation)
