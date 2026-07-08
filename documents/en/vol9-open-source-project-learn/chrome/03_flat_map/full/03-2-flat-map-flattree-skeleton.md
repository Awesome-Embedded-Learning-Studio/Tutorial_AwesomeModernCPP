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
- 'flat_map prerequisite (1): std::vector internals and growth'
- 'flat_map prerequisite (V): NO_unique_ADDRESS, EBO, and pair storage'
reading_time_minutes: 12
related:
- 'flat_map hands-on (III): lookup and insertion'
tags:
- host
- cpp-modern
- intermediate
- 容器
- map
- 内存管理
title: "flat_map hands-on (II): the flat_tree core skeleton"
---
# flat_map hands-on (II): the flat_tree core skeleton

In the previous piece we pinned down flat_map's target API and offhandedly said the whole thing really comes down to one class: `flat_tree`. This time we build that "ordered-array associative container adapter" by hand. Once it's standing you'll notice a number that's hard to un-notice: Chromium's flat_set.h is 191 lines, total. The set gets off that cheaply because everything reusable is swallowed by flat_tree, and flat_set itself barely writes any code.

`flat_tree` serves both map and set from one skeleton through three tricks working together. The first is a generic key-extractor policy (`GetKeyFromValue`): given the same value type, it can pull out the key or hand the value back unchanged, and that choice is what decides whether the container wears a map or set hat. The second is the ordered invariant: after every mutation it quietly patches the ordering back up. The third is a nested `value_compare` that translates a comparison on values back into a comparison on keys. We'll take them one at a time.

## The flat_tree template signature

`flat_tree`'s signature (flat_tree.h:104-105) looks like this:

```cpp
template <class Key, class GetKeyFromValue, class KeyCompare, class Container>
class flat_tree {
protected:
    Container body_;                                  // underlying sorted container (default: vector)
    [[no_unique_address]] KeyCompare comp_;           // key comparator (EBO, zero overhead)
    // ...
};
```

Four template parameters, and we'll go through them. `Key` is the key type, no surprise there. `GetKeyFromValue` is the secret weapon of this design: it's a functor exposing `const Key& operator()(const Value&)`, so given a value (a `pair<K,V>` for map, a `K` itself for set) it returns the key. The same flat_tree can act as map or set, and the entire difference rides on this one typename; we'll see the concrete spellings below. `KeyCompare` is the key comparator, defaulting to `std::less<>`. `Container` is the underlying sequence container, defaulting to `std::vector`: map uses `vector<pair<K,V>>`, set uses `vector<K>`.

Two data members: `body_` is the underlying container, `comp_` is the comparator. `comp_` carries `[[no_unique_address]]` so an empty comparator costs zero bytes; we walked through the why of that in [pre-05](./pre-05-flat-map-enua-ebo-and-pair-storage.md), no need to repeat it here.

---

## The key extractor: GetFirst vs std::identity

The key extractor is what lets map and set share one codebase. Let's look at the two concrete implementations.

flat_map uses `GetFirst` (flat_map.h:24-29):

```cpp
struct GetFirst {
    template <class Key, class Mapped>
    constexpr const Key& operator()(const std::pair<Key, Mapped>& p) const {
        return p.first;   // value is a pair, take first as the key
    }
};
```

flat_set is even thriftier and just reuses the standard library's `std::identity`, returning its argument as-is:

```cpp
// flat_set.h:163 is equivalent to:
using flat_set = flat_tree<Key, std::identity, Compare, std::vector<Key>>;
// std::identity's operator()(const T&) returns T itself, so value is the key
```

When flat_tree needs to compare two values internally, it first calls the extractor on each to get the key, then compares keys with `comp_`. So the same flat_tree code, under `GetFirst`, treats `pair<K,V>` as a map, and under `std::identity` treats `K` as a set. That stopped us for a second when we first read it: the entire implementation fork between map and set lands on a single typename.

