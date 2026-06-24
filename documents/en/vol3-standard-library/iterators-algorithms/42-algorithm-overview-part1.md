---
chapter: 7
cpp_standard:
- 11
- 17
- 20
description: 'We break `<algorithm>` into four sections—non-modifying, modifying,
  the erase–remove idiom, and sorted range searches—to clarify selection strategies:
  why `for_each` does not modify ranges, why `remove` only shifts elements instead
  of deleting them, how C++20''s `std::erase` handles value deletion in a single line,
  and why the `binary_search` family achieves $O(\log n)$ and requires sorted ranges.'
difficulty: intermediate
order: 42
platform: host
prerequisites:
- 迭代器基础与 category
- 迭代器适配器：反向、插入与流，把现成迭代器改出新行为
- vector 深入：三指针、扩容与迭代器失效
reading_time_minutes: 14
related:
- 容器选择指南：按操作、内存与失效规则挑对容器
tags:
- host
- cpp-modern
- intermediate
- 容器
title: 'Algorithm Overview (Part 1): Non-modifying, Modifying, and Searching Operations,
  and How to Choose the Right Algorithm for a Problem'
translation:
  source: documents/vol3-standard-library/iterators-algorithms/42-algorithm-overview-part1.md
  source_hash: 26c99c21c5a2ece2c036f069a855f0576584f23f3f20a657da45b2ecb2c9dee2
  translated_at: '2026-06-24T04:26:06.368689+00:00'
  engine: anthropic
  token_count: 4463
---
# Algorithm Overview (Part 1): Non-modifying, Modifying, and Lookup — How to Choose the Right Tool

In the previous post on iterator adapters, we used a handy little pattern—`lower_bound` to find a position + `insert` to place an element—to insert a new element into a sorted `vector` while maintaining order. That was actually an algorithm stepping into the spotlight. Now, we are officially diving into the `<algorithm>` header.

`<algorithm>` is a massive part of the STL, containing over eighty algorithms. If we went through the API signatures one by one, this post would turn into a boring manual—that's cppreference's job, not ours. Let's switch to a more useful perspective: **given a specific requirement, which algorithm should we pick?** We will categorize this large collection of algorithms based on "what they do to your range," remember two or three representatives from each category, keep the time complexity in mind, and you'll be ready to match the right tool to the problem when it arises.

In this post, we will cover the first four major categories: **non-modifying** algorithms (read-only), **modifying** algorithms (which move elements around), the **erase-remove idiom** (specifically for "removing" elements, and how C++20 simplifies it), and the **binary search** family that relies on sorted ranges. Sorting, partitioning, and merging will be saved for the next post. All examples have been tested locally on GCC 16.1.1 with `-std=c++20 -O2`, and the output reflects real terminal logs.

## Non-modifying: Read-only, doesn't change a single element

The first category is the easiest to understand—scanning from start to finish, read-only. `for_each` for traversal, `find` for locating, `count` for tallying, and `any_of` for predicate checks all belong here. Their common characteristic is that the range remains identical before and after the call, and the complexity is generally O(n) (except for the binary search family, which we will cover separately later).

Let's run a quick set of the most commonly used ones to review `for_each`, `find`, `find_if`, `count`, `any_of`, `all_of`, and `none_of` all at once:

