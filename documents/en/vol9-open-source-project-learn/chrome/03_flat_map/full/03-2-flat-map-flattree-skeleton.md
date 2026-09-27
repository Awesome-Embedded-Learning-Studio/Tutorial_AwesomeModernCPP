---
chapter: 1
cpp_standard:
- 17
- 20
description: "Building the flat_tree core skeleton: the sorted-vector adapter, a key-extractor policy, the ordered invariant, a nested value_compare, and how flat_map/flat_set inherit it"
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'flat_map hands-on (I): motivation and API design'
- 'flat_map prerequisite (I): std::vector internals and growth'
- 'flat_map prerequisite (V): NO_UNIQUE_ADDRESS, EBO, and pair storage'
reading_time_minutes: 12
related:
- 'flat_map hands-on (III): lookup and insert'
tags:
- host
- cpp-modern
- intermediate
- 容器
- map
- 内存管理
title: "flat_map hands-on (II): the flat_tree core skeleton"
translation:
  source: documents/vol9-open-source-project-learn/chrome/03_flat_map/full/03-2-flat-map-flattree-skeleton.md
  source_hash: fca4f5d83cd7264730c970abc0f722bf11cdfbcf498cb1c99412bb4280c15fff
  translated_at: '2026-09-26T02:40:14+00:00'
  engine: anthropic
  token_count: 3300
---
# flat_map hands-on (II): the flat_tree core skeleton

In the previous piece we pinned down flat_map's target API and mentioned in passing that the whole thing really comes down to one class: `flat_tree`. This time we want to walk you through building the skeleton of that "ordered-array associative container adapter" with your own hands. Once it's standing, you'll run into a rather interesting number: Chromium's flat_set.h is 191 lines, all told. The set gets off that cheap because everything reusable has been swallowed by flat_tree — there is almost no code left for flat_set itself to write.

flat_tree manages to serve both map and set from one skeleton, and it pulls that off with three tricks working together. The first is a generic key-extractor policy (`GetKeyFromValue`): handed the same value, it can pull out the key or return the value unchanged, and that is what decides whether the container wears a map hat or a set hat. The second is the ordered invariant: after every mutation it quietly patches the ordering back up by itself. The third is a nested `value_compare` that translates a comparison on values back into a comparison on keys. Let's take them apart one at a time.

## The flat_tree template signature

`flat_tree`'s template signature (flat_tree.h:104-105) looks like this:

```cpp
template <class Key, class GetKeyFromValue, class KeyCompare, class Container>
class flat_tree {
protected:
    Container body_;                                  // underlying sorted container (vector by default)
    [[no_unique_address]] KeyCompare comp_;           // key comparator (zero overhead via EBO)
    // ...
};
```

Four template parameters; let's go through them one at a time. `Key` is the key type — no suspense there. `GetKeyFromValue` is the secret weapon of this design: it is a function object exposing `const Key& operator()(const Value&)` — handed a value (a `pair<K,V>` for map, plain `K` for set), it hands back the key. The same flat_tree can act as a map or as a set, and the entire difference lands on this one typename; we'll see the concrete spellings further down. `KeyCompare` is the key comparator, defaulting to `std::less<>`. `Container` is the underlying sequence container, defaulting to `std::vector` — `vector<pair<K,V>>` for map, `vector<K>` for set.

There are just two data members: `body_` is the underlying container, `comp_` the comparator. `comp_` carries `[[no_unique_address]]` so that an empty comparator costs zero bytes; we already walked through the ins and outs of that in [pre-05](./pre-05-flat-map-enua-ebo-and-pair-storage.md), so we won't repeat it here.

---

## The key extractor: GetFirst vs std::identity

The key extractor is what lets map and set share one codebase. Let's look at the two concrete implementations.

On the flat_map side it is `GetFirst` (flat_map.h:24-29):

```cpp
struct GetFirst {
    template <class Key, class Mapped>
    constexpr const Key& operator()(const std::pair<Key, Mapped>& p) const {
        return p.first;   // the value is a pair, take first as the key
    }
};
```

flat_set has it even easier: it just takes `std::identity` straight out of the standard library, which returns its argument unchanged:

```cpp
// flat_set.h:163 is equivalent to:
using flat_set = flat_tree<Key, std::identity, Compare, std::vector<Key>>;
// std::identity's operator()(const T&) returns T itself — the value is the key
```

Whenever flat_tree needs to compare two values internally, it first calls the extractor on each to get the key, then compares the keys with `comp_`. So with one and the same flat_tree codebase, `GetFirst` makes it treat `pair<K,V>` as a map, and `std::identity` makes it treat `K` as a set. We did a double-take the first time we read this — the entire implementation fork between map and set comes down to a single typename.

---

## value_compare: translating a value comparison into a key comparison

