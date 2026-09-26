---
chapter: 1
cpp_standard:
- 17
- 20
description: "flat_map's test strategy — design cases around the invariants, measure per-item overhead and lookup/insert performance for real, and distill the selection criteria for flat_map vs std::map/absl::btree_map"
difficulty: advanced
order: 3
platform: host
prerequisites:
- "flat_map Design Guide (II): step-by-step implementation"
reading_time_minutes: 6
related:
- "flat_map Design Guide (I): motivation, API, and the flat_tree architecture"
- "flat_map hands-on (VI): testing and performance comparison"
tags:
- host
- cpp-modern
- advanced
- 容器
- map
- 测试
- 优化
title: "flat_map Design Guide (III): test strategy and performance comparison"
translation:
  source: documents/vol9-open-source-project-learn/chrome/03_flat_map/hands_on/03-flat-map-testing.md
  source_hash: 0cd9f5031d4c424cad8d4b6cc513fdb00dada32f251e78360f705f2e8f25cf46
  translated_at: '2026-09-26T03:08:37+00:00'
  engine: anthropic
  token_count: 1500
---
# flat_map Design Guide (III): test strategy and performance comparison

The implementation piece is behind us, and honestly we weren't all that reassured — `flat_tree`'s tangle of `lower_bound + emplace + shift` compiling is one thing; whether the semantics hold is another. A container is exactly the kind of thing that fears "looks like it runs" the most: you throw a few numbers in, iterate them back in order, green. But duplicate-key dedup, the `insert_or_assign` overwrite, a lying `sorted_unique`, iterator invalidation — all of those traps sit piled on the boundaries. In this piece we press the six invariants promised in part one back into tests, one by one, then set real measurements against `std::map` and `absl::btree_map` to see exactly where flat_map saves and where it pays. The playbook runs straight through [WeakPtr Design Guide (III)](../../02_weak_ptr/hands_on/03-weak-ptr-testing.md): invariants drive the cases, the data does the talking, no hand-waving.

## Six invariants → a test matrix

| # | Invariant | Assertion |
|---|---|---|
| 1 | Ordered | Iteration yields strictly ascending keys |
| 2 | Unique | Duplicate keys get deduplicated |
| 3 | Lookup semantics | find/contains/operator[]/at correct; at out of range CHECK/assert |
| 4 | insert_or_assign/try_emplace | For an existing key, one overwrites and one leaves it alone |
| 5 | sorted_unique | Skips the sort; lying input debug-aborts |
| 6 | Iterator invalidation | Old iterators invalidated after mutation (coarse rule) |

## Key test cases (Catch2-style sketches)

Six invariants sound abstract; on the ground, testing them means picking the boundaries that are guaranteed to blow up if the code is wrong. We pick the three that lock down the semantics hardest — sorted dedup at construction, the `insert_or_assign` overwrite, and `sorted_unique` lying into a debug abort. The companion project holds the demo .cpp files numbered `19` through `22` under `code/volumn_codes/vol9/full_tutorial_codes/chrome_design/`; wiring up the Catch2 test target is left as an extension. First, here is what the cases look like:

```cpp
TEST_CASE("flat_map sorts+uniques on construction", "[flat_map]") {
    flat_map<int,int> m{{3,30},{1,10},{3,30},{2,20}};
    std::vector<int> keys;
    for (auto& [k,v] : m) keys.push_back(k);
    REQUIRE(keys == std::vector<int>{1,2,3});   // invariants 1+2
}

TEST_CASE("insert_or_assign overwrites existing", "[flat_map]") {
    flat_map<int,int> m{{1,10}};
    auto [it, ins] = m.insert_or_assign(1, 99);
    REQUIRE_FALSE(ins); REQUIRE(it->second == 99);   // invariant 4
}

TEST_CASE("sorted_unique aborts on lying input", "[flat_map][.death]") {
    // Invariant 5: passing unsorted data while swearing sorted_unique → debug abort
    // flat_map<int,int> m(sorted_unique, std::vector<std::pair<int,int>>{{3,3},{1,1}});
}
```

All three stare at semantic boundaries, not API surface. The construction case verifies invariants 1 and 2 together — the `{3,30}` you throw in is duplicated and out of order, and iteration must come out exactly `1,2,3`; one element off means `sort_and_unique` is written wrong. The `insert_or_assign` case is finer-grained: we deliberately cross-check `ins` being false against `it->second==99`, precisely because conflating "inserted a new one" with "overwrote the old one" is the mistake to avoid. The `sorted_unique` lying case gets pulled out on its own because it aborts.

A case that aborts carries a headache: dropped into a plain TEST_CASE, the whole binary goes down with it. It has to be isolated as a death test and allowed to crash in a subprocess — the same routine 01-6 uses for the OnceCallback single-consume assertion, and the same one WeakPtr uses for CHECK-on-deref; we already walked that road in those two pieces.

## Performance: per-item overhead