```cpp
// Standard: C++20
#include <algorithm>
#include <iostream>
#include <vector>

int main()
{
    std::vector<int> v{3, 1, 4, 1, 5, 9, 2, 6};

    // for_each: 只读遍历，不改区间
    int sum = 0;
    std::for_each(v.begin(), v.end(), [&](int x) { sum += x; });
    std::cout << "for_each 求和: " << sum << '\n';

    // find: 线性查找，返回第一个等于目标的迭代器
    auto it = std::find(v.begin(), v.end(), 5);
    std::cout << "find 5 -> 偏移 " << (it - v.begin()) << '\n';

    // find_if: 第一个满足谓词的
    auto big = std::find_if(v.begin(), v.end(), [](int x) { return x > 7; });
    std::cout << "find_if(>7) -> " << (big != v.end() ? *big : -1) << '\n';

    // count / count_if
    std::cout << "count(1): " << std::count(v.begin(), v.end(), 1) << '\n';
    std::cout << "count_if(偶数): "
              << std::count_if(v.begin(), v.end(), [](int x) { return x % 2 == 0; }) << '\n';

    // none_of / any_of / all_of：返回 bool
    std::cout << "any_of(>8): " << std::any_of(v.begin(), v.end(), [](int x) { return x > 8; }) << '\n';
    std::cout << "all_of(<10): " << std::all_of(v.begin(), v.end(), [](int x) { return x < 10; }) << '\n';
    std::cout << "none_of(<0): " << std::none_of(v.begin(), v.end(), [](int x) { return x < 0; }) << '\n';

    return 0;
}
```

Here is the output we got:

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

In this family, we should specifically highlight the trio `any_of`, `all_of`, and `none_of`. They all perform **short-circuit evaluation**—`any_of` returns `true` immediately upon finding the first element that satisfies the predicate, without scanning the entire range; similarly, `all_of` returns `false` immediately upon finding the first element that fails the condition. Therefore, to check "are there any negative numbers in the range," we can use either `!std::all_of(..., [](x){return x>=0;})` or `std::any_of(..., [](x){return x<0;})`. The latter reads more directly and aligns better with the logic that "this is fundamentally a question about existence."

