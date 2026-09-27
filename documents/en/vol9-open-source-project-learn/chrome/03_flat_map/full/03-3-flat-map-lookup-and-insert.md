---
chapter: 1
cpp_standard:
- 17
- 20
description: 'Implement flat_tree lookup (lower_bound, O(lg n)) and insert (lower_bound + emplace, O(n) shift), cover flat_map''s operator[]/insert_or_assign/try_emplace, and measure the real cost of the shift'
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 'flat_map hands-on (II): the flat_tree core skeleton'
- 'flat_map prerequisite (II): complexity and amortized analysis'
reading_time_minutes: 12
related:
- 'flat_map hands-on (IV): sorted_unique construction optimization'
tags:
- host
- cpp-modern
- intermediate
- 容器
- map
- 优化
title: 'flat_map hands-on (III): lookup and insert'
translation:
  source: documents/vol9-open-source-project-learn/chrome/03_flat_map/full/03-3-flat-map-lookup-and-insert.md
  source_hash: c0b3dba58e2a891fbd316c18d1eb807e7b89b6a981a2129d68dcf671889d975d
  translated_at: '2026-09-26T02:32:13+00:00'
  engine: anthropic
  token_count: 6200
---
# flat_map hands-on (III): lookup and insert

[03-2](./03-2-flat-map-flattree-skeleton.md) stood the skeleton up; in this piece we fill it with the two things you'll actually use: how to look up, and how to insert. One of the two is flat_map's selling point, the other is its cost, and we'll take them apart one at a time.

flat_map does lookup beautifully. The data is already contiguous and sorted, so a binary search lands `O(log n)`, and nearly all of it feeds from cache. The selling point is real. Insert, though, is where you have to be careful—the `O(n)` shift is not something written in the docs to scare you, it genuinely bites. At the end we'll run an experiment so you can see with your own eyes what that shift curve looks like, turning the abstract conclusion into a cost you can feel. Both operations stand on the complexity analysis from [pre-02](./pre-02-flat-map-complexity-and-amortized.md) and the vector behavior from [pre-01](./pre-01-flat-map-vector-internals-and-growth.md); if you haven't cleared those prerequisites, circle back and pick them up first.

## Lookup: lower_bound, O(lg n)

flat_tree has a whole pile of lookup interfaces (`find`/`contains`/`lower_bound`/`equal_range`), and underneath they all converge on the same thing: a binary search. Chromium uses `std::ranges::lower_bound` together with a `KeyValueCompare` comparator object (flat_tree.h:1027) to find, on the sorted array, the first position not less than the key:

```cpp
// Core of flat_tree::find (simplified; passes a single binary comparator (value,key)->bool)
const_iterator find(const Key& key) const {
    auto it = std::lower_bound(body_.begin(), body_.end(), key,
        [&](const value_type& v, const Key& k) { return comp_(GetKeyFromValue{}(v), k); });
    // lower_bound gives us "the first not less than key"; we still have to confirm it is truly equal
    if (it != body_.end() && !comp_(key, GetKeyFromValue{}(*it))) return it;
    return body_.end();
}
```

> Note: `std::ranges::lower_bound(range, value, comp)` accepts only **one** comparator. Chromium's `KeyValueCompare` (flat_tree.h:439-462) is a class with **two `operator()` overloads** (v<k and k<v), passed as one whole comparator object—not two parallel lambdas. Our teaching version uses `std::lower_bound` (iterator pair) plus a single binary lambda; the semantics are equivalent and it reads more directly.

