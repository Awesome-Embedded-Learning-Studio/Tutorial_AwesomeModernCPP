---
chapter: 7
cpp_standard:
- 11
- 17
- 20
- 23
description: 'String the sequential and associative containers covered in Volume
  3 into one decision map: three threads — operation complexity, memory locality,
  and iterator invalidation rules — plus a selection decision tree, spelling out the
  pitfalls you step into by picking the wrong container.'
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 'array: An Aggregate Container with a Compile-Time Fixed Size'
reading_time_minutes: 11
related:
- 'Deep Dive into vector: Three Pointers, Reallocation, and Iterator Invalidation'
- 'deque, list, and forward_list: Three Alternatives to vector'
- 'Deep Dive into map and set: Red-Black Trees, Heterogeneous Lookup, and Node Handles'
- 'unordered_map and unordered_set Deep Dive: Hash Tables, Buckets, and Custom Hash'
- 'span: Non-owning Contiguous View'
tags:
- host
- cpp-modern
- intermediate
- 容器
- 内存管理
title: 'Container Selection Guide: Picking the Right One by Operations, Memory, and
  Invalidation Rules'
translation:
  source: documents/vol3-standard-library/containers/01-container-selection-guide.md
  source_hash: 603293a987409d52c432eabe9e72f8988c41c5727fdb8a2d42ce90290811b5c2
  translated_at: '2026-09-26T01:51:51+00:00'
  engine: anthropic
  token_count: 7800
---
# Container Selection Guide: Picking the Right One by Operations, Memory, and Invalidation Rules

## What This Article Solves: Picking the Wrong Container Is Burying a Performance Bug

Volume 3 has taken the workhorse containers apart one by one — `array`, `vector`, `deque`/`list`/`forward_list`, `map`/`set`, `unordered_map`/`unordered_set`, and `span`. Each of those articles asked "what does this container look like inside, and why is it designed that way"; this one flips the perspective: standing at "I have a pile of data to store — which one do I actually pick", we put them all on the same table and compare. Choosing the wrong container rarely crashes on the spot; it just makes your program slow, makes references die for no obvious reason, and makes the hot loop reallocate over and over — exactly the hardest kind of performance bug to hunt down, because the code "works", it just works maddeningly slowly.

Picking a container really comes down to three questions: **what operations you will run on it (complexity), how the data sits in memory (locality), and whether the iterators in your hand can still be trusted after a modification (invalidation rules)**. Get those three straight and everything else is detail. We will walk down each of these three threads, then close with a decision tree.

## First Separate the Two Camps: Sequential and Associative Containers

Standard-library containers split into two big families up front, and this split decides what your first question is. **Sequential containers** (`array`, `vector`, `deque`, `list`, `forward_list`) store data by "position": the order of elements in the container is the order you put them in, and what you care about is "at which position do I insert, at which position do I erase". **Associative containers** (`map`/`set` and their `unordered` variants) store data by "key": element order is determined by the key (ordered) or by the hash (unordered), and what you care about is "what do I look things up by".

Associative containers divide once more into two subfamilies. `map`/`set`/`multimap`/`multiset` are **ordered**: red-black trees underneath, sorted by key, lookups a steady `O(log n)`, and range traversal on top. The `unordered_map`/`unordered_set` group is **unordered**: hash tables underneath, average `O(1)` lookup but `O(n)` worst case (when everything collides into the same bucket), and no traversal in key order. The one-line test: **do you need to traverse in key order? If yes, red-black tree; if no, trade the ordering for average O(1) via hashing**. We benchmarked this trade-off in both [Deep Dive into map and set](06-map-set-deep-dive.md) and [the unordered_map and unordered_set deep dive](07-unordered-map-set-deep-dive.md).

## Complexity Cheat Sheet: Picking a Container by Operation

Spread the complexities out into a single table, and match it against the operations you need when choosing. Note that the table prices the operation itself; locating the position to operate on usually costs extra.

| Container | Random access | Front insert/erase | Back insert/erase | Middle insert/erase | Key lookup |
|-----------|--------------|-------------------|-------------------|---------------------|------------|
| `array` | O(1) | — | — | — | — |
| `vector` | O(1) | O(n) | amortized O(1) | O(n) | — |
| `deque` | O(1) | O(1) | O(1) | O(n) | — |
| `list` | O(n) | O(1) | O(1) | O(1) (iterator in hand) | — |
| `forward_list` | O(n) | O(1) | — | O(1) (iterator in hand) | — |
| `map` / `set` | — | — | — | O(log n) | O(log n) |
| `unordered_map` / `set` | — | — | — | average O(1) | average O(1), worst O(n) |