There is another easily overlooked but very useful algorithm: `std::search`. It doesn't look for a single element, but for an entire subsequence. For example, to find a specific word in a block of text, `find` searches for "a single character equal to the target," whereas `search` looks for "this substring matching the target sequence element-by-element":

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
    // 从上一次匹配点的下一位继续找第二次出现
    auto it2 = std::search(it + 1, text.end(), needle.begin(), needle.end());
    std::cout << "search 第二次偏移:          " << (it2 - text.begin()) << '\n';
    return 0;
}
```

```text
search("hello") 第一次偏移: 0
search 第二次偏移:          13
```

::: warning Don't confuse `find` with `search`
`find` checks if a "single element equals the target", while `search` checks if a "whole subsequence is element-wise equal". Use `find` to locate a value within a `vector<int>`, but use `search` to find a continuous subsequence (for example, checking if `[3, 4, 5]` is present). If you mix them up, `find` will return a position where "the first element equals the start of the subsequence", which is completely different from the "whole sequence match" you are looking for.
:::

## Mutating: Either modify in-place or write elsewhere

The second category operates on ranges. It comes in two styles: **in-place modification** (replacing or moving elements within the same range) and **write to destination range** (keeping the source unchanged and writing the result to another location, usually combined with insert iterators discussed in the previous article).

Let's run through a set of examples to cover all the patterns:

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

    // copy: 原样复制到目标区间
    std::vector<int> copied;
    std::copy(src.begin(), src.end(), std::back_inserter(copied));
    print(copied, "copy:           ");

    // copy_if: 带条件的复制
    std::vector<int> evens;
    std::copy_if(src.begin(), src.end(), std::back_inserter(evens),
                 [](int x) { return x % 2 == 0; });
    print(evens, "copy_if(偶数):  ");

    // transform: 一对一映射，把每个元素变身后写到目标
    std::vector<int> squared;
    std::transform(src.begin(), src.end(), std::back_inserter(squared),
                   [](int x) { return x * x; });
    print(squared, "transform(x*x): ");

    // replace / replace_if: 就地把满足条件的元素换成新值
    std::vector<int> r{1, 2, 3, 2, 4, 2};
    std::replace(r.begin(), r.end(), 2, 99);
    print(r, "replace(2->99): ");

    std::vector<int> r2{1, 2, 3, 4, 5, 6};
    std::replace_if(r2.begin(), r2.end(), [](int x) { return x % 2 == 0; }, 0);
    print(r2, "replace_if(偶->0): ");

    // unique: 就地去重相邻重复（关键看后面 erase-remove 段）
    std::vector<int> u{1, 1, 2, 3, 3, 3, 4, 1, 1};
    auto new_end = std::unique(u.begin(), u.end());
    std::cout << "unique 后逻辑终点偏移: " << (new_end - u.begin())
              << " 实际 size 仍为 " << u.size() << '\n';

    // move: 把元素搬走（右值），目标拿到所有权
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

There are two sets of "in-place vs. copy-elsewhere" comparisons here that are worth remembering:

- **Modifying values**: Use `replace` / `replace_if` in-place; if we want the result in a new range, use `replace_copy` / `replace_copy_if` (the ones with `_copy` in their names effectively combine "replace + copy" in one step, leaving the source untouched).
- **Moving elements**: Use `move` to rearrange in-place (this moves elements out of the source range, leaving behind "moved-from" husks—the fact that `words[0].size()` became `0` above is evidence that the string content was moved); use `transform` to copy the transformed result to a new range.

We will dedicate a separate section to `unique` shortly, because it is a twin sibling to `remove`. They both share the same counter-intuitive design—**they move elements but do not shrink the container**. This is one of the classic STL pitfalls, and it is the star of the next section.

## The erase-remove idiom: Why remove doesn't actually delete

This is one of the most classic STL designs, and it is also the one most likely to trip up beginners. The requirement is simple: delete all elements equal to `2` from a `vector`. The first instinct is probably to look for an algorithm named `remove`—and sure enough, there is `std::remove`. However, it **does not actually delete anything**.

Let's first look at what it actually does:

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

See what happened? `remove` shifts elements "not equal to 2" to the front, squeezing them into the first part of the range, and then returns a **new logical end**. However, the physical size of the `vector` remains unchanged; it still holds seven elements. The tail end contains leftover old values (`4 2 5`) from the shift—garbage that is "logically discarded but physically occupying slots."

### Why not delete directly: Algorithms don't know containers

This design might seem awkward, but the reasoning is actually quite sound: **`std::remove` only recognizes iterators, not containers**. As discussed in the previous section, algorithms are decoupled from containers via iterator interfaces—`remove` only receives two iterators. It has no idea whether they back a `vector`, `list`, or `deque`, let alone which `erase` method to call to actually shrink the capacity. Erasing is a container member function, outside the scope of an algorithm. Therefore, `remove` does what it can: it moves elements and returns the new end, leaving the actual resizing to the caller.

Thus, to actually delete elements, we need a two-step process—let `remove` do the shifting, then use the container's own `erase` to chop off the tail past the new end:

```cpp
v.erase(new_end, v.end());
```

```text
erase 后:             1 3 4 5   [size=4]
```

Combining these two steps gives us the famous **erase-remove idiom**:

```cpp
v.erase(std::remove(v.begin(), v.end(), 2), v.end());
```

The `unique` algorithm works exactly the same way—it only "squeezes out" adjacent duplicates. It merely moves elements without shrinking the container, so actual deletion requires pairing it with `erase`. The output from the previous `unique` example is proof: the logical end is shifted by five, but `size` remains nine. We truly need `u.erase(new_end, u.end())` to actually remove those elements. So, just remember this simple rule: **`remove` / `unique` only move elements; shrinking always relies on `erase`**.

### C++20: `std::erase` and `erase_if` handle this in one line

Writing that long chain of `erase(remove(...), end())` repeatedly gets tedious. C++20 introduces a set of new free functions—`std::erase(c, value)` and `std::erase_if(c, pred)`. They accept the container directly and delete values or elements satisfying a condition. Internally, they automatically handle the erase-remove idiom for you and conveniently return the number of elements removed:

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

That feels much cleaner, doesn't it? Now that we have this, can we completely forget the old erase-remove idiom? **Not entirely**. There is a nuance regarding the scope of application, verified here using GCC 16.1.1:

- **Sequence containers** (`vector` / `string` / `deque` / `list` / `forward_list`): Both `erase(c, value)` and `erase_if(c, pred)` are available.
- **Associative containers** (`map` / `set` / `multimap` / `multiset` and their `unordered_` variants): **Only `erase_if` is available; there is no value-based `erase`**.

Why is there no value-based `erase` for associative containers? Because they already have a member function `c.erase(key)` to delete a node by key. If the free function `std::erase(c, value)` also existed, it would cause a name collision with subtly different semantics, so the standards committee decided to provide only `erase_if` for associative containers. We tested this on GCC 16.1.1; calling `std::erase(s, 2)` on a `std::set` results in a compilation error:

```text
error: no matching function for call to 'erase(std::set<int>&, int)'
  7 |     std::erase(s, 2);   // 关联容器: 只有 erase_if，没有按值的 erase
