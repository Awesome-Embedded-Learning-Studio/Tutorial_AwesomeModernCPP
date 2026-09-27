---
chapter: 0
cpp_standard:
- 11
- 17
- 20
description: "Dissecting flat_map's sorted_unique_t tag dispatch — an empty tag type picks a function at overload-resolution time, skipping sort_and_unique, with DCHECK as a defensive check, all at zero runtime cost"
difficulty: intermediate
order: 4
platform: host
prerequisites:
- 'flat_map prerequisite (III): comparators, strict_weak_order, and transparent lookup'
reading_time_minutes: 9
related:
- 'flat_map hands-on (IV): sorted_unique construction optimization'
tags:
- host
- cpp-modern
- intermediate
- 容器
- map
- 零开销抽象
title: "flat_map prerequisite (IV): tag dispatch and sorted_unique_t"
translation:
  source: documents/vol9-open-source-project-learn/chrome/03_flat_map/full/pre-04-flat-map-tag-dispatch-and-sorted-unique.md
  source_hash: dd2ae3ef75a16e2b774619767d20dcad564f03eec9260f38d6b92be963be2f54
  translated_at: '2026-09-26T02:54:55+00:00'
  engine: anthropic
  token_count: 2000
---
# flat_map prerequisite (IV): tag dispatch and sorted_unique_t

In [pre-02](./pre-02-flat-map-complexity-and-amortized.md) we left a hook dangling: flat_map's bulk construction is `O(N log N)`, because once it receives the data it has to sort it and then de-duplicate it (`sort_and_unique`). Writing that, we couldn't help muttering — if the data is already sorted to begin with, isn't that sort a pure waste? The Chromium engineers thought of the same thing, and their answer is quite clean: pass a `sorted_unique_t` tag when constructing, which amounts to telling the container "relax, this batch is sorted with no duplicates", and it skips the sorting step — construction drops straight to `O(N)`.

This article takes apart the mechanism behind that tag — tag dispatch — plus how flat_map uses `DCHECK` in debug builds to catch the people who promise sorted data with their mouths while handing over scrambled data with their hands.

## The problem: does bulk construction have to sort every time

flat_map's ordinary constructors — whether you pass a vector, an initializer_list, or a range — all run `sort_and_unique` by default (flat_tree.h:567/578/586/594). It has no choice: it has no idea whether the data you hand it is already sorted, so the safe play is to sort first, then de-duplicate.

But in real projects, "the data is already sorted" scenarios are everywhere. A config copied over from another ordered container; a point cloud already sorted by id by the upstream pipeline; a test fixture that is a hand-written sorted list. Forcing an `O(N log N)` sort in those cases is pure CPU burn:

```cpp
std::vector<std::pair<int, Config>> raw = load_config();  // known to be sorted
flat_map<int, Config> m(raw.begin(), raw.end());           // still sorts again!
```

`raw` is already sorted, yet flat_map still spends `O(N log N)` sorting it one more time. With a small dataset you won't care; once you get to the million-element scale, that logarithmic factor genuinely hurts.

---

## tag dispatch: picking a function by type

The trick that solves this has a name: tag dispatch. The idea is almost embarrassingly plain: define an empty "tag type", and whether or not you pass that tag at construction lets the compiler pick a different function during overload resolution. The tag itself is an empty struct; nothing is passed at runtime, so the cost is naturally zero.

You will find this move all over the standard library. For the parallel version of `std::sort`, pass `std::execution::par` and it selects the parallel algorithm; don't pass it and you get the serial one. The tag carries no data of its own — it is purely a "routing signal" for overload resolution. The whole iterator_category apparatus runs on the same trick: stuff `std::random_access_iterator_tag` into an overload set and the algorithm takes the random-access fast path.

### flat_map's sorted_unique_t

flat_map uses exactly this scheme (flat_tree.h:28-31):

```cpp
struct sorted_unique_t {
    constexpr sorted_unique_t() = default;
};

inline constexpr sorted_unique_t sorted_unique;
```

An empty struct (keeping only the default constructor), plus a `constexpr` instance `sorted_unique`. When constructing a flat_map, stuff `sorted_unique` in as the first argument, and overload resolution routes you to the constructor that "skips the sort":

```cpp
std::vector<std::pair<int, Config>> raw = load_config();  // known to be sorted
flat_map<int, Config> m(sorted_unique, raw.begin(), raw.end());  // skips the sort!
```

The first argument is the tag; the data comes after. The tag carries no payload — its entire reason for existing is to let the compiler say "ah, take the no-sorting path" during overload resolution.

---

## The five sorted_unique overloads

flat_tree ships five constructor overloads for sorted_unique (flat_tree.h:606-646), one for each input shape: an InputIterator range, `from_range_t`, `const container_type&`, `container_type&&`, and `initializer_list`. They differ from the ordinary constructors by a single line: they don't call `sort_and_unique`.

