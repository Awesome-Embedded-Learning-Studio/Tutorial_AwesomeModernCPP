---
chapter: 0
cpp_standard:
- 11
- 17
- 20
description: "Get big O straight, tell single-shot from amortized cost, and land it on flat_map: O(log n) lookup, O(n) insert, O(N lgN) range construction, plus real measurements that show what the shift actually costs"
difficulty: intermediate
order: 2
platform: host
prerequisites:
- flat_map prerequisite (I): std::vector internals and growth
reading_time_minutes: 10
related:
- flat_map in practice (III): lookup and insert
tags:
- host
- cpp-modern
- intermediate
- 容器
- map
- 优化
title: "flat_map prerequisite (II): complexity and amortized analysis"
---
# flat_map prerequisite (II): complexity and amortized analysis

Back in [pre-00](./pre-00-flat-map-ordered-assoc-container-intro.md) we threw out the line "flat_map is `O(log n)` lookup, `O(n)` insert", and [pre-01](./pre-01-flat-map-vector-internals-and-growth.md) called vector `push_back` amortized `O(1)`. Hiding inside those two sentences is a distinction you can easily skip past, yet it has a real, measurable say in flat_map performance: single-shot cost and amortized cost are not the same thing. This piece takes that tool apart, because every performance conclusion we reach for flat_map downstream roots back in this kind of analysis. Once it clicks, you can judge for yourself when flat_map pays off and when reaching for it is a self-inflicted wound.

## Big-O: asymptotic complexity

Big-O describes how the cost of an operation grows with the input size `n`; constant factors and lower-order terms get dropped. The common buckets: `O(1)` is constant time, independent of `n`, like `vector::size()` reading a field; `O(log n)` is logarithmic, which is what you get when binary search on a sorted array halves the range each step; `O(n)` is linear, proportional to `n`, covering a full sweep or inserting in the middle of an array and shuffling every element after it; `O(n log n)` shows up in sorting, or when you binary-search-insert N elements one at a time; `O(n²)` is the nastier one, like inserting N elements at the head one by one, paying `O(n)` each time across N inserts.

Big-O answers "as `n` runs to infinity, who wins?" But as we flagged in [pre-00](./pre-00-flat-map-ordered-assoc-container-intro.md), big-O throws away the constant factor, and in real programs that factor, how many cycles each operation actually burns, can differ by an order of magnitude. This is exactly why flat_map beats std::map for small N: both have `O(log n)` lookup asymptotically, but flat_map lives in contiguous storage where a cache line holds several elements, while std::map's red-black tree nodes scatter across the heap and a single traversal racks up cache misses. So when you read a complexity claim, big-O is only the first half. The second half is the constant factor, and in our setting that mostly means cache behavior.

## Single-shot vs. amortized: a distinction that matters

This is the heart of the piece. The same operation has two complexity lenses: single-shot (single) looks at the worst case for doing it once; amortized looks at the average per operation over N runs in a row, spreading the occasional big spike across all N.

vector's `push_back` is the textbook example. Single-shot worst case is `O(n)`, because a resize has to move every existing element. Amortized, it is `O(1)`, because growth doubles geometrically: after a resize the next N pushes don't resize again, and that one `O(n)` spike spread over N pushes averages to a constant. The reason `push_back` feels fast day to day is that we cash this amortized check without noticing.

### flat_map's single-element insert gets no amortized discount

Here is the catch: flat_map's `insert(key, value)` does not get this deal. Recall from [pre-01](./pre-01-flat-map-vector-internals-and-growth.md) that flat_map must stay sorted, so `insert` runs `lower_bound` to find the slot (usually somewhere in the middle of the array) and then **shifts every element after that slot back by one**. That shift is a real `O(n)`, and it happens on every insert, not just occasionally.

So flat_map's single-element insert is `O(n)` single-shot and `O(n)` amortized, because every insert pays `O(n)` and there is no "occasional big spike" to spread out. Doing N single-element `insert`s in a row piles up to `O(n²)` total. That is the root of why "constructing a big flat_map by inserting one key at a time" is a trap; in 03-5 we cover how batch construction sidesteps it.

## O(log n) lookup: binary search

flat_map's lookup operations, `find`, `contains`, `lower_bound`, `equal_range`, are all `O(log n)`, all riding on binary search over a sorted array. Taking `lower_bound` as the example (flat_tree.h:1027 uses `std::ranges::lower_bound`):

```cpp
// In the sorted range [first, last), find the first position not less than key
auto it = std::ranges::lower_bound(data, key, comp);
```

