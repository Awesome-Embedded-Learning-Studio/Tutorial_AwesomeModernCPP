---
chapter: 1
cpp_standard:
- 17
- 20
description: "Implement flat_tree lookup (lower_bound, O(log n)) and insert (lower_bound + emplace, O(n) shift); cover flat_map's operator[]/insert_or_assign/try_emplace, and measure the shift cost by hand."
difficulty: intermediate
order: 3
platform: host
prerequisites:
- "flat_map hands-on (II): the flat_tree core skeleton"
- "flat_map prerequisite (II): complexity and amortized analysis"
reading_time_minutes: 12
related:
- "flat_map hands-on (IV): sorted_unique construction optimization"
tags:
- host
- cpp-modern
- intermediate
- 容器
- map
- 优化
title: "flat_map hands-on (III): lookup and insert"
---
# flat_map hands-on (III): lookup and insert

[03-2](./03-2-flat-map-flattree-skeleton.md) stood the skeleton up. This piece fills in the two things you'll actually do with it: how to look things up, and how to insert. One of these is flat_map's selling point, the other is its cost, and we'll take them apart separately.

Lookup is where flat_map genuinely shines. The data is already contiguous and sorted, so a binary search lands in `O(log n)`, and the memory it touches almost entirely sits in cache. The selling point is real. Insert is where you have to be careful. The `O(n)` shift isn't a documentation warning to scare you; it bites. At the end we'll run an experiment so you can see that shift curve with your own eyes, and turn the abstract claim into a cost you can feel. Both operations stand on top of the complexity analysis from [pre-02](./pre-02-flat-map-complexity-and-amortized.md) and the vector behavior from [pre-01](./pre-01-flat-map-vector-internals-and-growth.md); if you haven't worked through those, circle back first.

## Lookup: lower_bound, O(log n)

flat_tree exposes a whole family of lookup interfaces (`find`/`contains`/`lower_bound`/`equal_range`), and they all converge on the same thing: a binary search. Chromium uses `std::ranges::lower_bound` with a `KeyValueCompare` comparator object (flat_tree.h:1027) to find the first position not less than the key on the sorted array:

```cpp
// Core of flat_tree::find (simplified; passes a single binary comparator (value,key)->bool)
const_iterator find(const Key& key) const {
    auto it = std::lower_bound(body_.begin(), body_.end(), key,
        [&](const value_type& v, const Key& k) { return comp_(GetKeyFromValue{}(v), k); });
    // lower_bound gives "first not-less-than key"; still need to confirm equality
    if (it != body_.end() && !comp_(key, GetKeyFromValue{}(*it))) return it;
    return body_.end();
}
```

> Note: `std::ranges::lower_bound(range, value, comp)` accepts only **one** comparator. Chromium's `KeyValueCompare` (flat_tree.h:439-462) is a class with **two `operator()` overloads** (v<k and k<v), passed as a single comparator object, not as two parallel lambdas. Our teaching version uses `std::lower_bound` (iterator pair) plus a single binary lambda; the semantics are equivalent, and it reads more directly.

