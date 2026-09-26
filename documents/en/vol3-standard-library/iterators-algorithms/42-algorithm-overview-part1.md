---
chapter: 7
cpp_standard:
- 11
- 17
- 20
description: 'We split `<algorithm>` into four blocks — non-modifying, modifying, the
  erase-remove idiom, and sorted search — to lay out a selection strategy: why `for_each`
  never touches the range, why `remove` only shifts elements instead of truly deleting
  them, how C++20 `std::erase` deletes by value in one line, and why the `binary_search`
  family earns O(log n) — and why it demands sorting first.'
difficulty: intermediate
order: 42
platform: host
prerequisites:
- 'Iterator Basics and Categories: The Glue Between Containers and Algorithms'
- 'Iterator Adapters: Reverse, Insertion, and Stream — Teaching Old Iterators New Tricks'
- 'Deep Dive into std::vector: Three Pointers, Reallocation, and Iterator Invalidation'
reading_time_minutes: 14
related:
- 'Container Selection Guide: Choosing the Right Container Based on Operations, Memory, and Invalidation Rules'
tags:
- host
- cpp-modern
- intermediate
- 容器
title: 'Algorithm Overview (Part 1): Non-Modifying, Modifying, and Searching — How
  to Pick the Right Algorithm for a Problem'
translation:
  source: documents/vol3-standard-library/iterators-algorithms/42-algorithm-overview-part1.md
  source_hash: 26c99c21c5a2ece2c036f069a855f0576584f23f3f20a657da45b2ecb2c9dee2
  translated_at: '2026-09-26T01:33:55+00:00'
  engine: anthropic
  token_count: 11500
---
# Algorithm Overview (Part 1): Non-Modifying, Modifying, and Searching — How to Pick the Right Algorithm for a Problem

When we covered iterator adapters in the previous article, we casually used a little pattern — `lower_bound` to find the position, then `insert` to put the element there — sliding a new element into a sorted `vector` while keeping the order. That was actually an algorithm stepping onto the stage. Now we formally enter the `<algorithm>` volume.

`<algorithm>` is a huge chunk of the STL, packing more than eighty algorithms. Walking through API signatures one by one would turn this article into a dry manual — that is cppreference's job, not ours. So we take a more useful angle instead: **given a concrete requirement, which algorithm do you actually pick**. Group the whole pile by "what it does to your range", remember two or three representatives per group, keep the complexities in your head, and you can match problems to algorithms on sight.

This article covers the first four groups: **non-modifying** algorithms that only read, **modifying** algorithms that move elements around, the **erase-remove idiom** built specifically for "deleting elements" (plus how C++20 simplifies it), and the **binary search** family that depends on sorted ranges. Sorting, partitioning, and merging wait for the next article. All examples were run locally on GCC 16.1.1 with `-std=c++20 -O2`, and the outputs are real terminal logs.

## Non-Modifying: Read-Only, Not a Single Element Changed

The first group is the easiest to understand — sweep from beginning to end, read and never write. `for_each` for traversal, `find` following the trail, `count` for tallying, and the `any_of` squad doing predicate checks all belong here. What they share: the range looks exactly the same before and after the call, and complexity is basically O(n) (the binary family is the exception — we treat it separately later).

Let's first run a set of the most frequently used ones and see `for_each` / `find` / `find_if` / `count` / `any_of` / `all_of` / `none_of` all in one go:

```cpp
// Standard: C++20
#include <algorithm>
#include <iostream>
#include <vector>

int main()
{
    std::vector<int> v{3, 1, 4, 1, 5, 9, 2, 6};

    // for_each: read-only traversal, leaves the range untouched
    int sum = 0;
    std::for_each(v.begin(), v.end(), [&](int x) { sum += x; });
    std::cout << "for_each 求和: " << sum << '\n';

    // find: linear search, returns an iterator to the first element equal to the target
    auto it = std::find(v.begin(), v.end(), 5);
    std::cout << "find 5 -> 偏移 " << (it - v.begin()) << '\n';

    // find_if: the first one satisfying the predicate
    auto big = std::find_if(v.begin(), v.end(), [](int x) { return x > 7; });
    std::cout << "find_if(>7) -> " << (big != v.end() ? *big : -1) << '\n';

    // count / count_if
    std::cout << "count(1): " << std::count(v.begin(), v.end(), 1) << '\n';
    std::cout << "count_if(偶数): "
              << std::count_if(v.begin(), v.end(), [](int x) { return x % 2 == 0; }) << '\n';

    // none_of / any_of / all_of: return bool
    std::cout << "any_of(>8): " << std::any_of(v.begin(), v.end(), [](int x) { return x > 8; }) << '\n';
    std::cout << "all_of(<10): " << std::all_of(v.begin(), v.end(), [](int x) { return x < 10; }) << '\n';
    std::cout << "none_of(<0): " << std::none_of(v.begin(), v.end(), [](int x) { return x < 0; }) << '\n';

    return 0;
}
```