---

## value_compare: translating a value comparison into a key comparison

flat_tree also exposes a nested `value_compare` (flat_tree.h:122-130). Its job is to let the outside world compare by value; for instance, if you want to feed a value array straight to `std::sort`, you need a functor that compares values in hand:

```cpp
struct value_compare {
    constexpr bool operator()(const value_type& left, const value_type& right) const {
        GetKeyFromValue extractor;
        return comp(extractor(left), extractor(right));   // pull each key, then compare
    }
    [[no_unique_address]] key_compare comp;   // EBO again
};
```

What it does is "extract the key on both sides, then hand off to `comp`." For map that means comparing the `first` of two pairs; for set it means comparing the two keys directly (because the extractor is identity, a passthrough). This nested struct is what lets flat_tree offer a comparison interface at the value level while reusing the same key comparator underneath, with no need to write a separate value-comparison path.

---

## The ordered invariant: sorted and unique after every mutation

The single invariant flat_tree guards is this: `body_` is always strictly ascending under `comp_`, with no duplicates. It's maintained in two places: once during construction (a bulk sort), and once per insert (an incremental guard).

### Construction: sort_and_unique

A normal constructor (taking unordered input) calls `sort_and_unique` (flat_tree.h:147-149; implementation at 567/578/586/594):

```cpp
void sort_and_unique() {
    std::stable_sort(body_.begin(), body_.end(), value_comp());   // sort (O(N log N))
    auto it = std::ranges::unique(body_, equiv);                  // dedup (equiv = !comp && !comp)
    body_.erase(it.end(), body_.end());                           // chop off the duplicate tail
}
```

`stable_sort` orders by `value_comp`, then `unique` shuffles equivalent elements to the end, and `erase` lops off that tail. One note on why this is `stable_sort` rather than `sort`: if equivalent elements (multiple values under one key) had an original ordering, `stable_sort` preserves their relative order. flat_map only keeps one after dedup, but the stable semantics are safer in edge cases, for example when the value carries state. After construction, `body_` is in a clean sorted-and-unique state.

### Single-point insert: lower_bound + insert

Inserting one element at runtime (flat_tree.h:1060, `unsafe_emplace`) does a `lower_bound` to find the position (keeping order), then `insert`. lower_bound finds "the first position not less than the key," so inserting there stays sorted by construction; if the key is already present, `lower_bound` points at that equal element, and the `unique` contract requires the insert to be rejected to avoid a duplicate. The exact mechanics of this find-then-insert, and the genuinely painful shift cost that comes with it, we save for 03-3.

---

## Constructors: plain vs sorted_unique

flat_tree's constructors split into two families, and the split hides a real performance tradeoff. We'll pull it apart.

The plain family takes unordered data and dutifully calls `sort_and_unique` internally:

```cpp
flat_tree(InputIterator first, InputIterator last, const Compare& comp) {
    body_.insert(body_.end(), first, last);
    sort_and_unique();   // sort + dedup
}
```

The sorted_unique family takes a `sorted_unique_t` tag as your word that the data is already sorted and unique, and it skips the sort, running only a DCHECK:

```cpp
flat_tree(sorted_unique_t, InputIterator first, InputIterator last, const Compare& comp) {
    body_.insert(body_.end(), first, last);
    DCHECK(is_sorted_and_unique(body_, comp));   // debug-only check, no sort
}
```

The whole difference between the two families is that one `sort_and_unique` call: either it really sorts, or it takes your word. The latter skips an O(N log N) pass when you know the source is already ordered (moving out of another sorted container, for example), and it's the escape hatch flat_tree leaves for performance-sensitive paths. The mechanism behind tag dispatch itself we covered in [pre-04](./pre-04-flat-map-tag-dispatch-and-sorted-unique.md).

---

## How flat_map and flat_set inherit flat_tree

With the flat_tree skeleton in place, flat_map and flat_set have almost nothing left to write.

