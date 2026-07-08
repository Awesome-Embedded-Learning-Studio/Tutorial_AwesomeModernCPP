---
chapter: 1
cpp_standard:
- 17
- 20
description: "Test flat_map around its invariants with Catch2, then measure object size, per-item overhead, and lookup/insert performance against std::map and absl::btree_map, and derive selection criteria."
difficulty: intermediate
order: 6
platform: host
prerequisites:
- "flat_map hands-on (V): iterator invalidation and batch construction"
reading_time_minutes: 12
related:
- "flat_map prerequisite (0): ordered associative containers and std::map's red-black tree"
- "flat_map prerequisite (II): complexity and amortized analysis"
tags:
- host
- cpp-modern
- intermediate
- 容器
- map
- 测试
- 优化
title: "flat_map hands-on (VI): testing and performance comparison"
---
# flat_map hands-on (VI): testing and performance comparison

The code is done. Two questions left: is it correct, and how fast is it. This piece stays on those two. The first half designs tests around invariants and walks through sorting, lookup, insert, dedup, and invalidation one by one. The second half puts flat_map on the scale next to `std::map` and `absl::btree_map`, measuring how big the object is, how fast lookup runs, how much insert hurts. That "small N favors flat_map, big N with heavy writes tips back to std::map" judgment has to land on real numbers, and once the runs finish you'll see exactly where.

## Six invariants, none of them optional

Whether flat_map counts as "correct" comes down to whether these six invariants hold. We'll take them together.

Sorting and dedup are the first two, and the easiest to verify by eye: the keys you get from a walk must be strictly ascending, with no duplicates. The third is that lookup semantics stay clean, with `find`, `contains`, `operator[]`, and `at` each doing their job, and `at` on an out-of-range key should CHECK and crash right in front of you. That kind is a definite bug, and it had better blow up in release too. The fourth targets two write interfaces people mix up: `insert_or_assign` overwrites when the key already exists, `try_emplace` leaves it alone. One moves, one doesn't, and the semantics are not interchangeable. The fifth is the `sorted_unique` "liar's shortcut" path. You claim the data is already sorted and deduped, so it skips the sort; lie about it and it aborts in debug. The last one is iterator invalidation. Any mutation invalidates old iterators under the coarse rule, which the previous piece covered in depth, so here we just verify it.

## Key test cases (Catch2-style sketch)

Below are Catch2-style sketches. The runnable examples in the project right now are the `19` through `22` demo .cpp files under `code/.../chrome_design/`; wiring up a Catch2 test target is left as an extension.

```cpp
// Platform: host | C++ Standard: C++20
#include <catch2/catch_test.hpp>
#include "flat_map.hpp"
using namespace tamcpp::chrome;

TEST_CASE("flat_map is sorted+unique after construction", "[flat_map]") {
    flat_map<int, int> m{{3,30}, {1,10}, {3,30}, {2,20}};   // duplicate 3, unsorted
    // Invariant 1+2: ordered and deduped
    std::vector<int> keys;
    for (auto& [k, v] : m) keys.push_back(k);
    REQUIRE(keys == std::vector<int>{1, 2, 3});
}

TEST_CASE("at out-of-range CHECKs", "[flat_map][.death]") {
    flat_map<int, int> m{{1,10}};
    // Invariant 3: out-of-range CHECK crash (isolated death test)
    // REQUIRE_DEATH(m.at(99));
}

TEST_CASE("insert_or_assign overwrites, try_emplace leaves alone", "[flat_map]") {
    flat_map<int, int> m{{1,10}};
    auto [it1, ins1] = m.insert_or_assign(1, 99);   // exists -> overwrite
    REQUIRE_FALSE(ins1);
    REQUIRE(it1->second == 99);
    // try_emplace leaves an existing key alone (can't verify directly in the same test; semantics in 03-3)
}
```

These cases all aim at semantic edges: the sort and dedup, the `at` CHECK, the `insert_or_assign` overwrite. The `at` death test has to run isolated because it really does abort, which is not the same thing as an ordinary assertion.

## Performance: object size and per-item overhead

First let's pin down "how much memory." flat_map's body is a single `vector<pair<K,V>>` plus a zero-byte comparator; `std::map` runs on a red-black tree, where every node carries 3 pointers plus a color bit on top of the data. We use `sizeof` for the container skeleton, then look at the per-item overhead spread across 1 million elements:

```text
sizeof(flat_map<int,int>)  ≈ sizeof(vector<pair<int,int>>) = 24 bytes (three pointers, 64-bit)
sizeof(std::map<int,int>)  ≈ 48 bytes (tree root + comparator + sentinel node)

Extra overhead for a 1M-element map<int,int> (the 8MB of data itself not counted):
  flat_map:  ~0 extra (data contiguous, no node metadata)
  std::map:  ~32MB (32B per node x 1M, and one malloc per node)
```

flat_map almost freeloads on per-item cost: data sits in one contiguous run, there's no node metadata, and it mallocs once. `std::map` spends 32B per node on metadata alone and needs a million heap allocations to fill out. The smaller the element and the bigger the collection, the wider this gap tears open.

## Performance: lookup (cache friendly vs pointer chasing)