Each step of the binary search halves the range, so `log₂(n)` comparisons. Each comparison first extracts the key (for a map that's `pair.first`, O(1)), then runs `comp_`. That step is cheap, and because the data is contiguous those comparisons almost entirely hit cache. This is the real reason flat_map lookup is fast: not just `O(log n)`, but each comparison is cheap too. The two together are what gives you the edge over std::map.

The remaining interfaces are easy to reason about. `contains(key)` is just `find(key) != end()`; `equal_range(key)` returns the `[lower_bound, upper_bound)` range; `count(key)` returns 0 or 1 for a unique-key container like flat_map. As a side note, if you want to be strict about it Chromium's `find` is actually implemented through `equal_range` one layer down; our teaching version goes straight to `lower_bound + equality check` to skip that layer, and the semantics are fully equivalent. flat_map inherits all of these from flat_tree as-is, and the behavior lines up with std::map.

---

## Insert: lower_bound + emplace, O(n) shift

Single-element insert (`insert`/`emplace`) goes through this path (flat_tree.h:1060, `unsafe_emplace`):

```cpp
// Core of flat_tree::insert (simplified)
std::pair<iterator, bool> insert(const value_type& value) {
    const Key& key = GetKeyFromValue{}(value);
    auto it = std::lower_bound(body_.begin(), body_.end(), key,
        [&](const value_type& v, const Key& k) { return comp_(GetKeyFromValue{}(v), k); });   // 1. find position, O(log n)
    if (it != body_.end() && !comp_(key, GetKeyFromValue{}(*it))) {
        return {it, false};   // key already present, don't insert (unique-key invariant)
    }
    auto inserted = body_.emplace(it, value);               // 2. insert, O(n) shift
    return {inserted, true};
}
```

Read the code and the two steps are clear: first use `lower_bound` to find where to insert, then `vector::emplace` constructs the element there. The second step is where the real cost lives. `vector::emplace(pos, value)` has to shift every element after `pos` back by one slot. Under the hood that's a `std::move_backward` relocation, then construct the new element in the freed slot. How much gets moved depends on the number of trailing elements, which averages to `n/2`, and the big-O is `O(n)`.

This is the bill flat_map insert has to settle every time: shift half the elements on every insert. Asymptotic complexity `O(n)`, and there's no amortization argument here. It isn't an occasional hit, it's every single insert; nothing escapes it.

---

## flat_map specifics: operator[], insert_or_assign, try_emplace

flat_tree itself is generic. flat_map layers a few map-specific operations on top of it. Let's walk through them one at a time.

### operator[] (flat_map.h:313, 326)

```cpp
mapped_type& operator[](const Key& key) {
    auto it = lower_bound(key);              // find the position
    if (it == end() || comp_(key, GetKeyFromValue{}(*it))) {
        it = unsafe_emplace(it, ...);        // missing -> insert a default-constructed mapped
    }
    return it->second;
}
```

What `m[key]` does: look it up, and if the key isn't there, insert a default-constructed `mapped_type()`, then return the reference; if it is there, just return the reference to the existing one. The semantics match `std::map::operator[]` exactly. One thing worth flagging on its own: it mutates the container (it can actually insert something), so it doesn't work on a `const flat_map`, and the compiler will catch that for you at build time.

### insert_or_assign (flat_map.h:334-355)

```cpp
template <class M>
std::pair<iterator, bool> insert_or_assign(const Key& key, M&& obj) {
    auto result = emplace_key_args(key, std::forward<M>(obj));   // try insert first
    if (!result.second) {
        // key already present -> overwrite mapped
        result.first->second = std::forward<M>(obj);             // assignment (needs pair<K,V> to be non-const!)
    }
    return result;
}
```

What `insert_or_assign(key, val)` does: if the key isn't there, insert it; if it is there, **overwrite the value**. It returns `{iterator, inserted_bool}`, where `inserted=false` means this was actually an overwrite.

When I first read the source, the line that actually stopped me was the overwrite afterwards, `result.first->second = forward<M>(obj)`. It relies on `pair`'s second being assignable. That is exactly why flat_map has to store `pair<K, V>` internally and not `pair<const K, V>`. The latter's second isn't assignable, and this path would be dead. This storage choice, which looks trivial, is a hard constraint pushed back out by the `insert_or_assign` API. I've put the details in [pre-05](./pre-05-flat-map-enua-ebo-and-pair-storage.md).

### try_emplace (flat_map.h:392-413)

```cpp
template <class... Args>
std::pair<iterator, bool> try_emplace(const Key& key, Args&&... args) {
    // Construct mapped(args...) only when the key is absent
    auto [it, inserted] = emplace_key_args(key, std::piecewise_construct,
                                           std::forward_as_tuple(key),
                                           std::forward_as_tuple(std::forward<Args>(args)...));
    return {it, inserted};
}
```

`try_emplace(key, args...)` has the opposite temperament from the one above: only when the key is absent does it use `args...` to construct mapped; if the key is already there, it **leaves the existing value completely untouched**. That's the real difference between it and `insert_or_assign`, one overwrites, the other ignores. The implementation has a little subtlety to it: it uses `std::piecewise_construct + forward_as_tuple` to defer the pair's construction to the moment it's actually needed, so the mapped you pass in doesn't get constructed for nothing and then thrown away in the "key already exists" case.

---

## erase: O(n) shift

Don't stare only at insert; erase is also `O(n)`. It's the symmetric cost of contiguous storage. `erase` is handed straight to vector (flat_tree.h:914/921, `body_.erase`):

```cpp
iterator erase(const_iterator pos) {
    return body_.erase(pos);   // vector::erase, shifts following elements forward by one, O(n)
}
```

`erase(pos)` removes one position, `erase(first, last)` removes a range, and both do the same thing: shift the following elements forward by one slot as a batch. The `erase(key)` overload has one extra step. It has to `lower_bound` to find the position first (`O(log n)`), then `erase` to shift elements (`O(n)`), which together are `O(n) + O(log n)`, and the big-O is still `O(n)`.

---

## Measured: how expensive is that O(n) shift, really

Saying `O(n)` probably gives you a feel for it, but not the pain. Let's run an experiment and turn that shift curve from an abstract claim into a cost you can see. The idea is simple: insert at the head of a vector 100,000 times (`emplace(begin)`), so every insert shifts all the following elements back by one; then compare against `push_back` at the tail (amortized `O(1)`):

```cpp
// Platform: host | C++ Standard: C++17
#include <chrono>
#include <iostream>
#include <vector>

int main() {
    constexpr int N = 100'000;

    // Head insert: O(n) shift every time
    std::vector<int> a;
    auto t1 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; ++i) a.emplace(a.begin(), i);
    auto t2 = std::chrono::steady_clock::now();
    std::cout << "emplace(begin) x" << N << ": "
              << std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count()
              << " ms\n";

    // Tail insert: amortized O(1)
    std::vector<int> b;
    auto t3 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; ++i) b.push_back(i);
    auto t4 = std::chrono::steady_clock::now();
    std::cout << "push_back      x" << N << ": "
              << std::chrono::duration_cast<std::chrono::milliseconds>(t4 - t3).count()
              << " ms\n";
    return 0;
}
```

Real output on this machine (GCC 16, -O2):

```text
emplace(begin) x100000: 264 ms
push_back      x100000: 0 ms
```

Two orders of magnitude apart, sitting right there in front of you. That's the curve you step on when you treat flat_map like std::map and insert into it frequently. Every insert pays the bill for that 264ms `emplace(begin)` trajectory, proportionally.

So why did the earlier pieces keep repeating the "read-heavy, write-light" usage precondition? It isn't a documentation pleasantry. It's a hard constraint forced out by this O(n) shift curve. Write too much and you'll watch the performance curve crawl toward that 264ms trajectory with your own eyes.

---

## Tying it together: a complete lookup-and-insert example

```cpp
// Using the mini_flat_map from 03-2
mini_flat_map<int, std::string> m{std::vector<std::pair<int, std::string>>{
    {1, "one"}, {3, "three"}, {5, "five"}}};

auto it = m.find(3);
if (it != m.end()) std::cout << it->second << "\n";   // three

// Insert (sorted position decided automatically, O(n) shift)
// Here shown with flat_tree's insert (simplified)
// m.insert({4, "four"});  // inserts between 3 and 5, shifts 5
```

The zero-cost construction path for flat_map is saved for later, how `sorted_unique` skips the sort_and_unique step.

## References

- [Chromium `base/containers/flat_tree.h`: lower_bound / unsafe_emplace / erase](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_tree.h)
- [Chromium `base/containers/flat_map.h`: operator[]/insert_or_assign/try_emplace](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_map.h)
- [cppreference: std::lower_bound](https://en.cppreference.com/w/cpp/algorithm/lower_bound)
- [flat_map prerequisite (II): complexity and amortized analysis](./pre-02-flat-map-complexity-and-amortized.md)
