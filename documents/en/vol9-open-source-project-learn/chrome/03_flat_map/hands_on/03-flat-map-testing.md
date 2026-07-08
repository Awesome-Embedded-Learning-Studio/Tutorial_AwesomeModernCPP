---
chapter: 1
cpp_standard:
- 17
- 20
description: "flat_map's test strategy: design cases around invariants, then measure per-item overhead, lookup/insert performance, and lay out the selection criteria for flat_map vs std::map/absl::btree_map."
difficulty: advanced
order: 3
platform: host
prerequisites:
- 'flat_map design guide (II): step-by-step implementation'
reading_time_minutes: 6
related:
- 'flat_map design guide (I): motivation, API, and the flat_tree architecture'
- 'flat_map hands-on (VI): testing and performance comparison'
tags:
- host
- cpp-modern
- advanced
- 容器
- map
- 测试
- 优化
title: "flat_map Design Guide (III): Test Strategy and Performance Comparison"
---
# flat_map Design Guide (III): Test Strategy and Performance Comparison

The implementation piece is done, and honestly we weren't fully at ease with it. `flat_tree`'s tangle of `lower_bound + emplace + shift` compiling is one thing; whether the semantics actually hold is another. Containers are at their most dangerous when they "look like they run": you throw in a few numbers, iterate them back in order, tests go green, and you ship. Duplicate-key dedup, `insert_or_assign` overwrite, a lying `sorted_unique`, iterator invalidation, all of those land on the boundary. In this piece we press each of the six invariants promised in part one back into tests, then put flat_map on the bench against `std::map` and `absl::btree_map` to see where it actually saves and where it pays. The playbook mirrors the [WeakPtr design guide (III)](../../02_weak_ptr/hands_on/03-weak-ptr-testing.md): invariants drive the tests, numbers settle the rest, no hand-waving.

## Invariants into a test matrix

| # | Invariant | Assertion |
|---|---|---|
| 1 | Ordered | Iteration yields strictly ascending keys |
| 2 | Unique | Duplicate keys are deduped |
| 3 | Lookup semantics | find/contains/operator[]/at agree; out-of-range at CHECK/asserts |
| 4 | insert_or_assign/try_emplace | Existing is overwritten, new is inserted |
| 5 | sorted_unique | Skips the sort; lying input aborts in debug |
| 6 | Iterator invalidation | Old iterators are dead after mutation (coarse rule) |

## Key test cases (Catch2-style sketch)

The six invariants sound abstract, but once you sit down to test them you're really just picking the boundaries that blow up if you got them wrong. We picked three that pin the semantics hardest: sort+dedup at construction, `insert_or_assign` overwrite, and `sorted_unique` lying to abort in debug. The supporting demos live as `19` through `22` `.cpp` files under `code/volumn_codes/vol9/full_tutorial_codes/chrome_design/`; wiring them into a Catch2 test target is left as an extension. Here is what the cases look like:

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
    // Invariant 5: pass unsorted data but swear sorted_unique → debug abort
    // flat_map<int,int> m(sorted_unique, std::vector<std::pair<int,int>>{{3,3},{1,1}});
}
```

All three stare at semantic boundaries, not API surface. The construction case checks invariants 1 and 2 at once: `{3,30}` is dropped in twice and out of order, so iteration had better come back as exactly `1,2,3`. Off by one and `sort_and_unique` is wrong. The `insert_or_assign` case is finer; we deliberately contrast `ins` being false against `it->second == 99`, because conflating "inserted a new one" with "overwrote an old one" is the classic mistake. The `sorted_unique` lying case stands on its own because it aborts.

A case that aborts has a headache attached: drop it into a plain TEST_CASE and the whole binary goes down with it. You have to isolate it as a death test and let it crash in a subprocess. This is the same routine 01-6 uses for the OnceCallback single-consume assertion, and the same one WeakPtr uses for CHECK-on-deref. We already walked it in those two pieces.

## Performance: per-item overhead

```text
sizeof(flat_map<int,int>)  ≈ 24 bytes (three pointers)
sizeof(std::map<int,int>)  ≈ 48 bytes (tree root + sentinel + comparator)

Per-item overhead for a 1M-entry map<int,int> (the 8MB payload not counted):
  flat_map:  ~0 extra (data is contiguous)
  std::map:  ~32MB (32B/element × 1M + 1M malloc calls)
