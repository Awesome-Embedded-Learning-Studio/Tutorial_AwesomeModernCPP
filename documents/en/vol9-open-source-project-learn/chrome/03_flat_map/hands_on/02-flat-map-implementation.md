---
chapter: 1
cpp_standard:
- 17
- 20
description: "Implement flat_tree/flat_map layer by layer: signatures, the key extractor, sort_and_unique, lookup and insert, sorted_unique construction, and the flat_map-specific APIs. Code-dense, minimal filler."
difficulty: advanced
order: 2
platform: host
prerequisites:
- flat_map Design Guide (I): motivation, API, and the flat_tree architecture
- flat_map prerequisite (IV): tag dispatch and sorted_unique_t
reading_time_minutes: 13
related:
- flat_map Design Guide (III): test strategy and performance comparison
tags:
- host
- cpp-modern
- advanced
- 容器
- map
- 优化
title: "flat_map Design Guide (II): step-by-step implementation"
translation:
  source: documents/vol9-open-source-project-learn/chrome/03_flat_map/hands_on/02-flat-map-implementation.md
  source_hash: e768f89509917ac74b64c331811468914da54f9670b4f1ac3bbdd5d892aa42a3
  translated_at: '2026-09-26T03:43:13+00:00'
  engine: anthropic
  token_count: 6400
---
# flat_map Design Guide (II): step-by-step implementation

In the previous piece we talked the motivation and the interface through; this time we want a different approach: no more armchair strategizing, let's write `flat_tree` / `flat_map` out line by line. We'll stack it up layer by layer — starting from the class signature and data members at the very bottom, piling all the way up to the handful of APIs specific to `flat_map` itself. Code-dense, with explanations kept to the point; for the detailed reasoning, go read [full/03-2~03-4](../full/03-2-flat-map-flattree-skeleton.md). The companion project lives in `code/volumn_codes/vol9/full_tutorial_codes/chrome_design/` (`19` through `22`); we ran its tests the whole time we were writing.

## Layer 1: stand the skeleton up first

When we first sat down to build, we laid out the `flat_tree` class signature and its data members before anything else — with an unsteady foundation, everything above it is a pitfall.

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
    [[no_unique_address]] KeyCompare comp_;   // EBO: a stateless comparator costs zero bytes

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

`[[no_unique_address]]` makes an empty comparator (the default `std::less<>`) cost zero bytes — the first time we saw that it genuinely felt elegant: an empty object just evaporates into nothing. `extract_if_value` is the linchpin of heterogeneous comparison: values go through the extractor, bare keys pass through untouched, so you can query a `pair<int,string>` table with a plain `int` without wrapping it first.

## Layer 2: construction, and the sort_and_unique you can't dodge

With the skeleton up, the next thing to solve is "a pile of unordered stuff comes in — how does it become a sorted, deduplicated table". That is `sort_and_unique`.

```cpp
// Plain range constructor: append + sort and dedupe
template <class InputIt>
flat_tree(InputIt first, InputIt last, const KeyCompare& c = KeyCompare())
    : body_(first, last), comp_(c) {
    sort_and_unique();
}
// Container move constructor: bulk construction (the recommended posture)
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

`stable_sort` + `unique` + `erase`, O(N log N). Here's a point we didn't pay much attention to at first: why `stable_sort` rather than `sort`? Because the relative order of equivalent elements — `stable_sort` keeps it for you, `sort` guarantees nothing. If data you later `replace` back in depends on that order, `sort` may well have shuffled it away for you. The equality test inside `unique`'s lambda is `!less(a,b) && !less(b,a)`, meaning "neither less than, nor less-than'd by the other" — which is exactly the definition of equivalence, and it holds up better under heterogeneous comparison than a plain `==`.

## Layer 3: the sorted_unique constructor — a back door for data already in order

This is where it gets interesting. If the data in your hands is already sorted and deduplicated (poured out of another `flat_map`, say), running `sort_and_unique` over it again is pure waste. Chromium leaves a back door open for exactly this scenario: the `sorted_unique` tag.

```cpp
struct sorted_unique_t {};
inline constexpr sorted_unique_t sorted_unique{};

template <class InputIt>
flat_tree(sorted_unique_t, InputIt first, InputIt last, const KeyCompare& c = KeyCompare())
    : body_(first, last), comp_(c) {
    assert(is_sorted_unique());   // debug-only verification, no sorting
}

