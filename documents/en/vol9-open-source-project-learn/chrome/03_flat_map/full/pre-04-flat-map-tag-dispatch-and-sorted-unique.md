---
chapter: 0
cpp_standard:
- 11
- 17
- 20
description: "Pull apart flat_map's sorted_unique_t tag dispatch: an empty tag type steers overload resolution away from sort_and_unique, with a DCHECK guarding the contract, all at zero runtime cost"
difficulty: intermediate
order: 4
platform: host
prerequisites:
- flat_map prerequisite (III): comparator, strict_weak_order, and transparent lookup
reading_time_minutes: 9
related:
- flat_map in practice (IV): sorted_unique construction optimization
tags:
- host
- cpp-modern
- intermediate
- 容器
- map
- 零开销抽象
title: "flat_map prerequisite (IV): tag dispatch and sorted_unique_t"
---
# flat_map prerequisite (IV): tag dispatch and sorted_unique_t

Back in [pre-02](./pre-02-flat-map-complexity-and-amortized.md) we planted a hook: flat_map's batch construction is `O(N log N)`, because it has to grab the data, sort it, then deduplicate (`sort_and_unique`). We said it then and the question writes itself: if the data is already sorted, isn't that sort a pure waste? Chromium's engineers thought the same thing, and their answer is clean. You hand a `sorted_unique_t` tag into the constructor, which is you telling flat_map "trust me, this batch is sorted and has no duplicates," and it skips the sort, dropping construction to `O(N)`.

This piece pulls apart the mechanism behind that tag, which is called tag dispatch, plus the way flat_map catches, in debug builds, the people who promise "sorted" and then hand in garbage through a `DCHECK`.

## The problem: does every batch construction have to sort

flat_map's ordinary constructors, whether you pass a vector, an initializer_list, or a range, all run `sort_and_unique` by default (flat_tree.h:567/578/586/594). It has no choice. It cannot know whether your data is already sorted, so it sorts and dedupes to be safe.

In real code, "the data is already sorted" shows up constantly. A config table copied out of another sorted container. A point cloud the upstream pipeline already sorted by id. A test fixture you wrote by hand as an ordered list. In all of these, paying `O(N log N)` to re-sort is pure CPU waste:

```cpp
std::vector<std::pair<int, Config>> raw = load_config();  // known to be sorted
flat_map<int, Config> m(raw.begin(), raw.end());           // sorts it again anyway!
```

`raw` is already sorted, and flat_map still spends `O(N log N)` sorting it. Small datasets, you do not notice. Push into the millions, and that logarithmic factor starts to hurt.

---

## Tag dispatch: pick the function by type

The pattern that solves this has a name: tag dispatch. The idea is almost embarrassingly plain. You define an empty "tag type," and whether or not you pass that tag at construction time steers the compiler, during overload resolution, toward different functions. The tag itself is an empty struct, it carries nothing at runtime, so the cost is zero.

This trick is all over the standard library. Pass `std::execution::par` to the parallel `std::sort` and it picks the parallel algorithm; leave it off and it picks the serial one. The tag carries no data. It is purely a "routing signal" for overload resolution. The iterator_category machinery is the same trick: hand `std::random_access_iterator_tag` into an overload set and the algorithm takes the random-access fast path.

### flat_map's sorted_unique_t

flat_map uses exactly this pattern (flat_tree.h:28-31):

```cpp
struct sorted_unique_t {
    constexpr sorted_unique_t() = default;
};

inline constexpr sorted_unique_t sorted_unique;
```

An empty struct with only the default constructor left in, plus a `constexpr` instance `sorted_unique`. When you build a flat_map, you slip `sorted_unique` in as the first argument, and overload resolution routes you to the constructor that skips the sort:

```cpp
std::vector<std::pair<int, Config>> raw = load_config();  // known to be sorted
flat_map<int, Config> m(sorted_unique, raw.begin(), raw.end());  // skips the sort!
```

The first argument is the tag, the data comes after. The tag carries no payload. Its entire job is to let the compiler say, during overload resolution, "oh, take the path that does not sort."

---

## The five sorted_unique overloads

flat_tree gives sorted_unique five constructor overloads (flat_tree.h:606-646), one for each input shape: InputIterator range, `from_range_t`, `const container_type&`, `container_type&&`, and `initializer_list`. They differ from the plain constructors in exactly one line: they do not call `sort_and_unique`.

