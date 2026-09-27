---
chapter: 0
cpp_standard:
- 11
- 17
- 20
description: "Starting from std::map's red-black tree implementation: the per-node malloc plus cache-miss pain at small N, and how flat_map takes a different road with a sorted vector plus binary search"
difficulty: intermediate
order: 0
platform: host
prerequisites:
- 'WeakPtr prerequisite (0): weak references and the lifetime puzzle'
reading_time_minutes: 10
related:
- 'flat_map hands-on (I): motivation and API design'
- 'flat_map prerequisite (I): std::vector internals and growth'
tags:
- host
- cpp-modern
- intermediate
- 容器
- map
- 优化
title: "flat_map prerequisite (0): ordered associative containers and std::map's red-black tree"
translation:
  source: documents/vol9-open-source-project-learn/chrome/03_flat_map/full/pre-00-flat-map-ordered-assoc-container-intro.md
  source_hash: 242e32fde7eb5461223b35335366ca3a824b60988184434b28a5ba02322b7375
  translated_at: '2026-09-26T02:46:39+00:00'
  engine: anthropic
  token_count: 2000
---
# flat_map prerequisite (0): ordered associative containers and std::map's red-black tree

You toss off a `std::map<std::string, Config>` for a config table and most likely never give its internals a second thought — `O(log n)` lookup, and if the textbook says that's reasonable, it's reasonable. But if this table holds just a dozen entries, gets built once at startup and never moves again, and you actually go profile it, you'll find it's a good deal slower than you'd expect. The slowness isn't in the `O(log n)` term; it lives where asymptotic complexity can't hide: every key-value you store costs its own `malloc` for a node, and a lookup hops around the heap, stepping on cache misses nearly the whole way.

Chromium stands up its own family of ordered associative containers in `//base` — `flat_map`, `flat_set` — with the approach flipped completely around: one contiguous sorted array holds all the elements, and lookup is a binary search. The asymptotic complexity is still `O(log n)`, but the data sits together, so a single CPU cache line carries a dozen-odd elements into L1 as a freebie, and the constant factor shrinks by a large chunk. There's a price, of course: insert and erase degrade to `O(n)` (the array has to shift). In this piece we crack open std::map's red-black tree and spell out exactly why flat_map takes this other road.

## Pinning down "associative container" first

The dividing line between an associative container and a sequence container, stripped to a single sentence: a sequence container is accessed by position — you want the 0th one, the 1st one; an associative container is accessed by key — you call `m.find("timeout")` and want the value tied to that key. The standard library gives you two families here, the unordered `std::unordered_map` (a hash table, `O(1)` lookup on average) and the ordered `std::map` (a red-black tree, `O(log n)` lookup).

We're only staring at the ordered kind in this piece. For one thing, flat_map itself is ordered; for another, "ordered" is an invariant with real weight: you can iterate in key order, frame out a range with `lower_bound`, run predecessor and successor queries — none of which a hash table can do; unordered is unordered. So the question "how exactly should an ordered associative container be implemented" deserves real thought: the standard library's answer is a red-black tree, Chromium's is a sorted array. Let's lay both roads out and look.

## std::map's red-black tree implementation

Under the hood, `std::map` in all three major implementations (libstdc++, libc++, MSVC) is a red-black tree, a self-balancing binary search tree. Every key-value pair occupies one tree node, which on 64-bit looks like this:

```text
struct Node {
    color      color_;       // 1 byte (red/black, for balancing)
    Node*      left_;        // 8 bytes
    Node*      right_;       // 8 bytes
    Node*      parent_;      // 8 bytes
    pair<K,V>  data_;        // your key + value
};
```

The pointers plus the color alone already put you at 25 bytes (with alignment padding, usually 32 in practice), and that's before counting your key-value. In other words, for every element you store, on top of the data itself you pay another 32 bytes of node metadata for it.

Lookup is the textbook binary search: start at the root, compare keys, smaller goes left, larger goes right. The red-black tree keeps itself balanced, tree height stays `O(log n)`, so a lookup is `O(log n)` comparisons. Judged by asymptotic complexity alone: reasonable.

Reasonable, sure — but the pit hides in the step "get the node into cache before every comparison". Red-black tree nodes are heap-allocated one by one: you `insert` once, and underneath a Node gets `new`ed. A `std::map<int,int>` with a million elements means a million heap allocations for the nodes alone, with the returned addresses scattered all over the heap. Lookup is where it bites harder: the hop `node = node->left_` from the root has to dereference an address nobody has ever touched. The CPU pipeline can't prefetch it (the target address isn't known until the previous load completes), and it isn't sitting in L1/L2 either — that hop is a cache miss, and tens to hundreds of cycles are gone just like that. `O(log n)` comparisons, each one a possible miss: that is what `std::map::find` actually pays.

## The real disease: the constant factor

Let's stop here and pick this apart, because it is the root of the whole flat_map story.