Each step of binary search halves the search range, so `n` elements need at most `log₂(n)` comparisons. A million elements is roughly 20 comparisons, and each comparison hits cache (contiguous storage) in 1 to 2 cycles, so the total lookup cost is tiny. flat_map lookup is fast on two counts: it is `O(log n)`, and each comparison itself is cheap.

`find`, `contains`, `lower_bound`, and `equal_range` share their interface semantics with std::map, all inherited by flat_map from flat_tree. `find(key)` is an exact lookup, equal to `lower_bound` followed by one equality check; `contains(key)` is just `find != end`; `lower_bound(key)` gives the first position `>= key`; `equal_range(key)` gives the `[lower_bound, upper_bound)` range. The only difference is that the underlying layer swaps a tree walk for binary search.

## O(n) insert: the cost of the shift

flat_map's insert operations (`insert`/`emplace`/`operator[]`/`insert_or_assign`) all walk the same path (flat_tree.h:1060, `unsafe_emplace`): `lower_bound` finds the insertion slot in `O(log n)`, then a `vector::emplace` at that slot shifts every later element back by one, and that shift is `O(n)`. The total is dominated by the shift, landing at `O(n)`. erase works the same way (flat_tree.h:914/921, `body_.erase`): delete one element, shift everything after it forward, `O(n)`.

### Measurement: how expensive is the shift really

Saying `O(n)` is abstract, so we ran an experiment: insert at the front of a vector 100k times (`emplace(begin)`), shifting every later element on each call:

```text
100k vector::emplace(begin)  →  264 ms   (O(n²) total)
100k vector::push_back       →  0 ms      (amortized O(1))
```

Two orders of magnitude. If you treat flat_map like std::map and keep inserting in the middle, that 264ms curve is what you see. flat_map's `O(n)` insert is not a textbook warning meant to scare you; it is a wall you will actually hit.

## Range construction: O(N lg²N) → O(N lgN)

flat_map does have a cheap construction path. If you can hand it a blob of data in one shot (say, move-constructing from a `vector<pair<K,V>>`), it skips the per-element insert: append everything first, then sort and deduplicate in a single pass (`sort_and_unique`, flat_tree.h:147-149):

```text
flat_map construction (N elements):
  1. append all elements         O(N)
  2. sort_and_unique:
       std::stable_sort          O(N log N)
       unique + erase            O(N)
  total                          O(N log N)  (with spare memory; otherwise O(N log²N))
```

`stable_sort` is `O(N log N)` when spare memory is available, because it can grab a scratch buffer and do a merge; when memory is tight it degrades to `O(N log²N)`, since in-place merge pays `O(N log N)` per layer. So flat_map's batch construction is `O(N log N)`, far cheaper than the `O(N²)` of per-element insert. That is the implementation reason flat_map is strictly better for the "write once" pattern.

### sorted_unique: skip the sort

One step further: if you can guarantee the input is already sorted and duplicate-free, you can construct with the `sorted_unique_t` tag (flat_tree.h:606-646), and flat_map skips `sort_and_unique` entirely, taking ownership directly, dropping construction to `O(N)`. This is a clean specimen of zero-cost abstraction, and we save it for pre-04 and 03-4.

## Complexity summary table

flat_map's complexity story is collected in the table below, every row traceable to comments in flat_tree.h:

| Operation | Complexity | Notes |
|---|---|---|
| Lookup find/contains/lower_bound/equal_range | `O(log n)` | Binary search, cache-friendly |
| Single insert/emplace | `O(n)` | Includes shift, no amortization |
| erase(position/range) | `O(n)` | Shift |
| erase(key) | `O(n) + O(log n)` | Find then remove |
| operator[]/insert_or_assign/try_emplace | `O(n)` | Same as insert |
| Range construction (plain) | `O(N log²N)` / `O(N log N)` | Depends on spare memory |
| Range construction (sorted_unique) | `O(N)` | Skips sort_and_unique |
| reserve/shrink_to_fit | `O(n)` | Realloc, invalidates iterators |

Compared with std::map (red-black tree): lookup is `O(log n)`, asymptotically a tie with flat_map but losing on the constant factor; insert/erase is `O(log n)`, beating flat_map asymptotically. So on the asymptotic line std::map wins insert, and on the constant-factor line flat_map wins lookup. In plain terms: read-heavy, write-light, reach for flat_map; large and frequently mutated, reach for std::map.

In the next piece we look at flat_map's comparator: how it decides element ordering, and how the modern "transparent comparator" skips temporary object construction.

## References

- [cppreference: std::lower_bound (binary search)](https://en.cppreference.com/w/cpp/algorithm/lower_bound)
- [cppreference: complexity (amortized analysis)](https://en.cppreference.com/w/cpp/language/complexity)
- [Chromium `base/containers/flat_tree.h` — complexity comments](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_tree.h)