flat_map (flat_map.h:194-195) inherits and fills in its key extractor:

```cpp
template <class Key, class Mapped, class Compare = std::less<>,
          class Container = std::vector<std::pair<Key, Mapped>>>
class flat_map : public flat_tree<Key, internal::GetFirst, Compare, Container> {
    // inherits all the generic operations from flat_tree (find/insert/erase/lower_bound...)
    // adds only map-specific ones: operator[], at, insert_or_assign, try_emplace
};
```

flat_set (flat_set.h:159-163) goes further and doesn't even define a class, it's just an alias:

```cpp
template <class Key, class Compare = std::less<>,
          class Container = std::vector<Key>>
using flat_set = flat_tree<Key, std::identity, Compare, Container>;
// no code of its own, set is just flat_tree with key=value
```

The map-specific operations flat_map adds (`operator[]`, `at`, `insert_or_assign`, `try_emplace`) we'll cover in 03-3 alongside lookup and insertion. flat_set, where key is value, genuinely has nothing to add; one `using` and it's done. Look back at those 191 lines of flat_set.h and you can feel how much this abstraction buys you.

---

## A minimal flat_tree of our own

Reading Chromium's code only gets you so far, so let's build a minimal version by hand and feel how the "key extractor + ordered invariant" pair actually meshes:

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

    // Plain constructor: unordered data, sort + dedup internally
    flat_tree(Container data, KeyCompare comp = KeyCompare())
        : body_(std::move(data)), comp_(comp) {
        sort_and_unique();
    }

    // Lookup: O(log n) binary search
    const_iterator find(const Key& key) const {
        auto it = std::ranges::lower_bound(
            body_, key,
            [&](const value_type& v, const Key& k) { return comp_(GetKeyFromValue{}(v), k); },
            [&](const Key& k, const value_type& v) { return comp_(k, GetKeyFromValue{}(v)); });
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

This minimal version holds onto two things: sort-and-dedup at construction, and binary search at lookup. The key-extractor policy runs through both `find` and `sort_and_unique`, translating value to key each time. Next step is adding insertion and erasure, and that shift cost is flat_map's real soft spot.

---

## Assembling map and set out of flat_tree

```cpp
// map: stores pair<K,V>, extracts the key with GetFirst
struct GetFirst {
    template <class K, class V>
    constexpr const K& operator()(const std::pair<K, V>& p) const { return p.first; }
};

template <class K, class V>
using mini_flat_map = internal::flat_tree<K, GetFirst, std::less<>,
                                          std::vector<std::pair<K, V>>>;

// set: stores K, extracts the key with std::identity
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

Run it and you'll see `3 elements, front key=1` (sort worked) and `3 elements` (dedup worked). One flat_tree: under a `GetFirst` hat it's a map, swap in `std::identity` and it's a set.

---

The skeleton stands now. The signature `flat_tree<Key, GetKeyFromValue, KeyCompare, Container>` is the common base of the ordered-array associative containers; the key-extractor policy decides whether it wears a map or set hat; the ordered invariant is held by two gates, `sort_and_unique` at construction and `lower_bound + insert` at insertion; and `value_compare` translates value comparison back to key comparison. flat_map adds a handful of map-specific operations on top of this, and flat_set is done with a single `using`, which is how flat_set.h ends up at 191 lines.

Next we write flat_tree's lookup and insertion for real. The O(log n) binary search is the easy part; the O(n) shift is what we actually want to measure for you, because that cost is where the ache is.

## References

- [Chromium `base/containers/flat_tree.h`: the flat_tree class and value_compare](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_tree.h)
- [Chromium `base/containers/flat_map.h`: GetFirst and the flat_map subclass](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_map.h)
- [Chromium `base/containers/flat_set.h`: the flat_set alias](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_set.h)
- [flat_map hands-on (I): motivation and API design](./03-1-flat-map-motivation-and-api-design.md)
