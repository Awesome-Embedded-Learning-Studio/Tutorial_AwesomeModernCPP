---
chapter: 1
cpp_standard:
- 17
- 20
description: "Build flat_tree/flat_map layer by layer: signatures, key extractor, sort_and_unique, lookup and insert, sorted_unique construction, and the flat_map-specific API. Code-dense, little filler."
difficulty: advanced
order: 2
platform: host
prerequisites:
- flat_map design guide (I): motivation, API, and the flat_tree architecture
- flat_map prerequisite (IV): tag dispatch and sorted_unique_t
reading_time_minutes: 13
related:
- flat_map design guide (III): test strategy and performance comparison
tags:
- host
- cpp-modern
- advanced
- 容器
- map
- 优化
title: "flat_map Design Guide (II): Step-by-Step Implementation"
---
# flat_map Design Guide (II): Step-by-Step Implementation

The previous piece walked through the motivation and the interface. This time we stop arguing on paper and just write `flat_tree` and `flat_map` line by line. We'll stack layers from the bottom up, starting with the class signature and data members, climbing all the way to the few APIs that belong to `flat_map` itself. Code is dense, explanations only point at the load-bearing parts; for the full reasoning behind each choice, see [full/03-2~03-4](../full/03-2-flat-map-flattree-skeleton.md). The companion project lives in `code/volumn_codes/vol9/full_tutorial_codes/chrome_design/` (`19` through `22`); we kept the tests running next to the editor the whole time.

## Layer 1: stand the skeleton up

The first thing to nail down is the `flat_tree` class signature and its data members. Get the foundation wrong and everything above it leaks.

```cpp
// Platform: host | C++ Standard: C++20
#pragma once
#include <algorithm>
#include <cassert>
#include <functional>
#include <ranges>
#include <utility>
#include <vector>

namespace tamcpp::chrome::internal {

template <class Key, class GetKeyFromValue, class KeyCompare, class Container>
class flat_tree {
public:
    using key_type = Key;
    using key_compare = KeyCompare;
    using value_type = typename Container::value_type;
    using iterator = typename Container::iterator;
    using const_iterator = typename Container::const_iterator;
    using container_type = Container;
    using size_type = typename Container::size_type;

    static constexpr bool is_transparent_comparator =
        requires { typename KeyCompare::is_transparent; };

protected:
    Container body_;
    [[no_unique_address]] KeyCompare comp_;   // EBO: stateless comparator is zero bytes

    flat_tree() = default;
    explicit flat_tree(const KeyCompare& c) : comp_(c) {}

    // Turn a key comparison into a value comparison (extract on both sides)
    template <typename A, typename B>
    bool less(const A& a, const B& b) const {
        GetKeyFromValue ext;
        return comp_(extract_if_value(ext, a), extract_if_value(ext, b));
    }
    template <typename Ext, typename V>
    static const auto& extract_if_value(Ext& ext, const V& v) {
        if constexpr (std::is_same_v<std::decay_t<V>, value_type>) return ext(v);
        else return v;
    }
};

}  // namespace tamcpp::chrome::internal
```

`[[no_unique_address]]` makes an empty comparator (the default `std::less<>`) cost zero bytes. The first time we saw that trick it genuinely felt elegant: an empty object just evaporates. `extract_if_value` is the hinge of heterogeneous comparison. A value goes through the extractor; a bare key passes through untouched, so you can query a `pair<int, string>` table with a plain `int` without wrapping it first.

## Layer 2: construction, and the sort_and_unique you can't avoid

With the skeleton up, the next problem is "a pile of unordered stuff comes in, how do we turn it into a sorted, deduped table." That's `sort_and_unique`.

```cpp
// Range constructor: append, then sort + dedupe
template <class InputIt>
flat_tree(InputIt first, InputIt last, const KeyCompare& c = KeyCompare())
    : body_(first, last), comp_(c) {
    sort_and_unique();
}
// Container move constructor: bulk build (the recommended posture)
flat_tree(Container&& body, const KeyCompare& c = KeyCompare())
    : body_(std::move(body)), comp_(c) {
    sort_and_unique();
}

void sort_and_unique() {
    std::stable_sort(body_.begin(), body_.end(),
                     [this](const value_type& a, const value_type& b) { return less(a, b); });
    body_.erase(std::unique(body_.begin(), body_.end(),
                            [this](const value_type& a, const value_type& b) {
                                return !less(a, b) && !less(b, a);
                            }),
                body_.end());
}
```

`stable_sort` plus `unique` plus `erase`, O(N log N). One point we initially glossed over: why `stable_sort` and not `sort`? Because `stable_sort` preserves the relative order of equal elements and `sort` does not. If data you later `replace` back in depends on that order, `sort` may have already shuffled it. The equality lambda inside `unique` uses `!less(a,b) && !less(b,a)`, meaning "neither less nor less-than," which is the definition of equivalence. It's more robust under heterogeneous comparison than a plain `==`.

