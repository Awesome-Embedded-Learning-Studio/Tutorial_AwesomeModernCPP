---
chapter: 0
cpp_standard:
- 11
- 17
- 20
description: "Starting from std::map's red-black tree: the per-node malloc and cache misses that hurt at small N, and how flat_map trades the tree for a sorted vector plus binary search"
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
---
# flat_map prerequisite (0): ordered associative containers and std::map's red-black tree

You write `std::map<std::string, Config>` for a config table and probably don't give the internals a second thought. `O(log n)` lookup, textbook says it's fine, it's fine. But if that table has a dozen entries, gets built once at startup, and never moves again, go profile it. It's slower than you'd guess. Not because of the `O(log n)` term, but in the place asymptotic complexity can't hide: every key-value pair costs its own `malloc`, and a lookup hops across the heap from node to node, eating cache misses the whole way.

Chromium ships its own ordered associative containers in `//base`, `flat_map` and `flat_set`, and takes the exact opposite approach. Pack every element into one contiguous sorted array, look things up by binary search. The asymptotic complexity is still `O(log n)`, but because the data sits together, a single cache line drags a dozen elements into L1 for free, and the constant factor drops by a lot. There's a cost, of course: insert and erase degrade to `O(n)` because the array has to shift. This piece opens up std::map's red-black tree and walks through why flat_map picks the other road.

## Pinning down "associative container" first

The line between an associative container and a sequence container is one sentence: a sequence container is accessed by position, you want element 0, element 1; an associative container is accessed by key, you call `m.find("timeout")` and want the value tied to that key. The standard library gives you two flavors, the unordered `std::unordered_map` (a hash table, `O(1)` average lookup) and the ordered `std::map` (a red-black tree, `O(log n)` lookup).

We're only looking at the ordered kind here. For one thing flat_map itself is ordered. For another, "ordered" is a non-trivial invariant: you can iterate keys in sorted order, carve out a range with `lower_bound`, ask for a predecessor or successor, none of which a hash table can do. Unordered is unordered. So the question "how should an ordered associative container be implemented" deserves a real answer. The standard library's answer is a red-black tree; Chromium's is a sorted array. Let's lay both out.

## How std::map is built: the red-black tree

All three major implementations (libstdc++, libc++, MSVC) build `std::map` on a red-black tree, a self-balancing binary search tree. Each key-value pair lives in its own tree node, which on a 64-bit box looks roughly like this:

```text
struct Node {
    color      color_;       // 1 byte (red/black, for balancing)
    Node*      left_;        // 8 bytes
    Node*      right_;       // 8 bytes
    Node*      parent_;      // 8 bytes
    pair<K,V>  data_;        // your key + value
};
```

The pointers and the color alone already put you at 25 bytes (with alignment padding, usually 32 in practice), and that's before your key-value. In other words, for every element you store, on top of the data itself you pay 32 bytes of node metadata.

Lookup is the textbook binary search: start at the root, compare keys, go left if smaller, right if larger. The red-black tree keeps itself balanced, so the height stays `O(log n)`, so lookup is `O(log n)` comparisons. By asymptotic complexity, reasonable.

Reasonable, except for the part nobody draws on the slide: every comparison has to get the node into cache first. Red-black tree nodes are allocated one at a time on the heap. Each `insert` does a `new Node` underneath. A million-element `std::map<int,int>` means a million heap allocations, and the addresses come back scattered all over the heap. Lookup is worse. The hop `node = node->left_` dereferences an address nobody has touched before. The CPU pipeline can't prefetch it (the target address isn't known until the previous load finishes), L1 and L2 don't have it, so that hop is a cache miss, and tens to hundreds of cycles are gone. `O(log n)` comparisons, each one a likely miss, is the actual price `std::map::find` pays.

## The real disease: the constant factor

Let's stop here and nail this down, because it's the root of the entire flat_map story.

`std::map::find` is `O(log n)`. `flat_map::find` is also `O(log n)`. The asymptotic complexity is identical. But "same asymptotically" has never meant "same speed". Big-O deliberately throws the constant factor away, and the constant factor is set by how much each comparison actually costs.

