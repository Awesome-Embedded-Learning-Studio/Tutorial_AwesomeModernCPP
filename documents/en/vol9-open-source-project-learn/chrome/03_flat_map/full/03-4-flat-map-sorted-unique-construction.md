---
chapter: 1
cpp_standard:
- 11
- 17
- 20
description: "Implementing the sorted_unique construction optimization — use tag dispatch to skip sort_and_unique, drop bulk construction from O(N log N) to O(N), back it with the honest DCHECK contract, and know when to reach for it"
difficulty: intermediate
order: 4
platform: host
prerequisites:
- "flat_map hands-on (III): lookup and insert"
- "flat_map prerequisite (IV): tag dispatch and sorted_unique_t"
reading_time_minutes: 10
related:
- "flat_map hands-on (V): iterator invalidation and bulk construction"
tags:
- host
- cpp-modern
- intermediate
- 容器
- map
- 零开销抽象
title: "flat_map hands-on (IV): sorted_unique construction optimization"
translation:
  source: documents/vol9-open-source-project-learn/chrome/03_flat_map/full/03-4-flat-map-sorted-unique-construction.md
  source_hash: b86b525e9a1ab59a2a3d2f135ca0e289cd4864e87dfa252ee9e36035ea8ab638
  translated_at: '2026-09-26T02:32:13+00:00'
  engine: anthropic
  token_count: 2400
---
# flat_map hands-on (IV): sorted_unique construction optimization

Last time we tore flat_map's single-element insert down to the studs: every `insert` is an `O(n)` shift. One at a time, you never feel it. But if you use that to build a sizable flat_map — loading a config table at startup, say — the `O(N²)` total cost will keep you waiting forever. We took that fall ourselves once on a 100k-element config: startup was absurdly slow, and one look at the profile showed every cycle burning in the shift.

This piece is entirely about how to walk around that wall at construction time. First comes bulk construction: pile the data into a vector, then move it into flat_map with one final stroke, closing with a single `O(N log N)` sort. Then the real headliner, `sorted_unique` construction — if the data is already sorted, the sort step is skipped outright and the cost drops to `O(N)`. This optimization is [pre-04 tag dispatch](./pre-04-flat-map-tag-dispatch-and-sorted-unique.md) landing in real code, and we will walk it end to end.

---

## The trap: building with insert one by one is O(N²)

The most intuitive way is to write a loop and insert one at a time. It looks harmless:

```cpp
flat_map<int, Config> m;
for (auto& [k, v] : load_data()) {
    m.insert({k, v});   // an O(n) shift every single time
}
```

Spread the cost out and look at it. Insert #1 is `O(1)`, insert #2 is `O(2)`, climbing all the way to the Nth at `O(N)`, for a total of `O(1) + O(2) + ... + O(N) = O(N²)`. Let the data grow and it becomes a disaster. With 100k elements, the total shift count lands around `10⁸`, and measured runs grind on for several seconds. That is the exact spot where we stepped in it back then.

flat_map's interface designers clearly knew about this too, which is why they set aside cheaper routes specifically for construction time.

---

## Bulk construction: fill a vector first, then move, O(N log N)

The way around `O(N²)` is batching. Dump all the data into a vector first, then move that whole vector into flat_map:

```cpp
std::vector<std::pair<int, Config>> raw;
raw.reserve(N);
for (auto& [k, v] : load_data()) raw.emplace_back(k, v);   // vector push_back, amortized O(1)

flat_map<int, Config> m(std::move(raw));   // move construction, one sort inside
```

What the `flat_map(container_type&& items)` constructor does (around flat_tree.h:578) is simple: take over the vector's storage — an `O(1)` move — then call `sort_and_unique` once, at a cost of `O(N log N)`. Total construction cost is thereby pressed down to `O(N log N)`. Set that against per-element insert's `O(N²)`, still with 100k elements: `N log N ≈ 1.7×10⁶` versus `N² = 10¹⁰` — four orders of magnitude apart.

This is the construction posture flat_map officially recommends: accumulate the data in a vector, enjoy push_back's amortized `O(1)`, and move it in with one final stroke. The flat_map.h:61-62 docs put it in exactly these words: "If possible, construct a flat_map in one operation by inserting into a container and moving that container into the flat_map constructor."

---

## sorted_unique: skip the sort, O(N)

Take it one step further: if the data in your hands is already sorted and duplicate-free, then the `O(N log N)` `sort_and_unique` inside bulk construction is work done for nothing. Why not just take the vector over as-is? That is precisely what the `sorted_unique` constructor is there to solve:

```cpp
std::vector<std::pair<int, Config>> raw = load_already_sorted_data();   // already sorted
flat_map<int, Config> m(sorted_unique, std::move(raw));   // skips sort_and_unique, O(N)
```

Pass a `sorted_unique` tag as the first argument and flat_map routes to the sort-skipping constructor overload (flat_tree.h:606-646). It does exactly two things in total: take over the vector — an `O(1)` move — and run `DCHECK(is_sorted_and_unique(...))` once as a debug check. In release builds the DCHECK is empty, so the total cost is just that one takeover stroke: pure `O(N)`.

### The 5 sorted_unique overloads

flat_tree ships 5 overloads for sorted_unique, covering every kind of input source:

- `flat_map(sorted_unique, InputIterator first, last, comp)`
- `flat_map(sorted_unique, from_range_t, Range&&, comp)` (C++23 ranges)
- `flat_map(sorted_unique, const container_type&, comp)`
- `flat_map(sorted_unique, container_type&&, comp)` ← the one used above
- `flat_map(sorted_unique, initializer_list, comp)`

