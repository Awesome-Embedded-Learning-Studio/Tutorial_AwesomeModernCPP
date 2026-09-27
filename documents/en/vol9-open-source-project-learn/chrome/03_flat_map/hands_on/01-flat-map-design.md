---
chapter: 1
cpp_standard:
- 17
- 20
description: "A fast walkthrough of flat_map's design motivation, API, and the flat_tree adapter architecture for readers with template and performance experience — the condensed design-guide edition of this series' full/ track"
difficulty: advanced
order: 1
platform: host
prerequisites:
- Move semantics and perfect forwarding
- C++20 concepts and ranges
- 'flat_map prerequisite (0): ordered associative containers and std::map''s red-black tree'
reading_time_minutes: 6
related:
- 'flat_map Design Guide (II): step-by-step implementation'
- 'flat_map Design Guide (III): test strategy and performance comparison'
tags:
- host
- cpp-modern
- advanced
- 容器
- map
- 优化
title: "flat_map Design Guide (I): motivation, API, and the flat_tree architecture"
translation:
  source: documents/vol9-open-source-project-learn/chrome/03_flat_map/hands_on/01-flat-map-design.md
  source_hash: b3bc404a6d5c7125b591db0225fd9477eabd150dae35a56c1b2d4072c9508ff8
  translated_at: '2026-09-26T03:06:14+00:00'
  engine: anthropic
  token_count: 2400
---
# flat_map Design Guide (I): motivation, API, and the flat_tree architecture

> This is the hands-on track: we assume you are already comfortable with vector growth, complexity analysis, and C++20 concepts; if not, work through the [full/ prerequisites](../full/pre-00-flat-map-ordered-assoc-container-intro.md) first.

We were reading through `std::map`'s lookup path a while back, and the further we got, the higher our blood pressure climbed: 32 bytes of metadata per node, one malloc for every key inserted, and a lookup that chases `node = node->left_` pointer by pointer — every step a data-dependent dereference the CPU cannot reach to prefetch, so cache misses queue right up. Chromium spun up its own answer in `//base/containers`, called `flat_map`: swap the red-black tree for a sorted vector plus binary search. Home turf for read-heavy, write-light workloads — as plain as that. In this piece we get the motivation, the API, and the flat_tree adapter architecture straight in our heads first; implementation and tests are left to the next two pieces, and we will not touch code details here.

## The problem: where `std::map` gets stuck

`flat_map` and `std::map` both do `O(log n)` lookup — asymptotically identical, the kind of thing a textbook cannot tell apart. The difference lives entirely in the constant factor. Red-black tree nodes are scattered across the heap, each living on its own; every `node = node->left_` step of a lookup is a data-dependent dereference the CPU cannot prefetch, which most likely means one cache miss. Here is a number to make it visceral: with one million entries of `map<int,int>`, `std::map` eats roughly 32 MB in node metadata alone, plus one million malloc calls; `flat_map` carries almost zero extra overhead on that axis, its data obediently lined up in a single row. Same asymptotics, constant factor off by an order of magnitude — that is the entire reason `flat_map` exists.

## What the API looks like

The facade is disarmingly plain:

```cpp
template <class Key, class Mapped,
          class Compare = std::less<>,                              // transparent default
          class Container = std::vector<std::pair<Key, Mapped>>>    // non-const Key
class flat_map : public flat_tree<Key, internal::GetFirst, Compare, Container>;
```

The template parameters hide a few tradeoffs we find genuinely clever — easy to miss on a first scan, so let us point them out:

| Decision | Choice | Reason |
|---|---|---|
| Default comparator | `std::less<>` (transparent) | Heterogeneous lookup; `find("abc")` constructs no temporary string |
| Storage | `pair<Key,Mapped>`, non-const | vector must shift and assign; `pair<const K,V>` is not move-assignable |
| `at()` out of range | CHECK crash (not throw) | Chromium style; logic errors blow up on the spot |
| `sorted_unique` construction | tag dispatch skips the sort | O(N) when the data is already sorted, zero cost |
| `extract`/`replace` | rvalue-qualified bulk rebuild | Sidesteps per-element O(n) shift + iterator invalidation |

