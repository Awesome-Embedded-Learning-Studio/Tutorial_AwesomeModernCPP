---
chapter: 0
cpp_standard:
- 11
- 17
- 20
description: 'Sort out big O and the single-shot vs. amortized distinction, then land it on flat_map: O(lg n) lookup, O(n) insert, O(N lgN) range construction, with real measurements backing up the cost of the shift'
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'flat_map prerequisite (I): std::vector internals and growth'
reading_time_minutes: 10
related:
- 'flat_map hands-on (III): lookup and insert'
tags:
- host
- cpp-modern
- intermediate
- 容器
- map
- 优化
title: 'flat_map prerequisite (II): complexity and amortized analysis'
translation:
  source: documents/vol9-open-source-project-learn/chrome/03_flat_map/full/pre-02-flat-map-complexity-and-amortized.md
  source_hash: 13feb033f429c8d49d81b5e8bff189630d1b23f51056a9a790d550aaf3a6cc37
  translated_at: '2026-09-26T02:47:43+00:00'
  engine: anthropic
  token_count: 5000
---
# flat_map prerequisite (II): complexity and amortized analysis

Back in [pre-00](./pre-00-flat-map-ordered-assoc-container-intro.md) we tossed out the line "flat_map is `O(log n)` lookup, `O(n)` insert", and [pre-01](./pre-01-flat-map-vector-internals-and-growth.md) called vector `push_back` amortized `O(1)`. Hidden inside those two sentences is a distinction that is easy for you to miss, yet one that decides flat_map's performance in real, measurable terms: single-shot cost and amortized cost are two different things. In this piece we want to take that tool itself apart, because every performance conclusion about flat_map further down the road is rooted in this style of analysis. Once it clicks, you will naturally be able to judge when flat_map is the right call and when reaching for it is digging a pit for yourself.

## Big-O notation: asymptotic complexity

Big-O notation describes how an operation's cost grows with the input size `n`; constant factors and lower-order terms are ignored across the board. The most common buckets: `O(1)` is constant time, independent of `n`, like `vector::size()` reading a field straight off; `O(log n)` is logarithmic time, the bucket binary search on a sorted array falls into by halving the range every step; `O(n)` is linear, proportional to `n`, covering a full traversal or an insert in the middle of an array that has to shove every element behind it over; `O(n log n)` shows up in sorting, or in binary-search-inserting N elements one at a time; `O(n²)` is the nastier one, inserting N elements at the head one by one, paying `O(n)` each time across N rounds.

Big-O answers "as `n` runs to infinity, who wins?" But as we already warned in [pre-00](./pre-00-flat-map-ordered-assoc-container-intro.md), big-O throws the constant factor away, and in real programs that factor—how many cycles each operation actually burns—can differ by an order of magnitude. This is exactly the root of flat_map beating std::map at small N: both are `O(log n)` lookup asymptotically, but flat_map stores contiguously so one cache line holds several elements, while std::map's red-black tree nodes scatter across the heap, and a single pointer-hopping traversal picks up a pile of cache misses. So when you read a complexity claim, big-O is only the first half; the second half is the constant factor, which in our setting mostly means cache behavior.

## Single-shot vs. amortized: a key distinction

Here sits the core of this piece. The same operation has two complexity lenses: single-shot (single) asks what the worst case is for doing it once; amortized asks what the per-operation average is over N runs in a row, spreading the occasional big cost that pops up across those N operations.

vector's `push_back` is the textbook example. Single-shot worst case is `O(n)`—it triggers a resize and has to move every existing element; amortized it is `O(1)`, because growth doubles geometrically, and after one resize the next N pushes need no resize, so that lone `O(n)` spread over N pushes averages out to a constant per operation. The everyday feeling that `push_back` is fast is us quietly cashing in this amortized discount.

### flat_map's single-element insert gets no amortized discount

Here is the rub: flat_map's `insert(key, value)` does not get in on this deal. Recall from [pre-01](./pre-01-flat-map-vector-internals-and-growth.md): flat_map has to stay sorted, so `insert` first runs `lower_bound` to find the slot (usually landing somewhere in the middle of the array), then **shifts every element after that slot back by one**. That shift is a solid `O(n)`, and it happens on every single insert, not just once in a while.

So flat_map's single-element insert is `O(n)` single-shot and still `O(n)` amortized—every insert pays `O(n)`, so there is no "occasional big cost" that can be spread out. Run N single-element `insert`s in a row and the total cost piles up to `O(n²)`. That is the root of why "constructing a big flat_map by inserting one key at a time" is a trap; in 03-5 we will cover in detail how batch construction steers around it.

## O(lg n) lookup: binary search

flat_map's lookups—`find`, `contains`, `lower_bound`, `equal_range`—are all `O(log n)`, all riding on binary search over a sorted array. Take `lower_bound` as the example (flat_tree.h:1027 uses `std::ranges::lower_bound`):

```cpp
// Find, in the sorted array [first, last), the first position not less than key
auto it = std::ranges::lower_bound(data, key, comp);
```