```text
sizeof(flat_map<int,int>)  ≈ 24 bytes (three pointers)
sizeof(std::map<int,int>)  ≈ 48 bytes (tree root + sentinel + comparator)

Extra overhead of a 1M-element map<int,int> (the 8MB of data not counted):
  flat_map:  ~0 extra (data contiguous)
  std::map:  ~32MB (32B/element × 1M + 1M mallocs)
```

flat_map carries zero per-item metadata — one contiguous allocation and it's done; every std::map element hauls 32B of metadata and needs a heap allocation of its own on top. This is exactly where part one's "constant factor an order of magnitude apart" comes from — both do `O(log n)` lookup asymptotically, but std::map has already quietly booked 32MB of metadata plus a million mallocs where we can't see it.

## Performance: lookup vs insertion

`sizeof` and allocation counts alone aren't satisfying enough, so we ran it on the machine for real. This machine, GCC 16 -O2, companion `20_lookup_vs_shift_perf`, a 100k-element `map<int,int>`:

```text
100k lookups (100k elements):
  flat_map:  31 ms
  std::map:  34 ms     (int key + 100k: nearly tied)

1000 inserts into a 100k-element container:
  flat_map:   2 ms     (O(n) shift each time)
  std::map:   0 ms     (O(log n) node relinking)
```

One number here stopped us cold the first time we saw it — on lookup, flat_map didn't grind std::map into the dirt; the two are nearly tied. A moment's thought clears it up: at the 100k + int-key scale, the data itself fits in cache, std::map's pointer chasing hasn't started missing en masse yet, and flat_map's contiguity dividend naturally can't show. To see a real gap you have to push N higher still, or switch the key to something heavy like `std::string`. In standalone large-N tests flat_map coming out several times faster is common — but don't take that as dogma: at small N with light keys the edge just isn't visible, and that's normal.

Insertion flips the picture, with no suspense. flat_map has to `O(n)`-shift a whole stretch on every single insert — 2 ms against std::map's 0 ms — and the bigger N gets, the wider that gap tears. It is a wall, plain and solid. That's why flat_map's contract is spelled out plainly: home turf is read-many-write-few; don't press it into service as a high-frequency-write container.

## Selection criteria

| Workload | Recommendation | Reason |
|---|---|---|
| Write once, read many (config tables / command dispatch) | flat_map | Writes are one-shot; lookups are cache-friendly |
| Always tiny (~4 elements) | flat_map | Constant factors dominate; zero allocations |
| Large and frequently modified | std::map | O(n) insertion is a wall |
| Needs stable references/pointers | std::map | flat_map invalidates all iterators |
| Large N + frequent changes + ordered | absl::btree_map | The B-tree middle ground (disabled in Chromium over code bloat) |

This table is really a one-sentence matter: read-many-write-few, or a container that stays small — flat_map; write-heavy, or stable references and pointers required — std::map; large, frequently modified, and still insisting on order — that's absl::btree_map's middle-ground solution, though Chromium itself blocked that road off over code bloat. Chromium `//base/containers/README.md`'s way of slicing it is just this table set down in prose.

## vs std::flat_map (C++23) / absl::btree_map

Having talked this far, we can't dodge two relatives. C++23's `std::flat_map` (P0429) shares its ancestry with Chromium's flat_map — one line of thinking — but the standard version chose split storage: keys and values are kept in two separate arrays. Chromium pointedly refuses to split and stays honest with a single `vector<pair<K,V>>`. We read Chromium's tradeoff like this: split genuinely saves cache when you "iterate keys only" or "iterate values only", but it buys that with added implementation complexity — and flat_map's main battlefield is small read-mostly containers, where split's payoff simply cannot be cashed in. Not worth it.

The other one is `absl::btree_map`: a handful of keys packed into each 256B B-tree node, sitting between a red-black tree and a sorted vector — the best fit for large N + frequent modification + ordering. But Chromium disables btree in `//base`, on grounds of code bloat: every key/value type you instantiate means yet another huge pile of generated templates, and a B-tree node's split-and-merge logic weighs far more than the sorted vector's `lower_bound + shift` routine. It's a textbook specimen of engineering tradeoff: a technically better solution exists, but when the project-level cost is more than you can pay, you'd rather go without.

With that, the three flat_map pieces — design, implementation, verification — are complete. Looking back, it slots in with OnceCallback and WeakPtr as the third tile of the vol9/chrome puzzle: the first two covered "how to keep callbacks in line" and "how to keep lifetimes in line", and this one covers "how to store data both cheaply and fast" — all fundamentals of industrial-grade C++ from Chromium `//base`.

## References

- [Chromium `base/containers/README.md` — the container-selection guide](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/README.md)
- [Catch2 documentation](https://github.com/catchorg/Catch2/tree/devel/docs)
- [P0429 — the std::flat_map proposal](https://wg21.link/p0429)
- [absl::btree_map](https://abseil.io/docs/cpp/guides/btree)
- [flat_map Design Guide (I): motivation, API, and the flat_tree architecture](./01-flat-map-design.md)