flat_tree also hands the outside world a nested `value_compare` (flat_tree.h:122-130). Its purpose is to let external code compare by value — say you want to feed an array of values straight into `std::sort`; for that you need a functor that compares values in hand:

```cpp
struct value_compare {
    constexpr bool operator()(const value_type& left, const value_type& right) const {
        GetKeyFromValue extractor;
        return comp(extractor(left), extractor(right));   // pull each key, then compare
    }
    [[no_unique_address]] key_compare comp;   // EBO again
};
```

Its whole job is "extract the key on both sides, then hand them to `comp`". For map that means comparing the `first` of two pairs; for set it means comparing the two keys themselves (the extractor is identity, a straight passthrough). This nested struct is what lets flat_tree offer a comparison interface at the value level while reusing the same key comparator underneath — no separate machinery written just for values.

---

## The ordered invariant: sorted and unique after every mutation

The single core invariant flat_tree guards is this: `body_` is always strictly ascending under `comp_`, with no duplicates. The invariant is maintained in two places — one bulk sort at construction time, and a per-element guard at insertion time.

### Construction: sort_and_unique

The plain constructor (taking unordered data) calls `sort_and_unique` (flat_tree.h:147-149; the implementation lives at 567/578/586/594):

```cpp
void sort_and_unique() {
    std::stable_sort(body_.begin(), body_.end(), value_comp());   // sort (O(N log N))
    auto it = std::ranges::unique(body_, equiv);                  // dedup (equiv = !comp && !comp)
    body_.erase(it.end(), body_.end());                           // chop off the duplicate tail
}
```

`stable_sort` orders by `value_comp`, then `unique` moves the equivalent elements to the end, and `erase` chops off that tail. A word on why this is `stable_sort` rather than `sort`: if equivalent elements (several values under the same key) arrived in some order, `stable_sort` preserves their relative order — flat_map keeps only one of them after dedup anyway, but the stability semantics are safer in certain edge cases (when the value carries state, for instance). Once construction is done, `body_` sits in a clean, sorted, duplicate-free state.

### Single-point insert: lower_bound + insert

When a single element is inserted at runtime (flat_tree.h:1060, `unsafe_emplace`), the code first finds the position with `lower_bound` (keeping things ordered), then `insert`s there. `lower_bound` finds "the first position not less than the key", so inserting right there keeps the ordering for free; if the key is already present, `lower_bound` points at that equal element, and the `unique` semantics then demand the insert be rejected, to avoid a duplicate. The exact mechanics of this find-plus-insert, and the shift cost that genuinely hurts, we save for the detailed walkthrough in 03-3.

---

## Constructors: plain vs sorted_unique

flat_tree's constructors split into two families, and behind that split hides a performance trade-off. Let's pull it apart.

The plain family: you pass unordered data in, and it dutifully calls `sort_and_unique` internally:

```cpp
flat_tree(InputIterator first, InputIterator last, const Compare& comp) {
    body_.insert(body_.end(), first, last);
    sort_and_unique();   // sort + dedup
}
```

The sorted_unique family: you stake your word via a `sorted_unique_t` tag that the data is already sorted and unique, and it skips the sort, running only a DCHECK:

```cpp
flat_tree(sorted_unique_t, InputIterator first, InputIterator last, const Compare& comp) {
    body_.insert(body_.end(), first, last);
    DCHECK(is_sorted_and_unique(body_, comp));   // debug-only check, no sort
}
```

The entire difference between the two families comes down to one `sort_and_unique` call: either it really sorts, or it takes your word. The latter saves an O(N log N) pass when you know the data source is already ordered (moving data over from another sorted container, for instance), and it is the escape hatch flat_tree leaves open for performance-sensitive scenarios. The full story of tag dispatch as a mechanism we told in [pre-04](./pre-04-flat-map-tag-dispatch-and-sorted-unique.md).

---

## How flat_map / flat_set inherit flat_tree

With the flat_tree skeleton in place, there is almost nothing left to write on top of it for flat_map and flat_set.

flat_map (flat_map.h:194-195) goes the inheritance route and fills in the one key extractor it needs:

```cpp
template <class Key, class Mapped, class Compare = std::less<>,
          class Container = std::vector<std::pair<Key, Mapped>>>
class flat_map : public flat_tree<Key, internal::GetFirst, Compare, Container> {
    // inherits all of flat_tree's generic operations (find/insert/erase/lower_bound...)
    // adds only the map-specific ones: operator[], at, insert_or_assign, try_emplace
};
```

flat_set (flat_set.h:159-163) is more blunt still: it can't even be bothered to define a class and just makes an alias:

```cpp
template <class Key, class Compare = std::less<>,
          class Container = std::vector<Key>>
using flat_set = flat_tree<Key, std::identity, Compare, Container>;
// no code of its own — a set is just a flat_tree with "key=value"
```