Binary search halves the search range every step, so `n` elements take at most `log₂(n)` comparisons. A million elements is about 20 comparisons, and each comparison hits cache (contiguous storage) in just 1 to 2 cycles, so the total cost of a lookup is tiny. This is precisely why flat_map lookup is fast: not only is it `O(log n)`, but every single comparison is cheap in itself.

`find`, `contains`, `lower_bound`, and `equal_range` carry the same interface semantics as std::map, and flat_map inherits all of them from flat_tree—`find(key)` is an exact lookup, equal to `lower_bound` followed by one equality check; `contains(key)` is just `find != end`; `lower_bound(key)` gives the first position `>= key`; `equal_range(key)` gives the `[lower_bound, upper_bound)` range. The only difference is that underneath, the tree walk has been swapped for binary search.

## O(n) insert: the cost of the shift

flat_map's inserts (`insert`/`emplace`/`operator[]`/`insert_or_assign`) all walk the same path (flat_tree.h:1060, `unsafe_emplace`): first `lower_bound` finds the insertion slot, which is `O(log n)`; then one `vector::emplace` at that slot shoves every element behind it back by one, and that move is `O(n)`. The total complexity is dominated by the shift, landing at `O(n)`. erase works the same way (flat_tree.h:914/921, `body_.erase`): delete one element, and everything behind it shifts forward, `O(n)`.

### Measured: how expensive the shift really is

Saying `O(n)` is not tangible enough on its own, so we ran an experiment: insert at the head of a vector 100k times (`emplace(begin)`), shifting every element behind it on every single call:

```text
100k vector::emplace(begin)  →  264 ms   (O(n²) total cost)
100k vector::push_back       →  0 ms      (amortized O(1))
```

Two orders of magnitude apart. Use flat_map the way you use std::map, frequently inserting in the middle, and that 264ms curve is what you will see. flat_map's `O(n)` insert is not a textbook warning meant to scare you; it is a performance wall you will genuinely slam into.

## Range construction: O(N lg²N) → O(N lgN)

But flat_map does have a cheap construction path. If you can feed it a whole blob of data in one shot (say, move-constructing from a `vector<pair<K,V>>`), it has no need for per-element inserts—instead it appends every element first, then sorts and deduplicates in one pass (`sort_and_unique`, flat_tree.h:147-149):

```text
flat_map construction (N elements):
  1. append all elements         O(N)
  2. sort_and_unique:
       std::stable_sort          O(N log N)
       unique + erase            O(N)
  total                          O(N log N)  (with spare memory; otherwise O(N log²N))
```

`stable_sort` is `O(N log N)` when spare memory is available—it can grab a temporary buffer and do a merge; when memory falls short it degrades to `O(N log²N)`, because an in-place merge pays `O(N log N)` per level. So flat_map's batch construction is `O(N log N)`, far cheaper than the `O(N²)` of inserting one by one. That is the implementation-level reason flat_map is strictly better in the "write once" scenario.

### sorted_unique: skip the sort

One step further: if you can guarantee the input is already sorted and duplicate-free, you can construct with the `sorted_unique_t` tag (flat_tree.h:606-646), and flat_map skips `sort_and_unique` entirely—it takes over the data directly, and construction drops to `O(N)`. This is a textbook specimen of zero-overhead abstraction, and we save it for pre-04 and 03-4.

## Complexity summary table

flat_map's complexity conclusions are collected in the table below, every one of them traceable to comments in the flat_tree.h source:

| Operation | Complexity | Notes |
|---|---|---|
| Lookup find/contains/lower_bound/equal_range | `O(log n)` | Binary search, cache-friendly |
| Single insert/emplace | `O(n)` | Includes shift, no amortization |
| erase(position/range) | `O(n)` | Shift |
| erase(key) | `O(n) + O(log n)` | Find first, then remove |
| operator[]/insert_or_assign/try_emplace | `O(n)` | Same as insert |
| Range construction (plain) | `O(N log²N)` / `O(N log N)` | Depends on spare memory |
| Range construction (sorted_unique) | `O(N)` | Skips sort_and_unique |
| reserve/shrink_to_fit | `O(n)` | Realloc, invalidates iterators |

Compare sideways against std::map (red-black tree): lookup is `O(log n)`, asymptotically a tie with flat_map but losing on the constant factor; insert/erase is `O(log n)`, beating flat_map asymptotically. So on the asymptotic-complexity line, std::map wins insert; on the constant-factor line, flat_map wins lookup. In plain words: read a lot, write a little—go flat_map; big and frequently mutated—go std::map.

In the next piece we look at flat_map's comparator: how it decides element ordering, and how the modern "transparent comparator" skips constructing temporary objects.

## References

- [cppreference: std::lower_bound (binary search)](https://en.cppreference.com/w/cpp/algorithm/lower_bound)
- [cppreference: complexity (amortized analysis)](https://en.cppreference.com/w/cpp/language/complexity)
- [Chromium `base/containers/flat_tree.h` — complexity comments](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_tree.h)