Here is what it prints:

```text
for_each 求和: 31
find 5 -> 偏移 4
find_if(>7) -> 9
count(1): 2
count_if(偶数): 3
any_of(>8): 1
all_of(<10): 1
none_of(<0): 1
```

Inside this family, the three brothers worth singling out are `any_of` / `all_of` / `none_of`. All of them **short-circuit** — `any_of` returns `true` the moment it finds the first element satisfying the predicate, instead of dumbly sweeping the whole range; `all_of` returns `false` the moment it hits the first element that fails the condition. So for "does the range contain any negative number", both `!std::all_of(..., [](x){return x>=0;})` and `std::any_of(..., [](x){return x<0;})` work; the latter reads more directly and fits the mindset of "this was an existence question to begin with".

One more that is easy to overlook but very practical: `std::search`. What it looks for is not a single element but a whole subsequence. Say you are looking for a word inside a stretch of text: `find` answers "does this single character equal the target", while `search` is the one that checks "does this substring equal the target sequence element by element":

```cpp
// Standard: C++20
#include <algorithm>
#include <iostream>
#include <string>

int main()
{
    std::string text = "hello world, hello again";
    std::string needle = "hello";
    auto it = std::search(text.begin(), text.end(), needle.begin(), needle.end());
    std::cout << "search(\"hello\") 第一次偏移: " << (it - text.begin()) << '\n';
    // continue from one past the previous match point to find the second occurrence
    auto it2 = std::search(it + 1, text.end(), needle.begin(), needle.end());
    std::cout << "search 第二次偏移:          " << (it2 - text.begin()) << '\n';
    return 0;
}
```

```text
search("hello") 第一次偏移: 0
search 第二次偏移:          13
```

::: warning Don't use find where you need search
`find` compares "a single element equals the target"; `search` compares "a whole sub-range matches element by element". Looking for a value inside a `vector<int>`? Use `find`. Looking for a contiguous subsequence (say, whether `[3, 4, 5]` is in there)? That takes `search`. Mix them up, and `find` hands you the first position whose element equals the head of the subsequence — a completely different thing from the full-segment match you wanted.
:::

## Modifying: Either Change In Place or Write Somewhere Else

The second group does touch the range. It comes in two styles: **modify in place** (replace and shuffle within the same range) and **write to a destination range** (source untouched, results written elsewhere — usually teamed up with the insert iterators from the previous article).

Again, let's run a set that walks through all the patterns:

```cpp
// Standard: C++20
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

void print(const std::vector<int>& v, const char* lbl)
{
    std::cout << lbl;
    for (int x : v) std::cout << x << ' ';
    std::cout << '\n';
}

int main()
{
    std::vector<int> src{1, 2, 3, 4, 5};

    // copy: replicate as-is into the destination range
    std::vector<int> copied;
    std::copy(src.begin(), src.end(), std::back_inserter(copied));
    print(copied, "copy:           ");

    // copy_if: copy with a condition
    std::vector<int> evens;
    std::copy_if(src.begin(), src.end(), std::back_inserter(evens),
                 [](int x) { return x % 2 == 0; });
    print(evens, "copy_if(偶数):  ");

    // transform: one-to-one mapping, writes each transformed element to the destination
    std::vector<int> squared;
    std::transform(src.begin(), src.end(), std::back_inserter(squared),
                   [](int x) { return x * x; });
    print(squared, "transform(x*x): ");

    // replace / replace_if: in-place overwrite of elements satisfying the condition with a new value
    std::vector<int> r{1, 2, 3, 2, 4, 2};
    std::replace(r.begin(), r.end(), 2, 99);
    print(r, "replace(2->99): ");

    std::vector<int> r2{1, 2, 3, 4, 5, 6};
    std::replace_if(r2.begin(), r2.end(), [](int x) { return x % 2 == 0; }, 0);
    print(r2, "replace_if(偶->0): ");

    // unique: drop adjacent duplicates in place (see the erase-remove section later for the key part)
    std::vector<int> u{1, 1, 2, 3, 3, 3, 4, 1, 1};
    auto new_end = std::unique(u.begin(), u.end());
    std::cout << "unique 后逻辑终点偏移: " << (new_end - u.begin())
              << " 实际 size 仍为 " << u.size() << '\n';

    // move: carry the elements away (as rvalues), destination takes ownership
    std::vector<std::string> words{"aa", "bb", "cc"};
    std::vector<std::string> moved;
    std::move(words.begin(), words.end(), std::back_inserter(moved));
    std::cout << "move 后源区间首元素 size: " << words[0].size() << '\n';

    return 0;
}
```

```text
copy:           1 2 3 4 5
copy_if(偶数):  2 4
transform(x*x): 1 4 9 16 25
replace(2->99): 1 99 3 99 4 99
replace_if(偶->0): 1 0 3 0 5 0
unique 后逻辑终点偏移: 5 实际 size 仍为 9
move 后源区间首元素 size: 0
```

Two "in-place vs. write-elsewhere" pairings in here are worth remembering:

- **Changing values**: in place, `replace` / `replace_if`; to land the result in a new range, `replace_copy` / `replace_copy_if` (the ones with `_copy` in the name are effectively "replace + copy in one step", leaving the source untouched).
- **Moving elements**: to reshuffle in place, `move` (it hauls the source elements away, leaving "moved-from" husks behind — `words[0].size()` dropping to 0 above is the evidence that the string's contents were carried off); to copy a transformed result into a new range, `transform`.

We will give `unique` its own section shortly, because it and `remove` are twin brothers carrying the same counter-intuitive design — **they only shift elements, never shrink the container**. That is one of the most classic STL traps, and it stars in the next section.

## The erase-remove Idiom: Why remove Doesn't Really Delete

This is the most classic design in the STL and the one most likely to trip up newcomers. The requirement is simple: delete every element equal to `2` from a `vector`. Your first instinct is probably to look for an algorithm named `remove` — and sure enough, `std::remove` exists. But it **does not actually delete anything**.

First, let's see what it actually does:

```cpp
// Standard: C++20
#include <algorithm>
#include <iostream>
#include <vector>

int main()
{
    std::vector<int> v{1, 2, 3, 2, 4, 2, 5};
    std::cout << "原始:                ";
    for (int x : v) std::cout << x << ' ';
    std::cout << "  [size=" << v.size() << "]\n";

    auto new_end = std::remove(v.begin(), v.end(), 2);
    std::cout << "remove(2) 后逻辑终点偏移: " << (new_end - v.begin()) << '\n';
    std::cout << "remove 后物理内容:    ";
    for (int x : v) std::cout << x << ' ';
    std::cout << "  [size 仍为 " << v.size() << "]\n";
    return 0;
}
```

```text
原始:                1 2 3 2 4 2 5   [size=7]
remove(2) 后逻辑终点偏移: 4
remove 后物理内容:    1 3 4 5 4 2 5   [size 仍为 7]
```

See what happened — what `remove` does is shift the elements "not equal to 2" forward, squeezing them into the front half of the range, and then return a **new logical end**. The physical size of the `vector` hasn't budged: still 7 elements, with the tail holding leftover stale values from the shuffle (the `4 2 5` crowd) — garbage that is "logically abandoned but still physically squatting in its slot".

### Why It Doesn't Just Delete: Algorithms Don't Know Containers

This design looks awkward, but once the reasoning is laid out it is actually sound: **`std::remove` knows iterators, not containers**. As we covered in the previous article, algorithms decouple from containers through the iterator interface — all `remove` receives is two iterators. It has no idea whether a `vector`, a `list`, or a `deque` hangs behind them, let alone whose `erase` it should call to actually shrink anything. Erasing is a container member function, none of an algorithm's business. So `remove` does only what is within its reach: move elements, return the new end, and hand the shrinking back to the caller.

So a real deletion takes two steps — let `remove` finish the shifting, then take the container's own `erase` and chop off the tail beyond the new end:

```cpp
v.erase(new_end, v.end());
```

```text
erase 后:             1 3 4 5   [size=4]
```

Those two steps together form the famous **erase-remove idiom**:

```cpp
v.erase(std::remove(v.begin(), v.end(), 2), v.end());
```

`unique` plays exactly the same game — it merely "squeezes out" adjacent duplicates, likewise shifting without shrinking, and a real deletion needs `erase` alongside it. The earlier `unique` output is the proof: the logical end sits at offset 5 while `size` is still 9; only after `u.erase(new_end, u.end())` do those elements truly disappear. So one mnemonic is enough: **`remove` / `unique` only shift; shrinking is always `erase`'s job**.

### C++20: `std::erase` / `erase_if` Get It Done in One Line

Typing that `erase(remove(...), end())` chain over and over gets genuinely annoying. C++20 ships a set of new free functions — `std::erase(c, value)` and `std::erase_if(c, pred)` — that take the container directly and delete either a value or every element matching a condition, run the whole erase-remove dance for you internally, and even hand back how many they removed:

```cpp
// Standard: C++20
#include <algorithm>
#include <iostream>
#include <vector>

void print(const std::vector<int>& v, const char* lbl)
{
    std::cout << lbl;
    for (int x : v) std::cout << x << ' ';
    std::cout << "  [size=" << v.size() << "]\n";
}

int main()
{
    std::vector<int> w{1, 2, 3, 2, 4, 2, 5};
    auto erased = std::erase(w, 2);
    std::cout << "std::erase(w, 2) 删了 " << erased << " 个\n";
    print(w, "结果:                 ");

    std::vector<int> x{1, 2, 3, 4, 5, 6, 7, 8};
    auto erased_if = std::erase_if(x, [](int n) { return n % 2 == 0; });
    std::cout << "std::erase_if(偶数) 删了 " << erased_if << " 个\n";
    print(x, "结果:                 ");

    return 0;
}
```

```text
std::erase(w, 2) 删了 3 个
结果:                 1 3 4 5   [size=4]
std::erase_if(偶数) 删了 4 个
结果:                 1 3 4 5 7   [size=4]
```

Much cleaner, isn't it. Now that these exist, can the old erase-remove idiom be forgotten entirely? **Not quite**. There is a scope-of-application detail here, verified by hands-on testing on the local GCC 16.1.1:

- **Sequence containers** (`vector` / `string` / `deque` / `list` / `forward_list`): **both** `erase(c, value)` and `erase_if(c, pred)` exist.
- **Associative containers** (`map` / `set` / `multimap` / `multiset` and their `unordered_` variants): **only `erase_if`; there is no value-based `erase`**.

Why no value-based `erase` for associative containers? Because they already have a member `c.erase(key)` that removes a node by key. If the free function `std::erase(c, value)` existed too, the names would collide while the semantics differ subtly, so the standards committee simply gave associative containers only `erase_if`. We tested on GCC 16.1.1: calling `std::erase(s, 2)` on a `std::set` flat-out fails to compile:

```text
error: no matching function for call to 'erase(std::set<int>&, int)'
  7 |     std::erase(s, 2);   // 关联容器: 只有 erase_if，没有按值的 erase
```

The error is blunt: no matching `erase` found. So remember one sentence — **for associative containers, removing elements means `erase_if`; for sequence containers, `erase` works for values and `erase_if` for conditions**. On a sequence container, if you can write one line, don't write that `erase(remove(...), end())` chain anymore.

::: warning ranges::remove returns a subrange, not a bare iterator
C++20 also brings the ranges version, `std::ranges::remove`. What it returns is no longer a bare "new-end iterator" but a `subrange` (a bundle combining the kept range and the discarded range as a pair of iterators). Paired with `erase`, write it like this:

```cpp
auto [first, last] = std::ranges::remove(v, 2);
v.erase(first, last);
```

Keeping this mentally mixed with the classic `v.erase(std::remove(...), v.end())` is a recipe for dizziness; fortunately, on sequence containers going straight to `std::erase` / `erase_if` is the cheapest one-liner, so the ranges flavor of remove is rarely worth writing day to day.
:::

## Sorted Search: The Binary Family — O(log n) on the Premise That the Range Is Already Sorted

Everything covered so far — `find`, `count` — is an O(n) linear sweep, and its true colors show once the data grows. Is there a faster lookup? Yes, on the condition that **the range is already sorted**. Once it is, binary search chops the complexity from O(n) down to O(log n).

The family has four members with different divisions of labor:

- `binary_search(first, last, v)` — answers only "is v in there", returning a `bool`.
- `lower_bound(first, last, v)` — returns the position of the first element **not less than** v (`>= v`).
- `upper_bound(first, last, v)` — returns the position of the first element **greater than** v (`> v`).
- `equal_range(first, last, v)` — returns `[lower, upper)` in one shot, i.e. the complete extent of v inside the range.

Read as prose, `lower_bound` and `upper_bound` are easy to confuse. Let's just run them and let the output speak:

```cpp
// Standard: C++20
#include <algorithm>
#include <iostream>
#include <vector>

int main()
{
    std::vector<int> v{1, 3, 3, 5, 7, 7, 7, 9};   // already sorted ascending

    // binary_search: present or not (bool)
    std::cout << "binary_search(7): " << std::binary_search(v.begin(), v.end(), 7) << '\n';
    std::cout << "binary_search(4): " << std::binary_search(v.begin(), v.end(), 4) << '\n';

    // lower_bound: position of the first element "not less than" value (>= value)
    auto lo = std::lower_bound(v.begin(), v.end(), 7);
    std::cout << "lower_bound(7) -> 偏移 " << (lo - v.begin()) << " 值 " << *lo << '\n';

    // upper_bound: position of the first element "greater than" value (> value)
    auto up = std::upper_bound(v.begin(), v.end(), 7);
    std::cout << "upper_bound(7) -> 偏移 " << (up - v.begin()) << " 值 " << *up << '\n';

    // equal_range: [lower, upper) is 7's complete extent
    auto [eq_lo, eq_up] = std::equal_range(v.begin(), v.end(), 7);
    std::cout << "equal_range(7): [" << (eq_lo - v.begin()) << ", " << (eq_up - v.begin()) << ") -> ";
    for (auto it = eq_lo; it != eq_up; ++it) std::cout << *it << ' ';
    std::cout << "共 " << (eq_up - eq_lo) << " 个\n";

    // searching for a value that isn't there: lower_bound tells you where it would go
    auto lo4 = std::lower_bound(v.begin(), v.end(), 4);
    std::cout << "lower_bound(4) -> 偏移 " << (lo4 - v.begin()) << " 值 " << *lo4
              << "（4 不在，指向插入点）\n";
    return 0;
}
```

```text
binary_search(7): 1
binary_search(4): 0
lower_bound(7) -> 偏移 4 值 7
upper_bound(7) -> 偏移 7 值 9
equal_range(7): [4, 7) -> 7 7 7 共 3 个
lower_bound(4) -> 偏移 3 值 5（4 不在，指向插入点）
```

Against the output it becomes clear: the three `7`s occupy offsets 4, 5, and 6; `lower_bound(7)` lands on the first `7` (offset 4, where `>= 7` starts); `upper_bound(7)` lands on the first `9` after the `7`s (offset 7, where `> 7` starts); and `equal_range` hands you the half-open range `[4, 7)` in one go. Querying an absent value like `4`, `lower_bound` lands at offset 3 (pointing at `5`) — exactly the spot "where 4 would go if we inserted it".

### Picking Up from the Previous Article: `insert_sorted` Is Just `lower_bound` + `insert`

Looking back now, that little "order-preserving insert" pattern from the previous article clicks completely. `lower_bound` finds the insertion point on a sorted range in O(log n), then the container's `insert` squeezes the element in. The shifting step is unavoidable (contiguous storage, O(n)), but locating the position has been pressed down to logarithmic by binary search:

```cpp
// Standard: C++20
#include <algorithm>
#include <iostream>
#include <vector>

int main()
{
    std::vector<int> sorted{1, 3, 5, 7, 9};
    int new_val = 4;
    auto pos = std::lower_bound(sorted.begin(), sorted.end(), new_val);
    sorted.insert(pos, new_val);
    std::cout << "insert_sorted(4): ";
    for (int x : sorted) std::cout << x << ' ';
    std::cout << '\n';
    return 0;
}
```

```text
insert_sorted(4): 1 3 4 5 7 9
```

### How Much Faster Is Binary Than Linear: Run It and See

Just saying "O(log n) beats O(n)" is a bit hollow. Let's take a sorted `vector` of ten million elements and pit `find` against `binary_search` in the worst case (target at the very end) to see the real gap:

```cpp
// Standard: C++20
#include <algorithm>
#include <chrono>
#include <iostream>
#include <vector>

int main()
{
    constexpr int kN = 10'000'000;
    std::vector<int> v(kN);
    for (int i = 0; i < kN; ++i) v[i] = i;   // already sorted ascending

    int target = kN - 1;   // worst case: at the end

    auto t1 = std::chrono::high_resolution_clock::now();
    bool found_lin = std::find(v.begin(), v.end(), target) != v.end();
    auto t2 = std::chrono::high_resolution_clock::now();
    bool found_bin = std::binary_search(v.begin(), v.end(), target);
    auto t3 = std::chrono::high_resolution_clock::now();

    auto us_lin = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
    auto us_bin = std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count();

    std::cout << "find        (O(n))      " << found_lin << "  耗时 " << us_lin << " us\n";
    std::cout << "binary_search (O(log n)) " << found_bin << "  耗时 " << us_bin << " us\n";
    std::cout << "倍数差距: " << (us_bin > 0 ? us_lin / us_bin : -1) << "x\n";
    return 0;
}
```

Run locally on GCC 16.1.1 with `-O2` (a single measurement; the exact microseconds wobble with machine and run, but the order of magnitude is stable):

```text
find        (O(n))      1  耗时 5891 us
binary_search (O(log n)) 1  耗时 1 us
倍数差距: 5891x
```

Want to run it yourself and see the magnitude gap? Open this online demo:

<OnlineCompilerDemo
  title="Binary vs. Linear Search: The Dividend of O(log n)"
  source-path="code/examples/vol3/42_binary_vs_linear.cpp"
  description="Ten million sorted elements, worst case (target at the end): std::find has to sweep to the end (milliseconds), while std::binary_search lands it in a handful of comparisons (microseconds) — a gap of several thousand times in magnitude, provided the data really is sorted."
  allow-run
/>

With ten million elements, a linear `find` in the worst case has to sweep all the way to the end and lands in milliseconds; binary search pinpoints the target in a few comparisons and lands in microseconds — a gap of several thousand times in magnitude. That is the dividend "sorted" pays out — provided you genuinely keep it sorted.

### The Real Trap: Running Binary Search on an Unsorted Range

For the binary family, "already sorted" is a **hard precondition**, not a "nicer if sorted, passable if not". The standard spells it out as preconditions; violating them is **undefined behavior** — the compiler will not stop you, and the results are entirely untrustworthy. Let's run it on a deliberately shuffled sequence and let the trap show itself:

```cpp
// Standard: C++20
#include <algorithm>
#include <iostream>
#include <vector>

int main()
{
    // an unsorted sequence that makes binary_search miss
    std::vector<int> u{10, 1, 30, 2, 20, 3};   // contains 2, but unordered
    std::cout << "实际含 2?        " << (std::find(u.begin(), u.end(), 2) != u.end()) << '\n';
    std::cout << "binary_search(2):" << std::binary_search(u.begin(), u.end(), 2) << '\n';
    return 0;
}
```

```text
实际含 2?        1
binary_search(2):0
```

`2` is plainly in the range (`find` got it), yet `binary_search` returns `0` — because the algorithm assumes order and goes looking in the direction of "2 should be in the front half"; not finding it there, it concludes it doesn't exist. That is not a bug; we simply failed its precondition. So before reaching for the binary family, confirm the range is truly sorted; if unsure, honestly use `find` — O(n) is slower, but at least it won't lie to you.

::: warning Binary search presupposes "sorted", and consistent comparison semantics
Two preconditions people keep overlooking: first, the range must already be sorted; second, the comparator used for sorting and the one used for searching must agree semantically (sort in descending order and `binary_search`, which hunts in ascending order by default, is wrong all the same). `binary_search` / `lower_bound` / `upper_bound` / `equal_range` all accept an extra comparator parameter; when your sorting comparator doesn't match it, be sure to pass that parameter. Sort first, search second, keep the comparator consistent — with those three in place, the binary family is trustworthy.
:::

## Picking an Algorithm by Need: A Decision Table

After all that, combat boils down to one question — "for this requirement of mine, which one do I use". We've condensed the scenarios this article covered into a decision table; just find your row:

| What I want to do | Range state | Who to pick | Complexity |
|---|---|---|---|
| Tell "is there an element satisfying a condition" | any | `any_of` / `all_of` / `none_of` | O(n), short-circuit |
| Count "how many satisfy a condition" | any | `count_if` | O(n) |
| Find the first element satisfying a condition | any | `find_if` | O(n) |
| Find a contiguous subsequence | any | `search` | O(n·m) |
| Transform every element and place it in a new range | any | `transform` | O(n) |
| In-place overwrite of elements satisfying a condition | any | `replace_if` | O(n) |
| Delete every element equal to a value (sequence containers) | any | `std::erase(c, value)` | O(n) |
| Delete every element satisfying a condition (any container) | any | `std::erase_if(c, pred)` | O(n) |
| Delete every element equal to a value (pre-C++20) | any | `erase(remove(...), end())` idiom | O(n) |
| Drop adjacent duplicates | more effective if sorted first | `unique` + `erase` | O(n) |
| Tell "is a value present" | **sorted** | `binary_search` | O(log n) |
| Find the first position "not less than / greater than" a value | **sorted** | `lower_bound` / `upper_bound` | O(log n) |
| Find the complete extent of a value | **sorted** | `equal_range` | O(log n) |
| Insert a new element in order | **sorted** | `lower_bound` to find the point + `insert` | O(log n) + O(n) |

This table is where the article lands. Remember one master principle — **O(n) is the default gear for searching, modifying, and deleting; only what can be kept sorted earns the O(log n) binary dividend**.

## Summary

- `<algorithm>` splits by what it does to a range into four groups: non-modifying (read-only), modifying (change in place or write to a destination), the erase-remove idiom (deleting elements), and sorted search (the binary family).
- `any_of` / `all_of` / `none_of` short-circuit; `search` hunts subsequences, not single elements.
- `remove` / `unique` **only shift, never shrink**; they return a new logical end, and shrinking always falls to the container's `erase` — the most classic STL trap there is.
- C++20's `std::erase` / `erase_if` free functions make deleting elements a one-liner; sequence containers get both, associative containers only `erase_if`.
- The binary family (`binary_search` / `lower_bound` / `upper_bound` / `equal_range`) presses search down to O(log n), provided the **range is sorted** and comparator semantics agree; binary search on an unsorted range is undefined behavior and will hand you wrong answers.

In the next article we continue with the second half of the algorithms story — sorting (`sort` / `stable_sort` / `partial_sort`), partitioning (`partition`), merging (`merge`), and more O(log n) tricks available under the "sorted range" premise.

## References

- [cppreference: Algorithms library](https://en.cppreference.com/w/cpp/algorithm) — a tour of the whole `<algorithm>` collection, grouped by non-modifying / modifying / partitioning / sorting / binary search and more
- [cppreference: std::remove](https://en.cppreference.com/w/cpp/algorithm/remove) — the "shifts but never shrinks" mechanics of remove inside the erase-remove idiom
- [cppreference: std::erase, std::erase_if (C++20)](https://en.cppreference.com/w/cpp/container/erase) — the unified erasure free functions, their per-container specializations and scopes
- [cppreference: std::lower_bound](https://en.cppreference.com/w/cpp/algorithm/lower_bound) — the semantics ("first not less than") and complexity of the binary family
- [cppreference: std::binary_search](https://en.cppreference.com/w/cpp/algorithm/binary_search) — binary search preconditions (sorted) and the undefined-behavior notes