## Layer 3: the sorted_unique constructor, a back door for data already in order

Here it gets interesting. If your data is already sorted and deduped (poured out of another `flat_map`, say), running `sort_and_unique` again is pure waste. Chromium leaves a back door for exactly this case: the `sorted_unique` tag.

```cpp
struct sorted_unique_t {};
inline constexpr sorted_unique_t sorted_unique{};

template <class InputIt>
flat_tree(sorted_unique_t, InputIt first, InputIt last, const KeyCompare& c = KeyCompare())
    : body_(first, last), comp_(c) {
    assert(is_sorted_unique());   // debug check, no sort
}

bool is_sorted_unique() const {
    for (size_type i = 1; i < body_.size(); ++i)
        if (!less(body_[i - 1], body_[i])) return false;   // must be strictly ascending
    return true;
}
```

The tag makes overload resolution skip `sort_and_unique` and only DCHECK. O(N) copy, and in release even that `assert` is gone. The first time through, this gave us a small jolt: it's pushing the contract onto you, isn't it. It is. If you feed unsorted data into a `sorted_unique` constructor, debug builds catch it; release builds go to silent corruption. It's an honest contract. You're expected to know what you're doing.

## Layer 4: lookup, the binary search

Lookup on a sorted array has no suspense: `std::lower_bound`, O(log n). One detail is worth stopping on, though.

```cpp
const_iterator find(const Key& key) const {
    auto it = std::lower_bound(body_.begin(), body_.end(), key,
        [this](const value_type& v, const Key& k) { return less(v, k); });
    if (it != body_.end() && !less(key, *it)) return it;
    return body_.end();
}
bool contains(const Key& key) const { return find(key) != body_.end(); }
size_type count(const Key& key) const { return contains(key) ? 1 : 0; }
```

`std::lower_bound` does the binary search in O(log n) and takes only **one** binary comparator `(value, key) -> bool`. With a transparent comparator the `key` can be a heterogeneous type, so querying a `std::string` table with a `std::string_view` needs no conversion. The `!less(key, *it)` line in `find` is the equality trick: `lower_bound` gives you "the first position not less than key," and if that position is not less than key and key is not less than it either, they're equivalent, so return it; otherwise return `end()`.

A small pitfall we hit while writing this: Chromium's flat_tree uses `std::ranges::lower_bound(*this, key, KeyValueCompare(comp_))`. That `KeyValueCompare` is a comparator class with **two `operator()` overloads** (one for `v < k`, one for `k < v`), not two parallel lambdas; `ranges::lower_bound` also accepts a single comparator object. The "one lambda plus `extract_if_value`" form above is a teaching simplification, behaviorally equivalent, but the Chromium version handles more corners of heterogeneous lookup.

## Layer 5: insert, the O(n) shift you can't dodge

Insert is the powder keg of flat_map performance debates. The design piece covered the argument; here we land it.

```cpp
std::pair<iterator, bool> insert(value_type v) {
    auto it = std::lower_bound(body_.begin(), body_.end(), v,
        [this](const value_type& a, const value_type& b) { return less(a, b); });
    if (it != body_.end() && !less(v, *it)) return {it, false};   // already present, don't insert
    return {body_.emplace(it, std::move(v)), true};               // O(n) shift, insert succeeds
}
```

`lower_bound` finds the spot in O(log n); `emplace` shifts every element after it, O(n). That's the cost of a flat_map insert: a vector insertion in the middle, the tail moves down. The unique-key semantics are baked in too: if the key already exists, return `{it, false}` and don't insert. One thing we want to underline: look at the `bool` that comes back. We got lazy early on and ignored it, then spent half a day debugging a silent duplicate insert that had been swallowed.

## Layer 6: extract and replace, the two wrenches for bulk rebuild

Insert is single-element surgery. Sometimes you want to pour a whole batch in, or swap the entire container out. That's what `extract` and `replace` are for.

```cpp
container_type extract() && {
    return std::exchange(body_, container_type{});   // hand the whole thing out
}
void replace(container_type&& body) {
    body_ = std::move(body);
    assert(is_sorted_unique());   // the sorted_unique honest contract
}
iterator erase(const_iterator pos) { return body_.erase(pos); }   // O(n)
```

`extract` is qualified `&&`, so it only applies to rvalues: the container empties itself out, you take the contents, it's left a shell. `replace` follows the same honest-contract line as `sorted_unique`: it trusts that the data you pass in is sorted and deduped, `assert`s it only in debug, and takes over directly in release. The two wrenches compose well. If you want to bulk-update a flat_map, you can `extract` it, mutate the contents in whatever order you like outside, sort and dedupe, then `replace` it back, far faster than calling `insert` over and over.