```cpp
// Ordinary constructor (around flat_tree.h:567): sort and de-duplicate
flat_tree(InputIterator first, InputIterator last, ...) {
    insert(first, last);
    sort_and_unique();   // the expensive sort
}

// sorted_unique constructor (around flat_tree.h:606): skip the sort
flat_tree(sorted_unique_t, InputIterator first, InputIterator last, ...) {
    insert(first, last);
    DCHECK(is_sorted_and_unique(...));   // debug-only check, no sorting
}
```

The only difference between the two parameter lists is the leading `sorted_unique_t`. That is what the compiler splits on during overload resolution. `sorted_unique_t` is an empty type, its instance takes no space, and passing it costs the same as passing nothing — this "choice" doesn't charge you a single byte at runtime.

---

## DCHECK(is_sorted_and_unique): catching liars in debug builds

But someone who says "sorted" isn't guaranteed to hand over data that actually is. flat_map doesn't take you on blind faith: in debug builds it hangs a `DCHECK(is_sorted_and_unique(...))` (flat_tree.h:612/624/633/642) as insurance. `is_sorted_and_unique` (flat_tree.h:55-62) looks like this:

```cpp
template <typename Range, typename Comp>
constexpr bool is_sorted_and_unique(const Range& range, Comp comp) {
    return std::ranges::adjacent_find(range, std::not_fn(comp)) ==
           std::ranges::end(range);
}
```

It walks the adjacent pairs with `std::ranges::adjacent_find` paired with `std::not_fn(comp)`: the moment any adjacent pair fails "strictly less than" (equal or reversed), `adjacent_find` lands on that position, the `DCHECK` turns on you, and you get an abort.

This is a very Chromium-style contract: in debug builds flat_map verifies the truth of your guarantee for you — lie and it blows up on the spot; in release builds the `DCHECK` compiles away entirely, no checking at all, straight trust. `is_sorted_and_unique` itself is `O(N)` (one pass over the adjacent pairs), but that cost is only paid in debug — release is a true `O(N)` construction: append, take over, no sorting and no checking.

---

## Zero cost: release pays nothing

Lay the costs out and the arithmetic is plain. In a debug build, the sorted_unique constructor is the append (`O(N)`) plus the `O(N)` check inside `DCHECK(is_sorted_and_unique)` — still `O(N)` combined. In a release build the `DCHECK` simply disappears, and the sorted_unique constructor reduces to the append alone, pure `O(N)`.

Compare with the ordinary constructor: append (`O(N)`) + `sort_and_unique` (`O(N log N)`, via stable_sort). On large datasets `N log N` is a full logarithmic factor slower than `N` — at a million elements, that is a 20x gap. So under release, sorted_unique is a genuinely zero-overhead abstraction: use it only when you are confident the data is sorted, save the `log N` factor, and debug builds even throw in a free safety check.

---

## A minimal reproduction

Let's build a minimal version of tag dispatch ourselves and get a hands-on feel for what "picking a function by type" actually looks like:

```cpp
// Platform: host | C++ Standard: C++17
#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>

struct sorted_unique_t {};                       // empty tag type
inline constexpr sorted_unique_t sorted_unique{}; // constant instance

class MiniMap {
public:
    // Ordinary constructor: assume unsorted data, sort internally
    MiniMap(std::vector<int> data) : data_(std::move(data)) {
        std::sort(data_.begin(), data_.end());
        std::cout << "  [普通构造] 排序了\n";
    }
    // sorted_unique constructor: trust the caller, skip the sort (an assert check can be added in debug)
    MiniMap(sorted_unique_t, std::vector<int> data) : data_(std::move(data)) {
        assert(is_sorted_unique());   // debug check
        std::cout << "  [sorted_unique 构造] 跳过排序\n";
    }
private:
    bool is_sorted_unique() const {
        for (size_t i = 1; i < data_.size(); ++i)
            if (!(data_[i - 1] < data_[i])) return false;   // must be strictly ascending
        return true;
    }
    std::vector<int> data_;
};

int main() {
    std::vector<int> a = {3, 1, 2};
    MiniMap m1(a);                                   // ordinary constructor, sorts

    std::vector<int> b = {1, 2, 3};                  // already sorted
    MiniMap m2(sorted_unique, b);                    // skips the sort
    return 0;
}
```

Run it and you will see two lines of output: `[普通构造] 排序了` and `[sorted_unique 构造] 跳过排序`. The entire mechanism of tag dispatch hides in those two overload signatures — one takes an extra empty tag parameter, the compiler routes on it, there is no runtime cost whatsoever, and no fancy tricks.

One last building block remains in the prerequisites: `[[no_unique_address]]`, empty base optimization (EBO), and why flat_map stores `pair<K,V>` instead of `pair<const K,V>`.

## References

- [cppreference: tag dispatch](https://en.cppreference.com/w/cpp/named_req/TagDispatch)
- [cppreference: std::sort and execution policy tags](https://en.cppreference.com/w/cpp/algorithm/sort)
- [Chromium `base/containers/flat_tree.h` — sorted_unique_t and the DCHECK check](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_tree.h)