They differ from the corresponding plain constructors in exactly one way: they do not call `sort_and_unique`. The mechanism is tag dispatch, which we already pulled apart in [pre-04](./pre-04-flat-map-tag-dispatch-and-sorted-unique.md). `sorted_unique_t` is an empty tag type: the compiler picks a different function during overload resolution based on "whether you pass the tag", and it costs nothing at runtime — not a cent.

---

## DCHECK(is_sorted_and_unique): an honest contract

Here comes the question. You vouch to flat_map that "the data is sorted" — but what if it actually isn't? In debug builds, flat_map catches you lying with a `DCHECK` (flat_tree.h:612/624/633/642):

```cpp
flat_tree(sorted_unique_t, container_type&& body, const Compare& comp)
    : body_(std::move(body)), comp_(comp) {
    DCHECK(is_sorted_and_unique(body_, comp_));   // debug check
}
```

We saw the implementation of `is_sorted_and_unique` (flat_tree.h:55-62) in [pre-04](./pre-04-flat-map-tag-dispatch-and-sorted-unique.md):

```cpp
template <typename Range, typename Comp>
constexpr bool is_sorted_and_unique(const Range& range, Comp comp) {
    return std::ranges::adjacent_find(range, std::not_fn(comp)) ==
           std::ranges::end(range);
}
```

It scans adjacent elements once, confirming that each is strictly less than the next — nothing equal, nothing inverted. One `O(N)` pass, run only in debug. If you lied, the debug test aborts in your face; in release the `DCHECK` compiles to nothing, checks not a single element, and trusts you completely.

We like to call this arrangement the honest contract. flat_map hands you the `O(N)` construction optimization, and in exchange you must guarantee the data really is sorted; debug guards that guarantee for you, and release lets go and trusts. So whenever the data source is not reliable — user input, something scraped off the network — don't force sorted_unique. Use plain bulk construction like an honest citizen and let flat_map sort for you.

---

## When to use sorted_unique

The decision criterion really is one sentence: can your data source credibly guarantee sorted, duplicate-free data?

The cases that can guarantee it are easy to recognize. The data comes out of another sorted container — an export from another flat_map, say, or a `std::set`. Or you just processed it yourself with `std::sort` plus `unique`. Or it is a constant baked in at compile time, like a config table written as an initializer_list that you kept your eyes on while writing. In all of these scenarios, sorted_unique is used with a clear conscience.

Flip it around: if the data comes from user input, a file, or the network, the order is not under your control at all — don't gamble. There is also one class that is easy to miss: you cannot tell whether duplicates exist. Plain construction de-duplicates for you; sorted_unique does not. Once a duplicate element slips in, you have broken flat_map's invariant with your own hands, and lookup behavior afterward turns straight into voodoo. When in doubt, use plain bulk construction and let flat_map sort and de-duplicate itself — the cost is merely `O(N log N)`, still far faster than inserting one by one.

---

## A minimal re-implementation

With the reasoning done, let's hand-roll a minimal MiniMap ourselves and run both construction paths, so you can see it even more clearly:

```cpp
// Platform: host | C++ Standard: C++20
#include <algorithm>
#include <cassert>
#include <vector>

struct sorted_unique_t {};
inline constexpr sorted_unique_t sorted_unique{};

class MiniMap {
public:
    // Plain constructor: sort and de-duplicate
    MiniMap(std::vector<int> data) : data_(std::move(data)) {
        std::sort(data_.begin(), data_.end());
        data_.erase(std::unique(data_.begin(), data_.end()), data_.end());
    }
    // sorted_unique constructor: skip the sort, debug check
    MiniMap(sorted_unique_t, std::vector<int> data) : data_(std::move(data)) {
        assert(is_sorted_unique());   // catches a lie in debug
    }
    std::size_t size() const { return data_.size(); }
private:
    bool is_sorted_unique() const {
        for (std::size_t i = 1; i < data_.size(); ++i)
            if (!(data_[i-1] < data_[i])) return false;
        return true;
    }
    std::vector<int> data_;
};

int main() {
    MiniMap a{std::vector<int>{3, 1, 2, 1}};     // plain construction, sort + dedup → 3 elements
    MiniMap b(sorted_unique, std::vector<int>{1, 2, 3, 4});  // skip the sort → 4 elements
    // MiniMap c(sorted_unique, std::vector<int>{1, 3, 2});  // lying! debug abort
    return 0;
}
```

---

With that, we have walked around the `O(n)` wall of flat_map's single-element insert. On the bulk-construction route, you fill a vector first and move it in with one stroke, finishing at `O(N log N)`; if the data is already sorted, you pass the sorted_unique tag, the sort step is simply skipped, and you pay pure `O(N)` for the takeover, with the debug `DCHECK` standing guard. That is real, hard cash saved by tag dispatch at construction time.

Two things in flat_map are still worth a thorough teardown: the iterator invalidation rules, and more bulk construction patterns. We take those apart in the pieces to come.

## References

- [Chromium `base/containers/flat_tree.h` — sorted_unique overloads and is_sorted_and_unique](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_tree.h)
- [Chromium `base/containers/flat_map.h` — bulk construction advice](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_map.h)
- [flat_map prerequisite (IV): tag dispatch and sorted_unique_t](./pre-04-flat-map-tag-dispatch-and-sorted-unique.md)