The handful of map-specific operations flat_map adds (`operator[]`/`at`/`insert_or_assign`/`try_emplace`) we're saving for 03-3, together with lookup and insertion. flat_set, where the key is the value, truly has nothing to add — a single `using` and the story is told. Look back at those 191 lines of flat_set.h now, and you can feel how much leverage this abstraction buys.

---

## A minimal flat_tree replica

Just reading Chromium's code isn't quite satisfying, so let's roll a minimal version ourselves and get a first-hand feel for how those two tricks — key extractor plus ordered invariant — mesh:

```cpp
// Platform: host | C++ Standard: C++20
#include <algorithm>
#include <functional>
#include <iostream>
#include <utility>
#include <vector>

namespace tamcpp::chrome::internal {

template <class Key, class GetKeyFromValue, class KeyCompare, class Container>
class flat_tree {
public:
    using value_type = typename Container::value_type;
    using iterator = typename Container::iterator;
    using const_iterator = typename Container::const_iterator;

    // Plain constructor: unordered data, sorted and deduped internally
    flat_tree(Container data, KeyCompare comp = KeyCompare())
        : body_(std::move(data)), comp_(comp) {
        sort_and_unique();
    }

    // Lookup: O(log n) binary search
    const_iterator find(const Key& key) const {
        auto it = std::ranges::lower_bound(
            body_, key, comp_,
            [](const value_type& v) { return GetKeyFromValue{}(v); });
        if (it != body_.end() && !comp_(key, GetKeyFromValue{}(*it))) return it;
        return body_.end();
    }

    std::size_t size() const { return body_.size(); }
    const value_type& front() const { return body_.front(); }

private:
    void sort_and_unique() {
        GetKeyFromValue ext;
        std::stable_sort(body_.begin(), body_.end(),
                         [&](const value_type& a, const value_type& b) {
                             return comp_(ext(a), ext(b));
                         });
        body_.erase(std::unique(body_.begin(), body_.end(),
                                [&](const value_type& a, const value_type& b) {
                                    auto ka = ext(a), kb = ext(b);
                                    return !comp_(ka, kb) && !comp_(kb, ka);
                                }),
                    body_.end());
    }

    Container body_;
    [[no_unique_address]] KeyCompare comp_;
};

}  // namespace tamcpp::chrome::internal
```

This minimal version holds on to two things: sort-and-dedup at construction, and binary search at lookup. The key-extractor policy runs through both `find` and `sort_and_unique` — you can watch it translating value into key in each. The next step is adding insertion and erasure, and that shift cost is flat_map's real soft spot.

---

## Assembling map and set out of flat_tree

```cpp
// map: stores pair<K,V>, pulls the key with GetFirst
struct GetFirst {
    template <class K, class V>
    constexpr const K& operator()(const std::pair<K, V>& p) const { return p.first; }
};

template <class K, class V>
using mini_flat_map = internal::flat_tree<K, GetFirst, std::less<>,
                                          std::vector<std::pair<K, V>>>;

// set: stores K, pulls the key with std::identity
template <class K>
using mini_flat_set = internal::flat_tree<K, std::identity, std::less<>, std::vector<K>>;

int main() {
    mini_flat_map<int, std::string> m{std::vector<std::pair<int, std::string>>{
        {2, "b"}, {1, "a"}, {3, "c"}}};
    std::cout << m.size() << " elements, front key=" << m.front().first << "\n";   // 3, 1 (sorted)

    mini_flat_set<int> s{std::vector<int>{3, 1, 2, 1}};   // the duplicate 1 gets deduped
    std::cout << s.size() << " elements\n";                                        // 3
    return 0;
}
```

Run it and you'll see `3 elements, front key=1` (the sort took effect) and `3 elements` (the dedup took effect). One flat_tree: put the `GetFirst` hat on and it's a map, swap in `std::identity` and it's a set.

---

With this, the skeleton is standing. The signature `flat_tree<Key, GetKeyFromValue, KeyCompare, Container>` is the common base of the ordered-array associative containers: the key-extractor policy decides whether it wears a map hat or a set hat; the ordered invariant is guarded by two gates, `sort_and_unique` during construction and `lower_bound + insert` during insertion; and `value_compare` translates value comparison back into key comparison. flat_map adds a handful of map-specific operations on top, and flat_set wraps up with a single `using` — that is how flat_set.h arrives at 191 lines.

Next we'll write flat_tree's lookup and insertion for real. The O(log n) binary search is the easy part; the O(n) shift is what we actually want to measure for you — just how much that cost hurts.

## References

- [Chromium `base/containers/flat_tree.h` — the flat_tree class and value_compare](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_tree.h)
- [Chromium `base/containers/flat_map.h` — GetFirst and the flat_map subclass](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_map.h)
- [Chromium `base/containers/flat_set.h` — the flat_set alias](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_set.h)
- [flat_map hands-on (I): motivation and API design](./03-1-flat-map-motivation-and-api-design.md)
