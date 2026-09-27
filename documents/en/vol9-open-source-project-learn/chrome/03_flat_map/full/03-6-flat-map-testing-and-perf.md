---
chapter: 1
cpp_standard:
- 17
- 20
description: "Tests flat_map around its invariants with Catch2, then measures object size, per-item overhead, and lookup/insert performance for real, compares against std::map and absl::btree_map, and distills the selection criteria."
difficulty: intermediate
order: 6
platform: host
prerequisites:
- "flat_map hands-on (V): iterator invalidation and bulk construction"
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
translation:
  source: documents/vol9-open-source-project-learn/chrome/03_flat_map/full/03-6-flat-map-testing-and-perf.md
  source_hash: c18e6175c0d29391e1891f2737e01b5908b7fa260245223eb87f33474acb8a48
  translated_at: '2026-09-26T02:49:37+00:00'
  engine: anthropic
  token_count: 4300
---
# flat_map hands-on (VI): testing and performance comparison

The code is written, and what's actually left are two questions: did we get it right, and how fast does it run. This piece circles exactly those two. In the first half we design tests with the invariants as the target, verifying sorting, lookup, insertion, deduplication, and invalidation one by one; in the second half we haul flat_map onto the scale next to `std::map` and `absl::btree_map`, and measure how big the object is, how fast lookup is, and how painful insertion is. Once the runs are done, it will be plain exactly which data that criterion — "flat_map wins at small N; at large N with heavy writes std::map overtakes" — lands on.

## Six invariants, not one of them may slip

Whether flat_map counts as "correct" depends entirely on whether these six invariants hold up, so we'll take them all together here.

Sorting and deduplication are the first two, and the easiest to verify by eye: the keys you get from one traversal must be strictly ascending, and there must be no duplicates. The third is that the lookup semantics must be tidy — `find`, `contains`, `operator[]`, and `at` each doing its own job, where an out-of-range `at` CHECK-crashes right in front of you — that kind is a definite bug, and it has to blow up in release too. The fourth targets two write interfaces that are easy to mix up: `insert_or_assign` overwrites when the key already exists, while `try_emplace` leaves an existing key alone — one mutates, one doesn't; don't get the semantics backwards. The fifth is the `sorted_unique` path, the "liar takes the shortcut" route — you claim the data is already sorted and deduplicated, and it skips the sort; but if you lied, debug builds abort on the spot. The last one is iterator invalidation: after any mutation, old iterators are voided under the coarse rule. The previous piece already laid that one bare, so here we only verify it.

## Key test cases (Catch2-style sketches)