For std::map, every comparison is preceded by dragging the node out of memory and into cache. Nodes are scattered across the heap, so each hop is a probable miss. The comparison itself, two ints compared, is one cycle, give or take. Waiting for the node to arrive from memory is a hundred-plus cycles. The cost of the comparison is almost entirely the cache-miss wait; the one cycle spent actually comparing is noise.

flat_map goes the other way: every element sits next to its neighbors. The CPU pulls data from memory in cache lines (64 bytes on x86), so when you touch `data[0]`, `data[1]`, `data[2]`, and friends come along into L1 for free. Binary search does jump around (`mid = n/2`), but some contiguous stretch is always hot, so each comparison almost always hits cache and finishes in one cycle.

Same `O(log n)`, then, but in the small-to-medium data range flat_map's constant factor can be an order of magnitude smaller than std::map's. Chromium didn't build this wheel to win on asymptotic complexity. It built it to win on the constant factor.

## The other road: a sorted array plus binary search

flat_map's whole idea is one sentence: drop the tree, use a contiguous sorted array, look things up with binary search.

```text
flat_map<int,std::string>:
  data_:  [ (1,"a") | (3,"c") | (7,"g") | (9,"i") | ... ]   ← one contiguous sorted vector
                    lookup uses std::lower_bound (binary search, O(log n))
```

Lookup goes through `std::lower_bound`, a binary search over a sorted array, `O(log n)`, same asymptotics as std::map, but far more cache-friendly because the data is contiguous. The cost moves to insert and erase: push something into the middle and the whole tail shifts back by one, `O(n)`, against std::map's `O(log n)` insert. Storage is just one vector, zero extra node metadata, one contiguous allocation. That's the entire skeleton of flat_map, and it puts the classic "red-black tree vs sorted array" tradeoff on the table without dressing it up: the tree trades spatial locality for `O(log n)` insert, the array trades insert complexity for spatial locality.

So when does the array win? When reads dominate writes.

The canonical cases are config tables, lookup tables, command dispatch tables: built once at startup, then almost entirely read, with the occasional insert or erase. For a write-once-read-many workload like that, flat_map's `O(n)` insert happens exactly once, during construction (and even that can be batched into a single `O(N log N)` sort, see 03-4); after that, everything is `O(log n)` cache-friendly lookup. std::map, on the other hand, pays the cache-miss constant factor on every single lookup. Writes are a wash on both sides, one-time, but reads on flat_map are far faster. In this setting it's almost free.

Flip it around: if your set is large and churns constantly (a live index that keeps growing and shrinking), flat_map's `O(n)` insert starts to hurt, and that's std::map's home turf. Chromium's own container-choosing guide draws the line just that bluntly: write-once-read-many, reach for flat_map; write-many and large, stay with std::map.

## Chromium's call, the standard library's follow-on

flat_map isn't something Chromium invented out of thin air. The sorted-vector map has been around a while. Alexandrescu published `Loki::AssociationVector` back in 2001 in *Modern C++ Design*, and Boost.Container has carried `boost::flat_map` for years. Chromium moved the idea into `//base` in 2017 and gave it the Chromium-style treatment (`DCHECK`/`CHECK` validation, `raw_ptr_exclusion`, a transparent comparator by default).

One detail is worth pulling out. Chromium's flat_map packs keys and values together in one array (`vector<pair<K,V>>`), while the C++23 `std::flat_map` (proposal P0429) goes with split storage: keys live in one contiguous array, values in another. Split storage buys you a denser cache footprint when you only walk the keys, the values aren't tagging along; the cost is implementation complexity, you now keep two containers in sync. Chromium took the non-split, simpler path. The "looks better on paper" split design got set aside by an industrial user, because the implementation complexity it buys back doesn't pay for itself. We'll dig into that trade in 03-6's performance comparison.

That's the foundation layer. flat_map stores its data in a vector by default, so the next step is to get a firm grip on `std::vector`'s three pointers, its growth strategy, and its iterator invalidation rules. That's the prerequisite for understanding flat_map's behavior.

## References

- [Chromium `base/containers/flat_map.h`](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_map.h)
- [Chromium `base/containers/README.md`, the container-choosing guide](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/README.md)
- [cppreference: std::map (red-black tree implementation note)](https://en.cppreference.com/w/cpp/container/map)
- [P0429, the std::flat_map proposal (C++23)](https://wg21.link/p0429)
