---
chapter: 1
cpp_standard:
- 11
- 17
- 20
description: "Implement the sorted_unique construction optimization: use tag dispatch to skip sort_and_unique, drop bulk construction from O(N log N) to O(N), back it with an honest DCHECK contract, and know when to reach for it."
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
---
# flat_map hands-on (IV): sorted_unique construction optimization

[03-3](./03-3-flat-map-lookup-and-insert.md) tore single-element insert apart. Every `insert` is an `O(n)` shift, and that looks harmless when you add things one at a time. Try building a large flat_map with it, though, like loading a config table at startup, and the `O(N²)` total cost will grind you to a halt. We burned a whole afternoon on this once with a 100k-element config. Startup was absurdly slow, and the profiler showed the shift eating every cycle.

This piece is about how to walk around that wall at construction time. The first path is bulk construction: pile the data into a vector, move it into flat_map in one shot, pay `O(N log N)` for a single sort at the end. The real headliner is `sorted_unique` construction. If your data is already sorted, the sort step gets skipped entirely and the cost drops to `O(N)`. This is [pre-04 tag dispatch](./pre-04-flat-map-tag-dispatch-and-sorted-unique.md) landing on the ground, and we'll walk the whole path from end to end.

---

## The trap: building with insert is O(N²)

The obvious way to build the map is a loop that inserts one element at a time. It looks innocent enough:

```cpp
flat_map<int, Config> m;
for (auto& [k, v] : load_data()) {
    m.insert({k, v});   // every call is an O(n) shift
}
```

Spread the cost out and look at it. The first insert is `O(1)`, the second is `O(2)`, climbing all the way to the Nth at `O(N)`, for a total of `O(1) + O(2) + ... + O(N) = O(N²)`. Scale the data up and it turns into a disaster. With 100k elements the shift count lands around `10⁸`, and measured runs chew through several seconds. That was the exact spot where we got burned.

The flat_map authors clearly knew about this, so they left cheaper paths open specifically for construction.

---

## Bulk construction: fill a vector, move it in, O(N log N)

The way around `O(N²)` is to batch. Dump everything into a vector first, then move the whole vector into flat_map:

```cpp
std::vector<std::pair<int, Config>> raw;
raw.reserve(N);
for (auto& [k, v] : load_data()) raw.emplace_back(k, v);   // vector push_back, amortized O(1)

flat_map<int, Config> m(std::move(raw));   // move construction, one sort inside
```

The `flat_map(container_type&& items)` constructor (around flat_tree.h:578) does two things. It takes over the vector's storage in an `O(1)` move, then calls `sort_and_unique` once for `O(N log N)`. Total construction cost drops to `O(N log N)`. Compare that against per-element insert's `O(N²)` for the same 100k elements: `N log N ≈ 1.7×10⁶` against `N² = 10¹⁰`, four orders of magnitude apart.

This is the construction posture flat_map officially recommends. Pile the data in a vector, enjoy push_back's amortized `O(1)`, then move it in with one cut. The flat_map.h:61-62 docs say it straight out: "If possible, construct a flat_map in one operation by inserting into a container and moving that container into the flat_map constructor."

---

## sorted_unique: skip the sort, O(N)

Now suppose the data you're holding is already sorted and has no duplicates. That `O(N log N)` `sort_and_unique` inside bulk construction is wasted work. Just take the vector over as-is. That is exactly what the `sorted_unique` constructor is for:

```cpp
std::vector<std::pair<int, Config>> raw = load_already_sorted_data();   // already sorted
flat_map<int, Config> m(sorted_unique, std::move(raw));   // skip sort_and_unique, O(N)
```

Pass a `sorted_unique` tag as the first argument and flat_map routes to the sort-skipping overload (flat_tree.h:606-646). It does exactly two things: take over the vector in an `O(1)` move, then run `DCHECK(is_sorted_and_unique(...))` for a debug check. In release builds the DCHECK compiles away to nothing, so the whole cost is the takeover. Pure `O(N)`.

### The five sorted_unique overloads

flat_tree ships five overloads for `sorted_unique`, covering every input source:

- `flat_map(sorted_unique, InputIterator first, last, comp)`
- `flat_map(sorted_unique, from_range_t, Range&&, comp)` (C++23 ranges)
- `flat_map(sorted_unique, const container_type&, comp)`
- `flat_map(sorted_unique, container_type&&, comp)` ← the one used above
- `flat_map(sorted_unique, initializer_list, comp)`