`std::map::find` is `O(log n)`, `flat_map::find` is also `O(log n)`, and the asymptotic complexity is identical. But "asymptotically the same" has never meant "equally fast" — big-O notation deliberately erases the constant factor, and the constant factor is decided by how much each comparison actually costs.

For std::map, before every comparison the node first has to be dragged from memory into cache. Nodes are scattered across the heap, so every hop is most likely a miss. The comparison itself (two ints compared, say) is finished in 1 cycle, but waiting for the node to come over from the far side of memory takes 100+ cycles — nearly the whole "cost" of the comparison is burned waiting out the cache miss; the 1 cycle that actually does the comparing is negligible.

flat_map goes the other way: all the elements sit next to each other. The CPU scoops data out of memory by the cache line (64 bytes each on x86); when you touch `data[0]`, the neighbors `data[1]`, `data[2]`, … ride along into L1 for free. Binary search does jump around as it accesses (`mid = n/2`), but some contiguous stretch is always hot, so every comparison basically hits cache and is done in 1 cycle.

So with the same `O(log n)`, in the "small to medium data volume" bracket, flat_map's constant factor can be an order of magnitude smaller than std::map's. What Chromium won by building this wheel isn't the asymptotic complexity — it's the constant factor.

## A different road: sorted array + binary search

flat_map's core idea fits in one sentence: don't use a tree — use one contiguous sorted array, and binary search for lookups.

```text
flat_map<int,std::string>:
  data_:  [ (1,"a") | (3,"c") | (7,"g") | (9,"i") | ... ]   ← one contiguous sorted vector
                    lookup via std::lower_bound (binary search, O(log n))
```

Lookup runs through `std::lower_bound`, a binary search over the sorted array, `O(log n)` — the same asymptotics as std::map, but far more cache-friendly because the data is contiguous. The price sits with insert and erase: wedge one into the middle and the whole tail has to shift back one slot, `O(n)`; that's against std::map's `O(log n)` insert. On the storage side there is just one vector, 0 extra node metadata, one contiguous allocation. That is the entire skeleton of flat_map, and it puts the classic "red-black tree vs sorted array" trade-off on the table exactly as it is: the tree trades spatial locality for `O(log n)` insert; the array trades insert complexity for spatial locality.

So when does the array win? When reads are many and writes are few.

The most typical cases are exactly config tables, lookup tables, and command dispatch tables: constructed once at startup, then basically only read from, with rare inserts or erases afterwards. Under a "write once, read many" workload like that, flat_map's `O(n)` insert happens exactly once, during construction (and even that can be batch-optimized into a single `O(N log N)` sort-and-done, see 03-4); after that it's all `O(log n)` cache-friendly lookups. std::map? Every lookup pays that cache-miss constant factor. The writes are one-shot on both sides, so it's a tie there, but the reads on flat_map are far faster — in this corner of the world it's almost pure profit.

Flip it around: if your set is large and changes constantly (say, an index that never stops growing and shrinking), flat_map's `O(n)` insert starts to hurt, and that is std::map's home turf. Chromium's own container-choosing guide draws the line just that bluntly: write once and read many, use flat_map; write many and in large volume, use std::map.

## Chromium's trade-off, and the standard library's follow-up

flat_map isn't something Chromium dreamed up out of thin air. The sorted-vector map has a decent history — Alexandrescu already gave us `Loki::AssociationVector` back in 2001 in *Modern C++ Design*, and Boost.Container has long carried `boost::flat_map`. Chromium moved the idea into `//base` in 2017 and, along the way, gave it the Chromium-style special treatment (`DCHECK`/`CHECK` validation, `raw_ptr_exclusion`, a transparent comparator by default).

There's one detail we think is worth pulling out. Chromium's flat_map squeezes keys and values into one array (`vector<pair<K,V>>`), while `std::flat_map`, which landed in C++23 (proposal P0429), goes with key-value separation (split storage) — keys and values each get their own contiguous array. Separation buys a denser cache footprint when you only walk the keys, since the values don't come along for the ride; the cost is implementation complexity, keeping two sets of containers in sync. Chromium chose the simple non-split road — the "looks better" split scheme got set aside by a heavyweight industrial user, because the small gain it buys back doesn't pay for the implementation complexity. We'll pick that disagreement apart in 03-6's performance comparison.

That's it for the foundation layer. flat_map stores its data in a vector by default, so the next step is to get a solid grip on `std::vector`'s three pointers, its growth, and its iterator invalidation — that's the prerequisite for understanding flat_map's behavior.

## References

- [Chromium `base/containers/flat_map.h`](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_map.h)
- [Chromium `base/containers/README.md` — the container-choosing guide](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/README.md)
- [cppreference: std::map (red-black tree implementation note)](https://en.cppreference.com/w/cpp/container/map)
- [P0429 — the std::flat_map proposal (C++23)](https://wg21.link/p0429)