## Layer 7: the few APIs that actually belong to flat_map

The first six layers are all `flat_tree`'s business, indifferent to map versus set. What truly belongs to `flat_map` is just the following: `operator[]`, `at`, `insert_or_assign`. This is the point of splitting `flat_tree` and `flat_map`. One core engine, and map wears a thin shell on top.

```cpp
namespace tamcpp::chrome {

struct GetFirst {
    template <class K, class V>
    constexpr const K& operator()(const std::pair<K, V>& p) const { return p.first; }
};

template <class Key, class Mapped, class Compare = std::less<>,
          class Container = std::vector<std::pair<Key, Mapped>>>
class flat_map : public internal::flat_tree<Key, GetFirst, Compare, Container> {
    using base = internal::flat_tree<Key, GetFirst, Compare, Container>;
public:
    using mapped_type = Mapped;
    using base::base;   // inherit flat_tree's constructors / lookup / insert

    mapped_type& operator[](const Key& key) {
        auto it = std::lower_bound(this->body_.begin(), this->body_.end(), key,
            [this](const value_type& v, const Key& k) { return this->less(v, k); });
        if (it == this->body_.end() || this->less(key, *it))
            it = this->body_.emplace(it, std::piecewise_construct,
                                     std::forward_as_tuple(key),
                                     std::forward_as_tuple());   // default-construct mapped
        return it->second;
    }

    mapped_type& at(const Key& key) {
        auto it = this->find(key);
        assert(it != this->body_.end());   // teaching build uses assert; Chromium uses CHECK
        return it->second;
    }

    template <class M>
    std::pair<iterator, bool> insert_or_assign(const Key& key, M&& obj) {
        auto it = std::lower_bound(this->body_.begin(), this->body_.end(), key,
            [this](const value_type& v, const Key& k) { return this->less(v, k); });
        if (it != this->body_.end() && !this->less(key, *it)) {
            it->second = std::forward<M>(obj);   // overwrite .second (mapped only, key untouched)
            return {it, false};
        }
        return {this->body_.emplace(it, key, std::forward<M>(obj)), true};
    }
};

template <class Key, class Compare = std::less<>, class Container = std::vector<Key>>
using flat_set = internal::flat_tree<Key, std::identity, Compare, Container>;

}  // namespace tamcpp::chrome
```

`operator[]` inserts a default-constructed mapped when the key is missing, and that's exactly why flat_map uses `vector<pair<Key, Mapped>>` rather than `vector<pair<const Key, Mapped>>`: it has to default-construct in place, and `const Key` can't do that. `at` on a missing key hits an `assert`, which is fine for a teaching build; Chromium uses `CHECK` because release builds must crash too. `insert_or_assign` is the interesting one: if the key is present it overwrites `.second`, if not it inserts, and the returned `bool` tells you which path ran. The `flat_set` line at the bottom is one we're particularly fond of: a `using` alias plus a `std::identity` extractor, no extra code. That's the dividend of sinking the core into `flat_tree`.

## Run it, see it move

Writing it and not running it feels unsafe. Here's a minimum slice: construct, look up, mutate, and a set on the side.

```cpp
#include <iostream>
int main() {
    using namespace tamcpp::chrome;
    flat_map<int, std::string> m{{3,"c"},{1,"a"},{2,"b"}};   // construction sorts
    std::cout << m.size() << "," << m[1] << "\n";            // 3,a
    m.insert_or_assign(2, "B");                              // overwrite 2
    std::cout << m[2] << "\n";                               // B

    flat_set<int> s{{3,1,2,1}};                              // sort + dedupe
    std::cout << s.size() << "\n";                           // 3
    return 0;
}
```

That's all seven layers. `flat_tree` is the real implementation core; `flat_map` (subclass plus `GetFirst`) and `flat_set` (alias plus `std::identity`) are both thin shells over it. The points we hit along the way, `sort_and_unique` maintaining the sorted invariant, `lower_bound` for binary search, the O(n) shift inside `emplace`, `sorted_unique` skipping the sort, `extract`/`replace` for bulk rebuild, `[[no_unique_address]]` evaporating the empty comparator, that's the entire internal mechanics of flat_map. The code itself isn't much, but every choice behind it has a story, which is what made this piece fun to write. Next time we add the tests and the performance comparison, and see how it actually stacks up against `std::map`.

## References

- [Chromium `base/containers/flat_tree.h`](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_tree.h)
- [Chromium `base/containers/flat_map.h`](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_map.h)
- [flat_map design guide (III): test strategy and performance comparison](./03-flat-map-testing.md)
- [flat_map hands-on (II): the flat_tree core skeleton](../full/03-2-flat-map-flattree-skeleton.md)