```

flat_map carries zero per-item metadata; one contiguous allocation and it's done. std::map lugs 32B of metadata per element plus one heap allocation each. That is where the "constant factor off by an order of magnitude" line in part one comes from. Both are `O(log n)` lookup asymptotically, but std::map quietly books 32MB of metadata plus a million mallocs behind your back.

## Performance: lookup vs insert

Just reading `sizeof` and allocation counts isn't satisfying enough, so we ran it on the machine. GCC 16 -O2, the `20_lookup_vs_shift_perf` demo, a 100k-element `map<int,int>`:

```text
Lookup, 100k calls (100k elements):
  flat_map:  31 ms
  std::map:  34 ms     (int key + 100k: roughly tied)

Insert, 1000 calls into a 100k-element container:
  flat_map:   2 ms     (O(n) shift each time)
  std::map:   0 ms     (O(log n) node relink)
```

One number here surprised us the first time we saw it. On lookup, flat_map does not grind std::map into the dirt; they're roughly tied. Think about it for a second and it makes sense: at 100k entries with an int key, the data itself fits in cache, std::map's pointer chasing hasn't started missing in bulk yet, so flat_map's contiguity dividend has nothing to show. To actually see the gap you have to push N higher, or swap the key for something heavy like `std::string`. In independent large-N runs flat_map coming out several times faster is common, but don't take that as dogma. Small N with a light key, the advantage just isn't there, and that's normal.

Insert flips the picture, and with no suspense. Every flat_map insert has to `O(n)` shift a slab of elements, 2 ms against std::map's 0 ms, and the bigger N gets the wider that gap opens. It's a real wall. That's why flat_map's contract states it plainly: this is a read-heavy, write-light home, not a container for high-frequency writes.

## Selection criteria

| Workload | Recommendation | Reason |
|---|---|---|
| Write once, read many (config table, command dispatch) | flat_map | One-shot write, cache-friendly lookup |
| Always small (~4 elements) | flat_map | Constant factor dominates, zero allocation |
| Large and frequently mutated | std::map | O(n) insert is a wall |
| Needs stable refs/pointers | std::map | flat_map invalidates all iterators |
| Large N + frequent mutation + ordered | absl::btree_map | B-tree middle ground (Chromium bans it for code bloat) |

This table really boils down to one line: read-heavy or always-small, pick flat_map; write-heavy or needing stable references, pick std::map; large, frequently mutated, and ordered, that's absl::btree_map's middle ground, but Chromium itself blocked that path over code bloat. Chromium's `//base/containers/README.md` just writes this table out in prose.

## vs std::flat_map (C++23) / absl::btree_map

Two relatives are unavoidable here. C++23's `std::flat_map` (P0429) and Chromium flat_map share an origin and the same idea, but the standard version went with split storage, keys and values in two separate arrays. Chromium declines the split and sticks with a single `vector<pair<K,V>>`. We get Chromium's tradeoff: split genuinely saves cache when you iterate only keys or only values, but the implementation complexity climbs, and flat_map's main arena is read-heavy small containers where split's payoff never cashes in. Not worth it.

The other is `absl::btree_map`, a 256B B-tree node holding a handful of keys, sitting between red-black tree and sorted vector. It's the right fit for large N plus frequent mutation plus ordering. But Chromium bans btree inside `//base`, and the stated reason is code bloat. Each key/value type you instantiate pulls in another fat slab of template, and B-tree node split/merge logic is far heavier than sorted vector's `lower_bound + shift`. This is a textbook engineering tradeoff: a technically better option exists, the project-level cost is too high, so they decline.

That closes the flat_map design, implementation, and verification pieces. Looking back, it slots in as the third tile in vol9/chrome alongside OnceCallback and WeakPtr. The first two cover "how to keep callbacks in line" and "how to keep lifetimes in line," and this one covers "how to store data both cheaply and fast." All three are the industrial-grade C++ fundamentals you find in Chromium `//base`.

## References

- [Chromium `base/containers/README.md` — container selection guide](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/README.md)
- [Catch2 documentation](https://github.com/catchorg/Catch2/tree/devel/docs)
- [P0429 — the std::flat_map proposal](https://wg21.link/p0429)
- [absl::btree_map](https://abseil.io/docs/cpp/guides/btree)
- [flat_map design guide (I): motivation, API, and the flat_tree architecture](./01-flat-map-design.md)