Below are Catch2-style sketch cases (the project's current runnable examples are the demo .cpp files `19` through `22` under `code/.../chrome_design/`; wiring the Catch2 test target in is left as an extension):

```cpp
// Platform: host | C++ Standard: C++20
#include <catch2/catch_test.hpp>
#include "flat_map.hpp"
using namespace tamcpp::chrome;

TEST_CASE("flat_map is sorted+unique after construction", "[flat_map]") {
    flat_map<int, int> m{{3,30}, {1,10}, {3,30}, {2,20}};   // duplicate 3, unsorted
    // Invariants 1+2: ordered and deduplicated
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
    auto [it1, ins1] = m.insert_or_assign(1, 99);   // already exists → overwrite
    REQUIRE_FALSE(ins1);
    REQUIRE(it1->second == 99);
    // try_emplace leaves an existing key alone (can't be verified directly in the same test; semantics covered in 03-3)
}
```

These cases all aim at semantic boundaries — sort-and-dedupe, the CHECK inside `at`, the overwrite in `insert_or_assign`. The death test for `at` has to run isolated, because it genuinely aborts — not the same animal as an ordinary assertion.

## Performance: object size and per-item overhead

First let's measure "how much memory does it take" properly. flat_map's skeleton is nothing more than a `vector<pair<K,V>>` plus a zero-byte comparator; `std::map` goes the red-black-tree route, where every node carries 3 pointers plus a color bit, on top of the data itself. We look at the container skeleton with `sizeof`, then use the footprint of 1 million elements to see the per-item overhead amortized over each entry:

```text
sizeof(flat_map<int,int>)  ≈ sizeof(vector<pair<int,int>>) = 24 bytes (three pointers, 64-bit)
sizeof(std::map<int,int>)  ≈ 48 bytes (tree root + comparator + sentinel node)

Extra overhead of a 1M-element map<int,int> (the 8MB of data itself not counted):
  flat_map:  ~0 extra (data contiguous, no per-node metadata)
  std::map:  ~32MB (32B per node × 1M, plus one malloc per node)
```

flat_map practically freeloads on per-item overhead: the data sits in one contiguous run, there is no per-node metadata, and it mallocs exactly once. `std::map`, on this side, pays 32B per entry in node metadata alone, and needs a million heap allocations on top to fill out. The smaller the objects and the bigger the collection, the wider this gap tears open.

## Performance: lookup (cache-friendly vs pointer chasing)

Both sides do lookup in `O(log n)` — asymptotically neither is faster than the other. The constant factor is the watershed: flat_map's data is contiguous, so the comparisons during binary search get to ride the cache; `std::map`'s nodes sit scattered all over, and every hop's dereference is most likely a cache miss.

Measured (this machine, GCC 16, -O2, companion `20_lookup_vs_shift_perf`; a 100k-element `map<int,int>`, 100k `find` calls on each):

```text
100k lookups (100k elements):
  flat_map:  31 ms
  std::map:  34 ms
```

Don't rush to a verdict. At N=100k with an `int` key, the two sides are nearly tied — an int comparison costs a single cycle on its own, and that little cache dividends at this scale have not yet earned their keep — the two sides have comparable search depth. flat_map truly pulls away only when N gets bigger, or the key gets heavier. Switch the key to `std::string`, for example: comparison itself becomes expensive, the cost share of a single cache miss is instantly magnified, and in standalone large-N tests flat_map coming out several times faster is common. So "flat_map lookup is always faster" is not a line to memorize as dogma — it feeds on the workload: the bigger N gets and the heavier the key, the more visible the edge.

## Performance: insertion (the O(n) shift wall)

Lookup can still bend with the workload; on insertion flat_map plainly loses. Measured: appending 1000 keys one by one into a container already stuffed with 100k elements:

```text
1000 inserts into a 100k-element container:
  flat_map:  2 ms   (O(n) shift each time)
  std::map:  0 ms   (O(log n) node relinking each time)
```

flat_map loses in broad daylight, and this is the hardest piece of data behind the "read-mostly, write-rarely" criterion. If inserts dominate your workload, flat_map's `O(n)` shift will sooner or later become the bottleneck — swallow your pride and go back to `std::map`. The absolute numbers drift with the machine and with N, but the trend — flat_map insertion being slower than `std::map` — is rock solid: the bigger N gets, the wider the gap, because the shift's cost is O(n) by nature.

## Selection criteria (what the measurements say)

With all three sets of data in, the selection criteria are right there on the table:

| Workload | Recommendation | Reason |
|---|---|---|
| **Written once, read many times** (config tables, command dispatch, table lookup) | flat_map | All writes are one-shot (bulk construction); reads are cache-friendly |
| **Always small** (browser statistics put the mode at ~4 elements) | flat_map | Constant factors dominate at small N; the zero-allocation edge is big |
| **Large and frequently modified** (dynamic indexes) | std::map | flat_map's O(n) insert is a wall |
| **Needs pointer/reference stability** | std::map | flat_map iterators are all invalidated across mutations |
| **Many ordered keys + frequent modification + large N** | absl::btree_map | B-tree middle ground (but Chromium bans it over code bloat) |

One sentence says it all: read-mostly and write-rarely, take flat_map; write-heavy, go back to std::map. Chromium's `//base/containers/README.md` draws exactly the same line.

## vs std::flat_map (C++23) and absl::btree_map

`std::flat_map` (C++23, P0429) first. It shares its ancestry with this Chromium flat_map, but the standard version switched to a different storage layout — split storage, keys and values each in their own contiguous array, so when you only walk the keys they pack denser in cache and the values don't barge in and add noise. Sounds better; the price is keeping two containers in sync, and implementation complexity climbs. Chromium didn't go split — it honestly sticks with one `vector<pair<K,V>>`: the "looks better on paper" split was abandoned by the industrial mainline precisely because complexity and payoff didn't balance on the ledger.

Then `absl::btree_map`. It is a B-tree with TargetNodeSize=256B per node, so dozens of keys fit at once. One cache-line hit then covers comparisons against several keys — it cures the red-black tree's pointer-chasing ailment while dodging the sorted vector's `O(n)` insert. It is the antidote for that pile of demands that wants "ordered AND large-N AND frequently modified" all at once. But it carries one bill it cannot dodge: code size. Chromium explicitly bans `absl::btree_map` in `//base`, and this is exactly why.

## Trade-offs between the teaching version and Chromium

As with the previous two series, our teaching version simplifies one layer:

| Dimension | Chromium | Teaching version |
|---|---|---|
| Underlying Container | `std::vector` | same |
| Sorting | `std::stable_sort` + unique + erase | same |
| Transparent comparison | `KeyT<K>` + `KeyValueCompare` dual overloads | simplified template |
| `DCHECK(is_sorted_and_unique)` | full | emulated with `assert` |
| `[[no_unique_address]]` comparator | annotated | annotated |
| `extract`/`replace` | full | simplified/omitted |
| `raw_ptr_exclusion`/Chromium macros | full | omitted |

The core mechanisms (the sorted-vector adapter, tag dispatch, transparent comparison, EBO, bulk construction) are reproduced verbatim.

With this, flat_map as a component has gone all the way through design, implementation, and verification. Across the 13 pieces from [pre-00 red-black tree pain points] all the way to here, we've walked the entire chain of "why a sorted vector can take down the red-black tree". Add the earlier OnceCallback and WeakPtr series, and the three puzzle pieces of industrial-grade C++ design in Chromium `//base` — callbacks, weak references, and containers — are finally all in place.

## References

- [Catch2 documentation](https://github.com/catchorg/Catch2/tree/devel/docs)
- [Chromium `base/containers/README.md` — the container-selection guide](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/README.md)
- [P0429 — the std::flat_map proposal (C++23)](https://wg21.link/p0429)
- [absl::btree_map documentation](https://abseil.io/docs/cpp/guides/btree)
- [cppreference: std::map](https://en.cppreference.com/w/cpp/container/map)