A few spots in this table are the easiest to misread, so let's single them out. First, the "middle insertion O(1)" of `list` / `forward_list` — that O(1) covers only the insertion **act itself** (a linked list relinking two pointers), and the premise is that you **already hold an iterator to that position**; if you still have to traverse from the head to find the spot, the locating step alone is O(n), and the total stays O(n). Many people see "list insertion is O(1)" and conclude list suits frequent insert/erase workloads, when in the vast majority of "frequent insert/erase" scenarios, locating cost plus cache unfriendliness drag list down slower than vector. Second, that "amortized O(1)" at the back of `vector` — a single reallocation really is O(n), but spread over N push_backs each one is still constant, so the average is O(1); as long as you remember `reserve`, the number of reallocations compresses to nearly zero. Third, `deque`: O(1) insert/erase at both ends looks lovely, but middle insert/erase is O(n), and it carries a heavier constant than vector (the segmented structure has to move more), so deque is reserved for "queues with frequent traffic at both ends" — don't use it as a general-purpose container.

## Memory Locality: Contiguous vs. Node-Based, the Great Performance Divide

A complexity table can only tell you asymptotic behavior, but two containers both labeled "O(1) traversal" can differ by an order of magnitude in real speed — the gap lives in memory locality. How a container stores decides how the data is laid out in memory, which in turn decides whether the CPU cache hits or misses.

Sequential containers come in three storage tiers. `array` and `vector` are **contiguous** memory: elements packed shoulder to shoulder, a whole cache line enters L1 together during traversal, and the prefetcher can pull in the next stretch ahead of time. `deque` is **segmented-contiguous** — internally a set of fixed-size chunks, contiguous within a chunk but not across chunks, so random access has to compute "which element of which chunk", and traversal is smooth inside a chunk but stutters when crossing one. `list` / `forward_list` are **node-based** storage: each element gets its own individually `new`-ed node, nodes are strung together by pointers, scattered all over memory — nearly every step of a traversal jumps to a fresh address, and the cache hit rate is dreadful. Associative containers are all node-based storage: a red-black tree is one node per element, a hash table hangs a string of nodes off each bucket; neither comes close to the locality of contiguous containers.

This gap is not theoretical — run it once and you will see.

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

Don't want to set up a toolchain? Open the online demo below and run this benchmark to see how much faster contiguous memory really is:

<OnlineCompilerDemo
  title="Contiguous vs. Node-Based: A Measured vector and list Traversal Benchmark"
  source-path="code/examples/vol3/01_container_cache_benchmark.cpp"
  description="Both are O(n) traversals, but the contiguous vector saturates the cache while the node-based list pays a separate memory access for each element — measured to differ by several fold"
  allow-run
/>

In practice, traversing `vector` comes out several times faster than `list` (the exact factor depends on the machine and cache size; think multiples, not a few percent) — both traversals are O(n) and each addition is O(1), but `vector`'s contiguous memory feeds the cache, while every node of `list` costs its own memory access. That is the ground-floor justification for "default to vector": in the overwhelming majority of "store a pile of data, then walk it" scenarios, the cache dividend of contiguous memory far outweighs the little shuffling cost a linked list saves. **Only when you genuinely need frequent insert/erase at known positions, and that cost clearly dominates the traversal cost, can list win** — a condition far stricter than intuition suggests.

## Iterator Invalidation Cheat Sheet: After Modifying the Container, Are the References in Your Hand Still Good

The third dimension is iterator invalidation. You obtain an iterator or a reference, then insert into or erase from the container — is that iterator still usable? This directly decides whether you can "erase while traversing" or "stash a reference for later". The table below condenses the "Iterator invalidation" sections each container has on cppreference; it is authoritative and worth committing to memory.

| Container | Insertion (insert / push) | Erasure (erase / pop) |
|-----------|---------------------------|-----------------------|
| `vector` / `string` | all invalidated on reallocation; otherwise only those after the insertion point | the erase point and everything after it invalidated |
| `deque` | **all invalidated** | **all invalidated** |
| `list` / `forward_list` | not invalidated | only the erased element's invalidated |
| `map` / `set` etc. | not invalidated | only the erased element's invalidated |
| `unordered_map` / `set` etc. | invalidated on rehash; otherwise not invalidated | only the erased element's invalidated |

The row to watch in this table is `deque`. Plenty of people use deque as "a vector with O(1) at both ends", but vector, when not reallocating, only invalidates from the erase point onward — whereas **any deque erase invalidates every iterator**, a consequence of deque's segmented structure shuffling its chunk pointers. If your code "saved a deque iterator and then erased something afterwards", you have almost certainly stepped on this. By contrast, the biggest perk of the node-based containers (`list`, `map`, `set`, and their unordered versions) is that **insertion never invalidates and erasure only invalidates the erased element**, so they natively support "erase by iterator while traversing" and "holding references to elements long-term".