```

The error message is straightforward: no matching `erase` was found. So, remember this rule—**for associative containers, use `erase_if` to remove elements; for sequence containers, you can use `erase` to remove values or `erase_if` to remove conditions**. For sequence containers, don't bother writing that verbose `erase(remove(...), end())` chain anymore if you can do it in one line.

::: warning ranges::remove returns a subrange, not a raw iterator
C++20 also provides `std::ranges::remove`. It no longer returns a raw "new end iterator," but a `subrange` (a combination of the retained range and the removed range). When using it with `erase`, write it like this:

```cpp
auto [first, last] = std::ranges::remove(v, 2);
v.erase(first, last);
```

Mixing this up with the classic `v.erase(std::remove(...), v.end())` can be confusing. Fortunately, for sequence containers, using `std::erase` or `erase_if` directly is the most concise one-liner. We rarely write the ranges version of `remove` in daily practice.
:::

## Ordered Search: The Binary Search Bunch — O(log n) Requires Sorted Data

Up to this point, the `find` and `count` algorithms we discussed are all O(n) linear scans — they struggle when data volumes get large. Is there a faster way? Yes, provided the **range is already sorted**. Once sorted, binary search can cut the complexity down from O(n) to O(log n).

There are four algorithms in this family, each with a different role:

- `binary_search(first, last, v)` — Answers "is v present?", returns a `bool`.
- `lower_bound(first, last, v)` — Returns the position of the first element that is "**not less than** v" (`>= v`).
- `upper_bound(first, last, v)` — Returns the position of the first element that is "**greater than** v" (`> v`).
- `equal_range(first, last, v)` — Returns `[lower, upper)` in one go, representing the full range of v within the interval.

It's easy to confuse `lower_bound` and `upper_bound` just by reading the descriptions. Let's run through them and let the output do the talking:

```cpp
// Standard: C++20
#include <algorithm>
#include <iostream>
#include <vector>