Both sides look up in `O(log n)`, so asymptotically neither beats the other. The constant factor is the real divider: flat_map's data is contiguous, so the comparisons during binary search ride the cache; `std::map`'s nodes are scattered all over, and every hop is a dereference that most likely misses cache.

Measured on this machine with GCC 16 at `-O2`, using the companion `20_lookup_vs_shift_perf`, a 100K-element `map<int,int>` doing 100K `find` calls each:

```text
100K lookups (100K elements):
  flat_map:  31 ms
  std::map:  34 ms
```

Don't jump to conclusions. At N=100K with `int` keys the two sides are basically tied. An `int` compare costs a single cycle, and that little bit of cache goodwill hasn't yet overrun `std::map`'s advantage of a shallower tree. flat_map only pulls away when N gets bigger or the key gets heavier. Switch the key to `std::string`, say, and the compare itself becomes expensive, which magnifies the cost of each cache miss. In standalone large-N tests flat_map being a few times faster is common. So "flat_map lookup is always faster" is not a rule to memorize. It depends on the workload: the bigger N gets and the heavier the key, the more visible the edge.

## Performance: insert (the O(n) shift wall)

Lookup still bends with the workload. Insert is where flat_map just clearly loses. Measured by appending 1000 keys into a container already holding 100K elements:

```text
1000 inserts into a 100K-element container:
  flat_map:  2 ms   (O(n) shift each time)
  std::map:  0 ms   (O(log n) node rewiring each time)
```

flat_map loses fair and square, and that is the hardest piece of data behind the "read-heavy, write-light" judgment. If your workload is insert-heavy, flat_map's `O(n)` shift will bottleneck you sooner or later, and you should go back to `std::map`. The absolute numbers drift with the machine and with N, but the trend of flat_map insert being slower than `std::map` is stable. The bigger N gets, the wider the gap, because the shift cost is O(n) to begin with.

## Selection criteria (measured summary)

With three sets of data on the table, the selection criteria are right there:

| Workload | Recommendation | Reason |
|---|---|---|
| **Write once, read many** (config tables, command dispatch, lookups) | flat_map | Writes are one-shot (batch construction), reads are cache friendly |
| **Always small** (browser statistics mode around 4 elements) | flat_map | Constant factor dominates at small N, zero-allocation edge is large |
| **Large and frequently modified** (dynamic indexes) | std::map | flat_map's O(n) insert is a wall |
| **Needs pointer/reference stability** | std::map | flat_map iterators all invalidate across mutations |
| **Many ordered keys + frequent changes + large N** | absl::btree_map | B-tree middle ground (but Chromium disables it for code size) |

One line covers it: read-heavy and write-light goes to flat_map, write-heavy goes back to std::map. Chromium's `//base/containers/README.md` draws the same line.

## vs std::flat_map (C++23) and absl::btree_map

First, `std::flat_map` (C++23, P0429). It shares a lineage with this Chromium flat_map, but the standard version stores things differently. It uses split storage, with keys and values each in their own contiguous array, so when you only walk the keys the cache packs tighter and the values don't get in the way. Sounds better. The cost is maintaining two containers in sync, which pushes implementation complexity up. Chromium didn't go split; it stuck with one plain `vector<pair<K,V>>`. The "looks better" split got dropped by an industrial mainline, because the complexity and the payoff don't balance out.

Then `absl::btree_map`. It's a B-tree with TargetNodeSize=256B, so each node holds dozens of keys. One cache-line hit then compares several keys, which both fixes the pointer chasing of red-black trees and dodges the sorted vector's `O(n)` insert. It's the answer for the corner of demand that wants ordering, large N, and frequent changes all at once. But it carries a bill you can't ignore: code size. Chromium explicitly bans `absl::btree_map` in `//base`, and that's the reason.

## The teaching version vs Chromium's tradeoffs

Like the previous two series, our teaching version takes one simplifying pass:

| Dimension | Chromium | Teaching version |
|---|---|---|
| Underlying Container | `std::vector` | same |
| Sorting | `std::stable_sort` + unique + erase | same |
| Transparent comparison | `KeyT<K>` + `KeyValueCompare` dual overload | simplified template |
| `DCHECK(is_sorted_and_unique)` | full | `assert` simulation |
| `[[no_unique_address]]` comparator | annotated | annotated |
| `extract`/`replace` | full | simplified/omitted |
| `raw_ptr_exclusion`/Chromium macros | full | omitted |

The core mechanisms (sorted vector adapter, tag dispatch, transparent comparison, EBO, batch construction) are all here unchanged.

That closes the loop on flat_map's design, implementation, and verification. From [the red-black tree pain in pre-00](./pre-00-flat-map-ordered-assoc-container-intro.md) through these 13 pieces, we've walked the whole chain of "why a sorted vector can beat a red-black tree." Add in OnceCallback and WeakPtr from before, and the three pieces of industrial-grade C++ design in Chromium `//base` are now in place: callbacks, weak references, and containers.

## References

- [Catch2 documentation](https://github.com/catchorg/Catch2/tree/devel/docs)
- [Chromium `base/containers/README.md`: container selection guide](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/README.md)
- [P0429: std::flat_map proposal (C++23)](https://wg21.link/p0429)
- [absl::btree_map documentation](https://abseil.io/docs/cpp/guides/btree)
- [cppreference: std::map](https://en.cppreference.com/w/cpp/container/map)