One more detail exclusive to unordered containers: rehashing. When the load factor of an `unordered_map` exceeds `max_load_factor` (1.0 by default), it rehashes (grows the bucket count), and that single blow invalidates every iterator (references and pointers, however, do **not** get invalidated — the standard guarantees this explicitly). The countermeasure is calling `reserve(n)` up front to size the buckets adequately, which both avoids repeated rehashes in hot loops and avoids iterators suddenly dying on you.

## The Selection Decision Tree

Twist the three threads into one tree, and walk down from the question you should ask first.

The first cut lands on "do you know the size at compile time": if you know it and it never changes, go straight to `array` — zero heap allocation, constexpr-capable, parked in static storage saving RAM; nothing is cheaper than that. If you don't know it, or it varies, move to the second cut. The second cut lands on "is this lookup by key": if yes, take the associative branch — `map`/`set` if you need in-order traversal by key (O(log n)), `unordered_map`/`unordered_set` if average O(1) lookup is all you need (remember to reserve); if it is not lookup by key, take the sequential branch. The third cut lands on "where do you insert and erase frequently": frequent traffic at both ends, `deque`; growth only at the back, `vector` (be sure to reserve); frequent insert/erase at a known middle position with no need for random access, `list`; none of the above, `vector` by default.

```text
Is the size known at compile time and unchanging?
├─ yes → array
└─ no
   ├─ Lookup by key?
   │  ├─ Need in-order traversal by key → map / set    (O(log n))
   │  └─ Only average O(1) lookup needed → unordered_map/set (remember reserve)
   └─ Store by position
      ├─ Frequent traffic at both ends  → deque
      ├─ Mostly growth at the back      → vector (+ reserve)
      ├─ Frequent insert/erase at known positions → list (confirm locating + cache aren't the bottleneck)
      └─ Everything else                → vector (default)
```

Two addenda. First: if you only want to "borrow for a while" and not transfer ownership, use `span` — it is "a uniform read-only view over array/vector/C arrays" and the standard accessory for zero-copy parameter passing; see [the span deep dive](08-span.md). Second, new options arrive with C++23: for a map that is "sorted + cache-friendly", look at `flat_map` (a sorted vector underneath); for a variable-length container with "fixed capacity, never a heap allocation", look at C++26's `inplace_vector` — we cover those two separately in [New Standard Containers](10-new-containers-cpp23-26.md).

## The Most Common Mispicks

Here are the highest-frequency traps, listed for a quick self-check whenever you pick a container. First, **"lots of inserts and erases, therefore list"** — this ignores locating cost and cache unfriendliness; in the vast majority of cases vector plus erase is faster, and list only pays off when you truly hold large numbers of iterators long-term and insert/erase far more often than you traverse. Second, **not reserving on unordered containers** — stuffing N elements in without `reserve(N)` triggers multiple rehashes along the way, each rehashing every element, pure waste on the hot path. Third, **repeated push_back on vector without reserving** — same story: reallocation moves the whole block, and a single reserve eliminates the overwhelming majority of those copies. Fourth, **passing references around without checking invalidation rules** — especially storing a deque iterator and later modifying the container, or erasing a vector mid-traversal without updating the iterator; the compiler will not warn you about bugs like these — they only blow up at runtime.

## A Few Parting Words

When picking a container, first interrogate three things: operation complexity, memory locality, iterator invalidation. Get those three matched and you are nine times out of ten right; for the details (exception safety, custom allocation, heterogeneous lookup) go back to each container's deep-dive article. One plain but dependable default: **when in doubt, vector** — contiguous, amortized O(1) at the back, the most complete interface; it is the safest card with the widest coverage, and you switch only once you have measured it actually being the bottleneck. In the next article we move on to container adapters — `stack`, `queue`, `priority_queue` — which are not new containers but interface shells that "wrap" an underlying container into a stack/queue/heap.

Want to run it and see for yourself? Open the online demo below (it runs, and it shows the assembly too):

<OnlineCompilerDemo
  title="Container Selection: Store by Position vs. Look Up by Key"
  source-path="code/examples/vol3/01_container_selection.cpp"
  description="The differing operation costs of sequential containers (vector/list) versus associative containers (map/unordered_map), echoing the selection decision tree"
  allow-run
/>

## References

- [Container library overview (including iterator invalidation notes) — cppreference](https://en.cppreference.com/w/cpp/container)
- [Container iterator invalidation rules (by operation) — cppreference](https://en.cppreference.com/w/cpp/container#Iterator_invalidation)
- [The Iterator invalidation section of std::vector — cppreference](https://en.cppreference.com/w/cpp/container/vector#Iterator_invalidation)
