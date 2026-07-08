---
chapter: 1
cpp_standard:
- 17
- 20
description: "For readers comfortable with templates and performance: a fast walkthrough of flat_map's design motivation, API, and the flat_tree adapter architecture. The condensed design-guide version of the full/ series."
difficulty: advanced
order: 1
platform: host
prerequisites:
- Move semantics and perfect forwarding
- C++20 concepts and ranges
- 'flat_map prerequisite (0): ordered associative containers and std::map''s red-black tree'
reading_time_minutes: 6
related:
- 'flat_map design guide (II): step-by-step implementation'
- 'flat_map design guide (III): test strategy and performance comparison'
tags:
- host
- cpp-modern
- advanced
- 容器
- map
- 优化
title: "flat_map Design Guide (I): Motivation, API, and the flat_tree Architecture"
---
# flat_map Design Guide (I): Motivation, API, and the flat_tree Architecture

> Hands-on track. Assumes you're already comfortable with vector growth, complexity analysis, and C++20 concepts; if not, skim the [full/ prerequisites](../full/pre-00-flat-map-ordered-assoc-container-intro.md) first.

We were poking at `std::map`'s lookup path recently and the picture wasn't pretty: 32 bytes of metadata per node, one malloc per inserted key, and a lookup that walks `node = node->left_` one pointer chase at a time. Every step is a data-dependent dereference the CPU can't prefetch, so cache misses line up. Chromium ships its own answer in `//base/containers` called `flat_map`, which swaps the red-black tree for a sorted vector plus binary search. Read-heavy, write-light workloads win, and that's the whole pitch. This piece works through the motivation, the API, and the flat_tree adapter architecture. Implementation and tests come in the next two pieces.

## The problem: where `std::map` gets stuck

`flat_map` and `std::map` are both `O(log n)` lookup, asymptotically identical, and the textbook doesn't surface a difference. The difference lives entirely in the constant factor. Red-black tree nodes are scattered across the heap, so every `node = node->left_` step in a lookup is a data-dependent dereference the CPU can't prefetch, which most likely becomes a cache miss. A concrete number: for one million entries in a `map<int,int>`, `std::map` eats roughly 32 MB in node metadata alone plus one million malloc calls. `flat_map` has nearly zero overhead on that axis, with the data laid out in a single contiguous run. Same asymptotics, constant factor off by an order of magnitude. That is the entire reason `flat_map` exists.

## What the API looks like

The surface is surprisingly plain:

```cpp
template <class Key, class Mapped,
          class Compare = std::less<>,                              // transparent default
          class Container = std::vector<std::pair<Key, Mapped>>>    // non-const Key
class flat_map : public flat_tree<Key, internal::GetFirst, Compare, Container>;
```

The template parameters hide a few tradeoffs you might miss on a first read:

| Decision | Choice | Reason |
|---|---|---|
| Default comparator | `std::less<>` (transparent) | Heterogeneous lookup; `find("abc")` builds no temporary string |
| Storage | `pair<Key,Mapped>`, non-const | vector must shift and move-assign; `pair<const K,V>` can't move-assign |
| `at()` out of range | CHECK crash (not throw) | Chromium style; logic errors blow up immediately |
| `sorted_unique` constructor | tag dispatch skips sort | When data is already sorted it's O(N), zero cost |
| `extract`/`replace` | rvalue-qualified, batch rebuild | Avoids per-element O(n) shift and iterator invalidation |

Two of these deserve a closer look. `Compare` defaults to `std::less<>` rather than `std::less<Key>`. That's transparent comparison: you can call `find("abc")` on a string-keyed map without constructing a temporary `std::string`, saving one heap allocation. The other is `pair<Key, Mapped>` with a non-const Key. It looks counterintuitive (keys shouldn't change, so why not const?), but a vector has to shift and move-assign on insert and erase, and `pair<const K, V>` can't move-assign at all, which would cripple the whole vector.

`at()` out of range runs CHECK and crashes instead of throwing. That's Chromium style: a logic error shouldn't limp along, it should fail in your face right there. One thing that took a while to track down was how batch rebuilds work. The standard library's `extract` is rvalue-qualified here as `extract()&&`, paired with `replace(container_type&&)`, so you swap the underlying container in one shot and sidestep the per-element O(n) shift and the iterator-invalidation tangle that comes with it.

## flat_tree: one piece of code, two containers

The implementation hides an elegant layering that made us pause on first read. The whole core is a single class, `flat_tree<Key, GetKeyFromValue, KeyCompare, Container>`, a generic "sorted-array associative container." The real map and set are thin shells over it. `flat_map` subclasses `flat_tree<Key, GetFirst, ...>`, where the `GetFirst` policy pulls the first element out of a `pair<K, V>` to use as the key (flat_map.h:194-195). `flat_set` is even more direct: an alias of `flat_tree<Key, std::identity, ...>`, where `std::identity` uses the value itself as the key (flat_set.h:159-163).

The prettiest move is right here. A single typename-level extractor, `GetFirst` or `std::identity`, makes the same flat_tree serve as both map and set. Textbooks spin out a chapter on policy objects; this code teaches it in one line. Once you understand flat_tree, the only remaining difference between flat_map and flat_set is that one extractor line. The entirety of flat_set.h is 191 lines, and the core is essentially that line.

## Invariants and costs

The `body_` array inside flat_tree is always strictly ascending under `comp_` with no duplicates. That invariant is the foundation the whole mechanism sits on. Maintaining it splits into two phases: at construction time you run `sort_and_unique` once (stable_sort to order, unique to dedupe, erase to trim the tail), and during insertion you find the position with `lower_bound` then `emplace`. The cost breakdown:

| Operation | Complexity | Mechanism |
|---|---|---|
| find/contains/lower_bound | `O(log n)` | std::ranges::lower_bound binary search, cache-friendly |
| insert/emplace/erase | `O(n)` | vector shift, no amortization |
| operator[]/insert_or_assign/try_emplace | `O(n)` | same as insert |
| range constructor | `O(N log²N)` / `O(N log N)` | sort_and_unique |
| sorted_unique constructor | `O(N)` | skips sort, only DCHECK |

These costs mirror `std::map`'s: lookup wins on the constant factor, while insertion pays `O(n)` for the vector shift. `flat_map` isn't trying to be a general-purpose map. It bets on "read-heavy, write-light." If you have a config table, a routing table, or an enum-to-string mapping that's effectively read-only after construction, the trade pays off. If you're doing high-frequency insert and erase, go back to `std::map`. There's also an escape hatch for "my data is already sorted": the `sorted_unique` constructor tag-dispatches past the sort, and as long as DCHECK verifies the ordering, it's O(N) to come in, at zero cost.

That's the architecture and the cost model on paper. But paper clarity is one thing, and writing it out line by line surfaces things the page doesn't show: why `sort_and_unique` has to be three separate steps (stable_sort, unique, erase), how `sorted_unique`'s DCHECK avoids eating data in release builds, and where exactly the rvalue-qualified `extract()&&` pays off. The next piece opens up flat_tree's core code.

## References

- [Chromium `base/containers/flat_tree.h`](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_tree.h)
- [Chromium `base/containers/flat_map.h`](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_map.h)
- [Chromium `base/containers/README.md`: container selection guide](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/README.md)
- [flat_map prerequisite (0): ordered associative containers and std::map's red-black tree](../full/pre-00-flat-map-ordered-assoc-container-intro.md)