```cpp
// Plain constructor (around flat_tree.h:567): sort and dedupe
flat_tree(InputIterator first, InputIterator last, ...) {
    insert(first, last);
    sort_and_unique();   // the expensive sort
}

// sorted_unique constructor (around flat_tree.h:606): skip the sort
flat_tree(sorted_unique_t, InputIterator first, InputIterator last, ...) {
    insert(first, last);
    DCHECK(is_sorted_and_unique(...));   // debug-only check, no sort
}
```

The only difference between the two parameter lists is the leading `sorted_unique_t`. The compiler routes on it during overload resolution. `sorted_unique_t` is an empty type, the instance takes no space, so passing it is the same as passing nothing. This "choice" does not cost you a single byte at runtime.

---

## DCHECK(is_sorted_and_unique): catch the liars in debug

Of course, someone who says "sorted" does not always hand in sorted data. flat_map does not just take your word for it. In debug builds it hangs a `DCHECK(is_sorted_and_unique(...))` (flat_tree.h:612/624/633/642) on the path as insurance. `is_sorted_and_unique` (flat_tree.h:55-62) looks like this:

```cpp
template <typename Range, typename Comp>
constexpr bool is_sorted_and_unique(const Range& range, Comp comp) {
    return std::ranges::adjacent_find(range, std::not_fn(comp)) ==
           std::ranges::end(range);
}
```

It pairs `std::ranges::adjacent_find` with `std::not_fn(comp)` and walks the adjacent pairs. The moment any adjacent pair fails "strictly less than" (equal, or out of order), `adjacent_find` lands on that position, the `DCHECK` trips, and you get an abort.

This is a very Chromium-flavored contract. In debug, flat_map checks the truth of your promise for you, and if you lied it blows up on the spot. In release, `DCHECK` compiles down to nothing, there is no check at all, and flat_map trusts you outright. `is_sorted_and_unique` is itself `O(N)` (one sweep over adjacent pairs), but you only pay that in debug. Release is a true `O(N)` construction: append, then take ownership, no sort, no check.

---

## Zero cost: release pays nothing

Add up the bill and it clears in one read. In a debug build, the sorted_unique constructor is append (`O(N)`) plus the `O(N)` `DCHECK(is_sorted_and_unique)` sweep, so still `O(N)`. In a release build, the `DCHECK` disappears, and the sorted_unique constructor is just append, pure `O(N)`.

Set that against the plain constructor: append (`O(N)`) plus `sort_and_unique` (`O(N log N)`, going through `stable_sort`). On a large dataset, `N log N` is slower than `N` by that logarithmic factor. At a million elements, that is roughly a 20x gap. So sorted_unique in release is a true zero-cost abstraction: you reach for it when you are sure the data is sorted, you save the `log N` factor, and in debug you get a free check thrown in for safety.

---

## A minimal re-implementation

Let us hand-roll a minimal tag dispatch, so the shape of "pick a function by type" is something you can feel directly:

```cpp
// Platform: host | C++ Standard: C++17
#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>

struct sorted_unique_t {};                        // empty tag type
inline constexpr sorted_unique_t sorted_unique{};  // constant instance

class MiniMap {
public:
    // Plain constructor: assume unsorted data, sort internally
    MiniMap(std::vector<int> data) : data_(std::move(data)) {
        std::sort(data_.begin(), data_.end());
        std::cout << "  [plain constructor] sorted\n";
    }
    // sorted_unique constructor: trust the caller, skip the sort (assert in debug)
    MiniMap(sorted_unique_t, std::vector<int> data) : data_(std::move(data)) {
        assert(is_sorted_unique());   // debug check
        std::cout << "  [sorted_unique constructor] skipped the sort\n";
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
    MiniMap m1(a);                                   // plain constructor, sorts

    std::vector<int> b = {1, 2, 3};                  // already sorted
    MiniMap m2(sorted_unique, b);                    // skips the sort
    return 0;
}
```

Run it and you see two lines: `[plain constructor] sorted` and `[sorted_unique constructor] skipped the sort`. The entire mechanism of tag dispatch lives in those two overload signatures. One takes an extra empty tag argument, the compiler routes on it, there is no runtime cost, and there is no clever trick to it.

That leaves one last piece in the prerequisite run: `[[no_unique_address]]`, the empty base optimization (EBO), and why flat_map stores `pair<K,V>` instead of `pair<const K,V>`.

## References

- [cppreference: tag dispatch](https://en.cppreference.com/w/cpp/named_req/TagDispatch)
- [cppreference: std::sort and the execution policy tag](https://en.cppreference.com/w/cpp/algorithm/sort)
- [Chromium `base/containers/flat_tree.h` — sorted_unique_t and the DCHECK check](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_tree.h)