Each step of the binary search cuts the range in half, so `log₂(n)` comparisons. Each comparison first extracts the key with the extractor (for a map that's `pair.first`, O(1)), then runs `comp_`. That step is cheap, and because the data is contiguous, those comparisons hit cache almost every time. This is exactly why flat_map lookup is fast: it isn't just `O(log n)`—every single comparison is cheap too, and the two together are what produce that edge over std::map.

The remaining interfaces are all easy to reason about. `contains(key)` is just `find(key) != end()`; `equal_range(key)` returns the `[lower_bound, upper_bound)` range; `count(key)` returns 0 or 1 for a unique-key container like flat_map. As a side note, if you want to be strict about it, Chromium's `find` actually goes through `equal_range` one layer down; in our teaching version we skip that layer and go straight to `lower_bound` plus an equality check, which is fully equivalent semantically. flat_map inherits all of these interfaces from flat_tree as-is, and the behavior lines up with std::map.

---

## Insert: lower_bound + emplace, O(n) shift

Single-element insert (`insert`/`emplace`) goes through this path (flat_tree.h:1060, `unsafe_emplace`):

```cpp
// Core of flat_tree::insert (simplified)
std::pair<iterator, bool> insert(const value_type& value) {
    const Key& key = GetKeyFromValue{}(value);
    auto it = std::lower_bound(body_.begin(), body_.end(), key,
        [&](const value_type& v, const Key& k) { return comp_(GetKeyFromValue{}(v), k); });   // 1. find the position, O(log n)
    if (it != body_.end() && !comp_(key, GetKeyFromValue{}(*it))) {
        return {it, false};   // key already present, don't insert (unique-key invariant)
    }
    auto inserted = body_.emplace(it, value);               // 2. insert, O(n) shift
    return {inserted, true};
}
```

One look at the code says it all—just two steps: first `lower_bound` to find where the element belongs, then `vector::emplace` constructs it right there. The second step is where the real cost lives. `vector::emplace(pos, value)` has to shift every element after `pos` back by one slot as a batch—under the hood that's a `std::move_backward` relocation, and then the new element is constructed in the freed slot. How much gets moved is decided by the number of elements behind the position, which averages out to `n/2`, or `O(n)` in big-O terms.

This is the bill flat_map has to accept on every insert: shifting half the elements each time. Asymptotic complexity `O(n)`, and there is no amortization argument to invoke here—it isn't an occasional shift, it's every single insert; not one of them escapes.

---

## flat_map specifics: operator[], insert_or_assign, try_emplace

flat_tree itself is generic; flat_map layers a few map-specific operations on top of it. Let's take them one at a time.

### The operator[] implementation (flat_map.h:313, 326)

```cpp
mapped_type& operator[](const Key& key) {
    auto it = lower_bound(key);              // find the position
    if (it == end() || comp_(key, GetKeyFromValue{}(*it))) {
        it = unsafe_emplace(it, ...);        // not present -> insert a default-constructed mapped
    }
    return it->second;
}
```

What `m[key]` does: look the key up; if it doesn't exist, insert a default-constructed `mapped_type()`, then return the reference; if it exists, just return a reference to the one already there. The semantics match `std::map::operator[]` exactly. One thing we want to flag separately—it mutates the container (it can genuinely insert something), so it doesn't work on a `const flat_map`, and the compiler will stop you at compile time.

### insert_or_assign (flat_map.h:334-355)

```cpp
template <class M>
std::pair<iterator, bool> insert_or_assign(const Key& key, M&& obj) {
    auto result = emplace_key_args(key, std::forward<M>(obj));   // try inserting first
    if (!result.second) {
        // key already present -> overwrite mapped
        result.first->second = std::forward<M>(obj);             // assignment (needs pair<K,V> to be non-const!)
    }
    return result;
}
```

The behavior of `insert_or_assign(key, val)`: if the key isn't there, insert it; if it is there, **overwrite the value**. It returns `{iterator, inserted_bool}`, where `inserted=false` means this call was actually an overwrite.

When we first read the source, what actually tripped us up was that overwrite line afterwards—`result.first->second = forward<M>(obj)`. It relies on `pair`'s second being assignable. This is exactly why flat_map must store `pair<K, V>` internally instead of `pair<const K, V>`—with the latter, second isn't assignable, and this path would be blocked dead. This seemingly inconsequential storage choice is a hard constraint reverse-engineered from the `insert_or_assign` API; the details live in [pre-05](./pre-05-flat-map-enua-ebo-and-pair-storage.md).

### try_emplace (flat_map.h:392-413)

```cpp
template <class... Args>
std::pair<iterator, bool> try_emplace(const Key& key, Args&&... args) {
    // Only construct mapped(args...) when the key is absent
    auto [it, inserted] = emplace_key_args(key, std::piecewise_construct,
                                           std::forward_as_tuple(key),
                                           std::forward_as_tuple(std::forward<Args>(args)...));
    return {it, inserted};
}
```

`try_emplace(key, args...)` has the opposite temperament from the one above: only when the key is absent does it construct mapped from `args...`; if the key is already there, it **leaves the existing value completely untouched**. That is the essential difference from `insert_or_assign`—one overwrites, the other ignores. The implementation has a bit of finesse to it: it uses `std::piecewise_construct + forward_as_tuple` to defer the pair's construction to the moment it is truly needed, so the mapped you pass in doesn't get constructed for nothing and then thrown away in the "key already exists" case.

---

## erase: O(n) shift

Don't fixate on insert—erase is `O(n)` too. It's the symmetric cost of contiguous storage. `erase` is forwarded straight to vector (flat_tree.h:914/921, `body_.erase`):

```cpp
iterator erase(const_iterator pos) {
    return body_.erase(pos);   // vector::erase, shifts the following elements forward by one slot, O(n)
}
```

`erase(pos)` removes one position and `erase(first, last)` removes a range, but both do the same thing: shift the following elements forward by one slot as a batch. The `erase(key)` overload involves one extra step—it has to `lower_bound` to find the position first (`O(log n)`), then `erase` to shift the elements (`O(n)`), which adds up to `O(n) + O(log n)`, and the big-O is still `O(n)`.

---

## Measured: how expensive that O(n) shift really is

Just saying `O(n)` probably gives you a feel for it, but not the pain. Let's run an experiment and turn that shift curve from an abstract conclusion into a cost visible to the naked eye. The idea is simple: insert at the head of a vector 100,000 times (`emplace(begin)`), shifting all the following elements back by one slot every time; then compare against `push_back` at the tail (amortized `O(1)`):

```cpp
// Platform: host | C++ Standard: C++17
#include <chrono>
#include <iostream>
#include <vector>

int main() {
    constexpr int N = 100'000;

    // Head insertion: O(n) shift every time
    std::vector<int> a;
    auto t1 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; ++i) a.emplace(a.begin(), i);
    auto t2 = std::chrono::steady_clock::now();
    std::cout << "emplace(begin) x" << N << ": "
              << std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count()
              << " ms\n";

    // Tail insertion: amortized O(1)
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

Two orders of magnitude apart, sitting right there in front of you in the plainest way. That is the curve you step on when you treat flat_map like std::map and insert into it frequently—every insert pays a proportional share of that 264ms `emplace(begin)` bill.

So why do the earlier pieces keep droning on about the "read-heavy, write-light" usage precondition? It really isn't a courtesy phrase from the docs; it's a hard constraint forced out by this O(n) shift curve. Write too much, and you'll watch with your own eyes as the performance curve crawls toward that 264ms trajectory.

---

## Tying it together: a complete lookup-and-insert example

```cpp
// Using the mini_flat_map from 03-2
mini_flat_map<int, std::string> m{std::vector<std::pair<int, std::string>>{
    {1, "one"}, {3, "three"}, {5, "five"}}};

auto it = m.find(3);
if (it != m.end()) std::cout << it->second << "\n";   // three

// Insert (the sorted position is decided automatically, O(n) shift)
// Shown here with flat_tree's insert (simplified)
// m.insert({4, "four"});  // inserts between 3 and 5, shifting 5
```

flat_map's zero-cost construction is left for later—how `sorted_unique` skips the sort_and_unique step.

## References

- [Chromium `base/containers/flat_tree.h` — lower_bound / unsafe_emplace / erase](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_tree.h)
- [Chromium `base/containers/flat_map.h` — operator[]/insert_or_assign/try_emplace](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_map.h)
- [cppreference: std::lower_bound](https://en.cppreference.com/w/cpp/algorithm/lower_bound)
- [flat_map prerequisite (II): complexity and amortized analysis](./pre-02-flat-map-complexity-and-amortized.md)