Two of these deserve a closer look. `Compare` defaults to `std::less<>` rather than `std::less<Key>` — the move is called transparent comparison: call `find("abc")` on a string-keyed map and no temporary `std::string` gets constructed, saving one heap allocation. The other is that `Key` in `pair<Key, Mapped>` carries no const. It looks counterintuitive — keys are not supposed to change, so why not const? Because a vector has to shift and move-assign on insert and erase, and `pair<const K, V>` simply cannot move-assign; the whole vector would be ruined.

`at()` out of range goes straight to CHECK and crashes, no exception thrown — that is Chromium style: a logic error should not soldier on, it blows up in your face right there. And one thing that took us quite a while to track down at first: how does a bulk rebuild work? The standard library's `extract` shows up here as the rvalue-qualified `extract()&&`, paired with `replace(container_type&&)` — swap out the underlying container in one shot and dodge that whole tangle of per-element O(n) shifts and iterator invalidation.

## flat_tree: one piece of code, two containers

The implementation hides an elegant layering that stopped us cold on first read. The core is really a single class, `flat_tree<Key, GetKeyFromValue, KeyCompare, Container>`, a generic "sorted-array associative container"; the actual map and set are just thin shells over it. `flat_map` is a subclass of `flat_tree<Key, GetFirst, ...>`, where the `GetFirst` policy object digs the first element out of a `pair<K, V>` to serve as the key (flat_map.h:194-195); `flat_set` is more direct still — just an alias of `flat_tree<Key, std::identity, ...>`, with `std::identity` treating the value itself as the key (flat_set.h:159-163).

To our eye the prettiest move is right here: a single typename extractor — `GetFirst`, or `std::identity` — makes the same flat_tree serve as both map and set. Policy objects get a whole tour in the textbooks; here the lesson is taught in one line of code. Once you understand flat_tree, the only difference left between flat_map and flat_set is that one extractor line; flat_set.h runs 191 lines in total, and the core is essentially that line.

## Invariants and costs

Inside flat_tree, the `body_` array is always strictly ascending under `comp_`, with no duplicates — that is the foundation the whole mechanism sits on. Keeping it that way is a two-phase job: at construction, run one pass of `sort_and_unique` (stable_sort to order, unique to deduplicate, erase to trim the tail); on insert, find the position with `lower_bound`, then `emplace`. The costs, laid out clearly so you know where you stand:

| Operation | Complexity | Mechanism |
|---|---|---|
| find/contains/lower_bound | `O(log n)` | std::ranges::lower_bound binary search, cache-friendly |
| insert/emplace/erase | `O(n)` | vector shift, no amortization |
| operator[]/insert_or_assign/try_emplace | `O(n)` | same as insert |
| range construction | `O(N log²N)` / `O(N log N)` | sort_and_unique |
| sorted_unique construction | `O(N)` | skips the sort, only DCHECK |

You can see the shape: these costs mirror std::map's. Lookup wins on the constant factor; insertion pays `O(n)` for the vector shift. `flat_map` has no ambition to be a general-purpose map — it bets everything on "read-heavy, write-light." If what you hold is a config table, a routing table, an enum mapping — something effectively read-only once construction is done — the trade pays off; run high-frequency inserts and erases instead, and you should honestly go back to std::map. It also leaves a back door open for "my data is already sorted": the `sorted_unique` constructor tag-dispatches past the sort — as long as DCHECK verifies it, you walk in at O(N), zero cost.

That squares away the architecture and the costs. But laying it out on paper is one thing; actually typing it out line by line surfaces things the page cannot show — why `sort_and_unique` has to split into the three steps of stable_sort + unique + erase, how `sorted_unique`'s DCHECK guarantees no data gets wrongly dropped in release builds, and where exactly the rvalue-qualified `extract()&&` trick pays off. In the next piece we spread flat_tree's core code out on the table.

## References

- [Chromium `base/containers/flat_tree.h`](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_tree.h)
- [Chromium `base/containers/flat_map.h`](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_map.h)
- [Chromium `base/containers/README.md` — the container selection guide](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/README.md)
- [flat_map prerequisite (0): ordered associative containers and std::map's red-black tree](../full/pre-00-flat-map-ordered-assoc-container-intro.md)