They differ from the corresponding plain constructors in exactly one way: no `sort_and_unique`. The mechanism is tag dispatch, which we pulled apart in [pre-04](./pre-04-flat-map-tag-dispatch-and-sorted-unique.md). `sorted_unique_t` is an empty tag type, and the compiler picks a different function during overload resolution based on whether you pass the tag. Zero runtime cost.

---

## DCHECK(is_sorted_and_unique): an honest contract

Here's the catch. You tell flat_map "the data is sorted," but what if it isn't? flat_map catches the lie in debug with a `DCHECK` (flat_tree.h:612/624/633/642):

```cpp
flat_tree(sorted_unique_t, container_type&& body, const Compare& comp)
    : body_(std::move(body)), comp_(comp) {
    DCHECK(is_sorted_and_unique(body_, comp_));   // debug check
}
```

We saw `is_sorted_and_unique` (flat_tree.h:55-62) in [pre-04](./pre-04-flat-map-tag-dispatch-and-sorted-unique.md):

```cpp
template <typename Range, typename Comp>
constexpr bool is_sorted_and_unique(const Range& range, Comp comp) {
    return std::ranges::adjacent_find(range, std::not_fn(comp)) ==
           std::ranges::end(range);
}
```

It scans adjacent elements to confirm each one is strictly less than the next, with no equals and no inversions. One `O(N)` pass, debug only. Lie to it and the debug test aborts in your face. In release the `DCHECK` compiles to nothing and not a single byte gets checked. It trusts you completely.

We call this an honest contract. flat_map hands you the `O(N)` optimization, and the price is that you guarantee the data really is sorted. Debug polices that guarantee for you; release lets go and trusts it. So when the data source is not reliable, user input or something scraped off the network, don't force `sorted_unique`. Use plain bulk construction and let flat_map sort it for you.

---

## When to reach for sorted_unique

The test comes down to one line: can your data source credibly guarantee sorted, duplicate-free data?

The trustworthy cases are easy to spot. The data comes out of another sorted container, like an export from another flat_map or a `std::set`. Or you just ran `std::sort` plus `unique` over it yourself. Or it's a compile-time constant, like a config table written as an initializer_list where you stared at the keys while writing them. In all of these `sorted_unique` sits comfortably.

Flip it around. If the data comes from user input, a file, or the network, the order is not under your control, so don't risk it. There's a class of slip that's easy to miss here: you're not sure whether duplicates sneak in. Plain construction de-duplicates for you, `sorted_unique` does not. Let a duplicate slip through and you've broken flat_map's invariant with your own hands. Lookups after that turn into astrology. When you're not sure, fall back to plain bulk construction and let flat_map sort and de-duplicate. The cost is `O(N log N)`, still miles faster than per-element insert.

---

## A minimal reproduction

Theory's done. Let's hand-roll a minimal MiniMap that exercises both construction paths, so you can see the difference plainly:

```cpp
// Platform: host | C++ Standard: C++20
#include <algorithm>
#include <cassert>
#include <vector>

struct sorted_unique_t {};
inline constexpr sorted_unique_t sorted_unique{};

class MiniMap {
public:
    // Plain construction: sort and de-duplicate
    MiniMap(std::vector<int> data) : data_(std::move(data)) {
        std::sort(data_.begin(), data_.end());
        data_.erase(std::unique(data_.begin(), data_.end()), data_.end());
    }
    // sorted_unique construction: skip the sort, debug-check the claim
    MiniMap(sorted_unique_t, std::vector<int> data) : data_(std::move(data)) {
        assert(is_sorted_unique());   // catch the lie in debug
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

That's the `O(n)` wall walked around. Bulk construction fills a vector and moves it in for an `O(N log N)` finish; if the data is already sorted, pass the `sorted_unique` tag, skip the sort, and pay `O(N)` for a plain takeover, with a debug `DCHECK` standing guard. That is real money saved by tag dispatch, at construction time.

Two things in flat_map still deserve a full teardown: iterator invalidation rules, and the broader set of bulk construction patterns. We pick those up next.

## References

- [Chromium `base/containers/flat_tree.h`: sorted_unique overloads and is_sorted_and_unique](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_tree.h)
- [Chromium `base/containers/flat_map.h`: bulk construction advice](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_map.h)
- [flat_map prerequisite (IV): tag dispatch and sorted_unique_t](./pre-04-flat-map-tag-dispatch-and-sorted-unique.md)