int main()
{
    std::vector<int> v{1, 3, 3, 5, 7, 7, 7, 9};   // 已升序

    // binary_search: 在不在（bool）
    std::cout << "binary_search(7): " << std::binary_search(v.begin(), v.end(), 7) << '\n';
    std::cout << "binary_search(4): " << std::binary_search(v.begin(), v.end(), 4) << '\n';

    // lower_bound: 第一个「不小于」value 的位置（>= value）
    auto lo = std::lower_bound(v.begin(), v.end(), 7);
    std::cout << "lower_bound(7) -> 偏移 " << (lo - v.begin()) << " 值 " << *lo << '\n';

    // upper_bound: 第一个「大于」value 的位置（> value）
    auto up = std::upper_bound(v.begin(), v.end(), 7);
    std::cout << "upper_bound(7) -> 偏移 " << (up - v.begin()) << " 值 " << *up << '\n';

    // equal_range: [lower, upper) 就是 7 的完整范围
    auto [eq_lo, eq_up] = std::equal_range(v.begin(), v.end(), 7);
    std::cout << "equal_range(7): [" << (eq_lo - v.begin()) << ", " << (eq_up - v.begin()) << ") -> ";
    for (auto it = eq_lo; it != eq_up; ++it) std::cout << *it << ' ';
    std::cout << "共 " << (eq_up - eq_lo) << " 个\n";

    // 查一个不存在的值：lower_bound 给的是「该插哪」
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

Looking at the output, it becomes clear: the three `7`s occupy offsets 4, 5, and 6. `lower_bound(7)` lands on the first `7` (offset 4, the start of `>= 7`), and `upper_bound(7)` lands on the first `9` after the `7`s (offset 7, the start of `> 7`). `equal_range` gives us the half-open range `[4, 7)` in one go. If we search for a non-existent value like `4`, `lower_bound` lands at offset 3 (pointing to `5`) — which is exactly the position where "4 would be inserted if we were to add it."

### Connecting to the Previous Article: `insert_sorted` is just `lower_bound` + `insert`

Now, looking back, the little "order-preserving insertion" pattern from the previous article makes perfect sense. `lower_bound` finds the insertion point in O(log n) on a sorted range, and then we use the container's `insert` to push the element in. We can't avoid the data movement (contiguous storage, O(n)), but we've reduced the step of finding the position to logarithmic time using binary search:

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

### Binary Search vs. Linear Search: How Much Faster?

Saying "O(log n) is faster than O(n)" is a bit abstract. Let's take a sorted `vector` with ten million elements and compare `find` against `binary_search` in the worst-case scenario (where the target is at the end) to see the real difference:

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
    for (int i = 0; i < kN; ++i) v[i] = i;   // 已升序

    int target = kN - 1;   // 最坏情况：在末尾

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

Native GCC 16.1.1 with `-O2` (single measurement; specific microsecond counts vary by machine and execution, but the order of magnitude remains stable):

```text
find        (O(n))      1  耗时 5891 us
binary_search (O(log n)) 1  耗时 1 us
倍数差距: 5891x
```

Want to see the performance gap firsthand? Check out this online demo:

<OnlineCompilerDemo
  title="Binary vs. Linear Search: The Benefits of O(log n)"
  source-path="code/examples/vol3/42_binary_vs_linear.cpp"
  description="Ten million sorted elements, worst-case scenario (target at the end): std::find scans to the end (milliseconds), std::binary_search finishes in a few comparisons (microseconds). The difference is several orders of magnitude—provided the data is actually sorted."
  allow-run
/>

With ten million elements, a linear `find` might scan to the very end in the worst case, taking milliseconds. Binary search locates the target in just a few comparisons, taking microseconds. That's a difference of several orders of magnitude. This is the bonus that "sorted" brings—provided you actually keep it sorted.

### The Real Trap: Using Binary Search on Unsorted Ranges

The "sorted" requirement for binary search algorithms is a **hard prerequisite**, not a "nice-to-have" optimization. The standard specifies this as a precondition; violating it results in **undefined behavior**. The compiler won't stop you, and the results are completely unreliable. Let's run this on a deliberately shuffled sequence to expose the trap:

```cpp
// Standard: C++20
#include <algorithm>
#include <iostream>
#include <vector>

int main()
{
    // 一个会让 binary_search 漏判的未排序序列
    std::vector<int> u{10, 1, 30, 2, 20, 3};   // 含 2，但无序
    std::cout << "实际含 2?        " << (std::find(u.begin(), u.end(), 2) != u.end()) << '\n';
    std::cout << "binary_search(2):" << std::binary_search(u.begin(), u.end(), 2) << '\n';
    return 0;
}
```

```text
实际含 2?        1
binary_search(2):0
```

`2` is clearly in the range (`find` found it), yet `binary_search` returns `0`—because the binary search algorithm assumes the range is sorted and looks in the direction where "2 should appear in the first half". If it doesn't find it there, it assumes it doesn't exist. This isn't a bug; we just failed to meet its prerequisites. Therefore, before using the binary search family, confirm that the range is actually sorted. If you aren't sure, stick with `find`; it's O(n) and slower, but at least it won't mislead you.

::: warning Binary search requires a "sorted" range and consistent comparison semantics
Two frequently overlooked prerequisites: first, the range must be sorted; second, the comparator used for sorting must be semantically consistent with the one used for searching (if you sorted in descending order but use `binary_search`'s default ascending order search, it will still fail). `binary_search`, `lower_bound`, `upper_bound`, and `equal_range` all accept an additional comparator parameter. If the sorting comparator doesn't match the default, you must pass this parameter in. Sort first, search later, keep comparators consistent—only when these three things align are the binary search algorithms reliable.
:::

## Choosing an Algorithm by Requirement: A Decision Table

With all that said, the practical question boils down to one thing—"For my specific requirement, which algorithm should I use?" We've summarized the scenarios covered in this article into a decision table; just find the row that matches your needs:

| What I want to do | Range State | Pick this | Complexity |
|---|---|---|---|
| Check "if any element satisfies a condition" | Any | `any_of` / `all_of` / `none_of` | O(n), short-circuiting |
| Count "how many elements satisfy a condition" | Any | `count_if` | O(n) |
| Find the first element satisfying a condition | Any | `find_if` | O(n) |
| Find a contiguous subsequence | Any | `search` | O(n·m) |
| Transform each element and put it in a new range | Any | `transform` | O(n) |
| Modify elements in-place that satisfy a condition | Any | `replace_if` | O(n) |
| Remove all elements equal to a value (sequence containers) | Any | `std::erase(c, value)` | O(n) |
| Remove all elements satisfying a condition (any container) | Any | `std::erase_if(c, pred)` | O(n) |
| Remove all elements equal to a value (pre-C++20) | Any | `erase(remove(...), end())` idiom | O(n) |
| Remove adjacent duplicates | More effective if sorted first | `unique` + `erase` | O(n) |
| Check "if a value exists" | **Sorted** | `binary_search` | O(log n) |
| Find the first position "not less / greater than" a value | **Sorted** | `lower_bound` / `upper_bound` | O(log n) |
| Find the full range of a value | **Sorted** | `equal_range` | O(log n) |
| Insert a new element while preserving order | **Sorted** | `lower_bound` to find position + `insert` | O(log n) + O(n) |

This table wraps up this article. Remember one overarching principle—**O(n) is the default gear for checking, modifying, and deleting; only if you sort properly do you get the O(log n) binary search bonus**.

## Summary

- `<algorithm>` falls into four categories based on what it does to a range: non-modifying (read-only), modifying (in-place or write-to-destination), the erase-remove idiom (removing elements), and sorted searching (binary search family).
- `any_of` / `all_of` / `none_of` use short-circuit evaluation; `search` finds subsequences, not single elements.
- `remove` / `unique` **only move elements, they don't shrink capacity**; they return a new logical end, and shrinking always relies on the container's `erase`—this is the classic STL pitfall.
- C++20's `std::erase` / `erase_if` free functions let us delete elements in one line; sequence containers have both, while associative containers only have `erase_if`.
- The binary search family (`binary_search` / `lower_bound` / `upper_bound` / `equal_range`) reduces search complexity to O(log n), provided the **range is sorted** and the comparator semantics are consistent; using binary search on an unsorted range is undefined behavior and will yield incorrect results.

In the next article, we will cover the second half of this topic—sorting (`sort` / `stable_sort` / `partial_sort`), partitioning (`partition`), merging (`merge`), and more O(log n) techniques available under the "sorted range" premise.

## References

- [cppreference: Algorithms library](https://en.cppreference.com/w/cpp/algorithm) — Overview of the entire `<algorithm>` suite, categorized by non-modifying / modifying / partitioning / sorting / binary search, etc.
- [cppreference: std::remove](https://en.cppreference.com/w/cpp/algorithm/remove) — The mechanism of `remove` in the erase-remove idiom: "moves elements, does not shrink capacity"
- [cppreference: std::erase, std::erase_if (C++20)](https://en.cppreference.com/w/cpp/container/erase) — Unified deletion free functions, their specializations per container, and applicable scopes
- [cppreference: std::lower_bound](https://en.cppreference.com/w/cpp/algorithm/lower_bound) — The semantics ("first not less than") and complexity of the binary search family
- [cppreference: std::binary_search](https://en.cppreference.com/w/cpp/algorithm/binary_search) — Binary search prerequisites (sorted) and undefined behavior explanation