bool is_sorted_unique() const {
    for (size_type i = 1; i < body_.size(); ++i)
        if (!less(body_[i - 1], body_[i])) return false;   // must be strictly ascending
    return true;
}
```

The tag steers overload resolution past `sort_and_unique`; all that remains is a DCHECK. An O(N) copy construction, and in release builds even that `assert` is gone. The first time we read this, our heart skipped a beat — isn't this pushing the contract onto you? It is. If you stuff a pile of unsorted data into the `sorted_unique` constructor, a debug build can still catch it; a release build goes straight to silent corruption. It's an honest contract: you're expected to know what you're doing.

## Layer 4: lookup, binary search's business

Lookup in a sorted array holds no suspense: `std::lower_bound`, binary search, O(log n). But one detail here is worth stopping for a look.

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

`std::lower_bound` binary-searches in O(log n) and takes only **one** binary comparator `(value, key)→bool` — with a transparent comparator, `key` can be a heterogeneous type, so you can query a `std::string` table with a `std::string_view` without converting first. That `!less(key, *it)` line in `find` is the equality trick: `lower_bound` hands you "the first position not less than key"; if that position is neither less than key nor is key less than it, they're equal — return it; otherwise return `end()`.

One small pitfall from our own writing, worth flagging for you: Chromium's flat_tree uses `std::ranges::lower_bound(*this, key, KeyValueCompare(comp_))`. That `KeyValueCompare` is a comparator class with **two `operator()` overloads** (one each for `v<k` and `k<v`), not two parallel lambdas; `ranges::lower_bound` likewise accepts only a single comparator object. Our "one lambda + `extract_if_value`" form above is a teaching simplification — behaviorally equivalent, but the Chromium version shoulders the corner cases of heterogeneous queries better.

## Layer 5: insert — that O(n) shift, no escaping it

Insert is the powder keg at the center of the flat_map performance debate. The design piece argued this out earlier; here we land it.

```cpp
std::pair<iterator, bool> insert(value_type v) {
    auto it = std::lower_bound(body_.begin(), body_.end(), v,
        [this](const value_type& a, const value_type& b) { return less(a, b); });
    if (it != body_.end() && !less(v, *it)) return {it, false};   // already present, don't insert
    return {body_.emplace(it, std::move(v)), true};               // O(n) shift, insert succeeds
}
```

`lower_bound` finds the position in O(log n); `emplace` shifts every element after that position, O(n). That is the price of a flat_map insert — drop one into the middle of a vector and everything behind it has to move. The unique-key semantics are baked in too: if the key already exists, return `{it, false}` and don't insert. One thing we want to stress at this step: do look at the `bool` that comes back. We were lazy and ignored it when we first wrote this, then spent ages debugging before realizing duplicate inserts had been silently swallowed.

## Layer 6: extract and replace — the two wrenches of bulk rebuild

Insert is single-element fine work, but sometimes you want to pour a whole batch in at once, or swap the entire container out. That's what `extract` and `replace` are for.

```cpp
container_type extract() && {
    return std::exchange(body_, container_type{});   // hand the whole thing over
}
void replace(container_type&& body) {
    body_ = std::move(body);
    assert(is_sorted_unique());   // the sorted_unique-style honest contract
}
iterator erase(const_iterator pos) { return body_.erase(pos); }   // O(n)
```

`extract` is `&&`-qualified — it only applies to rvalues. The meaning is "the container empties itself out; you take the contents and keep them; it's left a hollow shell". `replace` walks the same honest-contract line as `sorted_unique`: it trusts that the data you pass in is sorted and deduplicated, `assert`s it only in debug, and in release simply takes it over. These two wrenches compose nicely — if you want to bulk-update a flat_map, you can `extract` it first, mutate things outside in whatever order you like, sort and dedupe, then `replace` it back. Far faster than calling `insert` over and over.

## Layer 7: the few APIs flat_map calls its own

The first six layers are all `flat_tree`'s business, indifferent to map versus set. What truly belongs to `flat_map` is just what follows: operator[], at, insert_or_assign. That's also the point of the `flat_tree` / `flat_map` layering — one core engine, with map wearing a thin shell on top.

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
    using base::base;   // inherit flat_tree's constructors/lookup/insert

    mapped_type& operator[](const Key& key) {
        auto it = std::lower_bound(this->body_.begin(), this->body_.end(), key,
            [this](const value_type& v, const Key& k) { return this->less(v, k); });
        if (it == this->body_.end() || this->less(key, *it))
            it = this->body_.emplace(it, std::piecewise_construct,
                                     std::forward_as_tuple(key),
                                     std::forward_as_tuple());   // default-construct the mapped
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

`operator[]` inserts a default-constructed mapped when the key is missing — and that is exactly why flat_map uses `vector<pair<Key, Mapped>>` rather than `vector<pair<const Key, Mapped>>`: it has to be able to default-construct into place, and `const Key` can't do that job. A missing key in `at` hits an assert, which keeps the teaching build simple; Chromium uses CHECK, because release builds must crash too. `insert_or_assign` is an interesting API — if the key is there it overwrites `.second`, if not it inserts, and the returned bool tells you which of the two actually happened. And the last line, `flat_set`, is one we're especially fond of: a `using` alias + a `std::identity` extractor, zero extra code. That's the dividend of sinking the core into `flat_tree`.

## Run it, and see whether it moves

Writing it without running it leaves us uneasy. Here's a minimal slice — construct, look up, mutate, plus a set on the side.

```cpp
#include <iostream>
int main() {
    using namespace tamcpp::chrome;
    flat_map<int, std::string> m{{3,"c"},{1,"a"},{2,"b"}};   // construction sorts
    std::cout << m.size() << "," << m[1] << "\n";            // 3,a
    m.insert_or_assign(2, "B");                              // overwrite 2
    std::cout << m[2] << "\n";                               // B

    flat_set<int> s{{3,1,2,1}};                              // sort and dedupe
    std::cout << s.size() << "\n";                           // 3
    return 0;
}
```

And with that, all seven layers are done. The `flat_tree` layer is the real implementation core; `flat_map` (subclass + `GetFirst`) and `flat_set` (alias + `std::identity`) are both thin shells wrapped around it. The points we stumbled into along the way — `sort_and_unique` maintaining the sorted invariant, `lower_bound` for binary search, that O(n) shift inside `emplace`, `sorted_unique` skipping the sort, `extract`/`replace` for bulk rebuild, `[[no_unique_address]]` evaporating the empty comparator — that's the entire internal strength of flat_map. The code itself isn't much, but there's a story behind every trade-off, which is exactly what made this piece more and more fun to write. In the next piece we add the tests and the performance comparison, and see how much it really differs from `std::map` in a fair, sharp-edged fight.

## References

- [Chromium `base/containers/flat_tree.h`](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_tree.h)
- [Chromium `base/containers/flat_map.h`](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_map.h)
- [flat_map Design Guide (III): test strategy and performance comparison](./03-flat-map-testing.md)
- [flat_map hands-on (II): the flat_tree core skeleton](../full/03-2-flat-map-flattree-skeleton.md)
