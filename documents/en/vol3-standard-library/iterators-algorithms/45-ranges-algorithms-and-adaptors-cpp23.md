---
chapter: 7
cpp_standard:
- 20
- 23
description: We dive deep into the three-step evolution of algorithms into Ranges
  (Range parameters, Concept constraints, and Niebloids blocking ADL). We also cover
  how the C++23 `fold` family fixes the return type pitfalls of `accumulate`, and
  how `contains` and `find_last` eliminate the `find != end()` anti-pattern. Finally,
  we test the current state of GCC 16.1.1's support for new adapters like `zip`, `chunk`,
  `slide`, `stride`, and `repeat`.
difficulty: advanced
order: 45
platform: host
prerequisites:
- 迭代器基础与 category
- 迭代器适配器：反向、插入与流
related:
- 新标准容器：flat_map、inplace_vector 与 mdspan
reading_time_minutes: 22
tags:
- host
- cpp-modern
- advanced
- Ranges
title: 'Ranges Algorithms and C++23 Additions: fold, contains, and New Adapters'
translation:
  source: documents/vol3-standard-library/iterators-algorithms/45-ranges-algorithms-and-adaptors-cpp23.md
  source_hash: 4329b9fe6e95dfddc52c097dd0c5ba66a004182d4b981273784ebe948215fdab
  translated_at: '2026-06-24T00:47:12.131906+00:00'
  engine: anthropic
  token_count: 5156
---
# Ranges Algorithms and C++23 Additions: Fold, Contains, and New Adapters

In previous articles, we covered iterators and iterator adapters, but we left the algorithms side stuck on the "old `<algorithm>`" style. This article will thoroughly explain the modern evolution of algorithms: how C++20 "Range-ified" the entire `<algorithm>` library (via parameters, concepts, and Niebloids), and the key additions in C++23—how the `fold` family fixes the old pitfalls of `accumulate`, how `contains`/`find_last` eliminate the "`find() != end()`" anti-pattern, and a batch of new ranges adapters (`zip`, `chunk`, `slide`, `stride`, `repeat`).

Let's set a boundary first to avoid overlap with other volumes: general concepts like ranges views, the pipe operator `|`, and lazy evaluation belong to Volume 4 (which will focus on ranges view pipelines). This article will not expand on general mechanisms but will focus on two specific topics: "Range-ification of algorithms" and "C++23 new algorithms/adapters." Materializing views into containers via `ranges::to` belongs to the container lineage and is covered in Volume 3's [New Standard Containers](../containers/10-new-containers-cpp23-26.md) and cppreference; we will only mention it in passing here when used.

## Range-ification of Algorithms: What Changed in Three Steps

C++20 didn't just slap a `ranges::` prefix on the old algorithms. It changed three things at once, each corresponding to a practical difference you will encounter.

### Step 1: Parameters Changed from "Iterator Pair" to "Range"

The old style required manually providing two iterators: `std::sort(v.begin(), v.end())`. The ranges version accepts a Range directly: `std::ranges::sort(v)`. Typing half as much is the least benefit; the real advantage lies in the **sentinel**.

The old STL required the head and tail iterators to be of the **same type**—`begin()` and `end()` had to return the same kind of iterator. This seemed natural but actually blocked a very natural class of sequences: **null-terminated C strings**. Their "end" isn't a pointer position of the same type as the head iterator, but rather a condition—"stop when `\0` is hit." Before ranges, you either had to calculate `strlen` manually or wrap it in `std::string_view` first.

Ranges abstracted the "end" as a **sentinel**: a sentinel can be a different type from the iterator, as long as it can be compared for equality with the iterator. This allows sequences where "the length is unknown beforehand and reading stops at a certain condition" to be fed directly into algorithms. `string_view` is the prime example; its `end()` returns a sentinel, and `ranges::count` can consume it directly:

```cpp
// Standard: C++20
#include <algorithm>
#include <iostream>
#include <string_view>
#include <vector>

int main() {
    // Range 参数:一个参数搞定整个容器
    std::vector<int> v{3, 1, 4, 1, 5, 9, 2, 6};
    std::ranges::sort(v);
    std::cout << "ranges::sort 后: ";
    for (auto x : v) std::cout << x << ' ';
    std::cout << '\n';

    // string_view 的 end() 是哨兵,天然适配「读到 \0 停」
    std::string_view sv = "hello";
    std::cout << "ranges::count(\"hello\", 'l') = "
              << std::ranges::count(sv, 'l') << '\n';
}
```

`g++ -std=c++23 -O2` (native GCC 16.1.1) output:

```text
ranges::sort 后: 1 1 2 3 4 5 6 9
ranges::count("hello", 'l') = 2
```

### Step 2: Concepts reject incorrect types at the call site

In the previous discussion on iterator categories, we mentioned this specific scenario: `std::sort` requires random-access iterators, but `std::list` iterators only go up to bidirectional. Consequently, `std::sort` cannot be used with a `list`. Prior to C++20, this requirement was only documented—passing the wrong type wouldn't prompt the compiler to say "wrong type" at the call site. Instead, the compiler would silently instantiate the template, eventually spewing out a long trail of errors like "operator- not found," leaving the reader to trace back and deduce where things went wrong.

Ranges algorithms bring these requirements into the type system using concepts. When calling `ranges::sort(l)` on a `list`, the concept checks the constraints **at the call site** and immediately rejects them, resulting in an error message that gets straight to the point. Comparing the two approaches side-by-side makes the difference starkly clear:

```cpp
// Standard: C++20
#include <algorithm>
#include <list>

int main() {
    std::list<int> l{3, 1, 4, 1, 5};
    std::ranges::sort(l);   // ranges 版:Concept 层直接拒绝
    std::sort(l.begin(), l.end());   // 老版:模板深处的 operator- 报错
}
```

Error reported by `ranges::sort(l)` on GCC 16.1.1 (showing the first few lines):

```text
concept_reject.cpp:7:22: error: no match for call to
    '(const std::ranges::__sort_fn) (std::__cxx11::list<int>&)'
    7 |     std::ranges::sort(l);   // Concept 层直接拒绝
      |     ~~~~~~~~~~~~~~~~~^~~
  • candidate 1: ... requires (random_access_iterator<_Iter>) ...
                            ^^^^^^^^^^^^^^^^^^^^^^^^^^^
```

The error message for the traditional `std::sort(l.begin(), l.end())` syntax is buried deep within the templates:

```text
/usr/include/c++/16.1.1/bits/stl_algo.h: In instantiation of
'constexpr void std::__sort(_RandomAccessIterator, _RandomAccessIterator, _Compare)
   [with _RandomAccessIterator = _List_iterator<int>; ...]':
  required from here
stl_algo.h:1914:50: error: no match for 'operator-'
 (operand types are 'std::_List_iterator<int>' and 'std::_List_iterator<int>')
 1914 |                                 std::__lg(__last - __first) * 2,
```

One explicitly states at the call site, "I require random access, and you didn't give it to me." The other dives deep into `__sort` internals and complains, "`__last - __first` cannot be calculated." The former allows you to pinpoint the issue immediately, while the latter requires you to deduce that "list iterators cannot be subtracted" to realize what went wrong. This demonstrates the practical value of Concepts: they transform "requirements in documentation" into "compile-time checkable facts." If a `list` needs sorting, it should use its own member function `list::sort()` (as discussed in the previous article, it uses a merge sort implementation with O(n log n) complexity).

### Step 3: Niebloids—Algorithms Opt-Out of ADL

This step is more subtle, but it can be quite baffling when encountered. Legacy STL algorithms are **ordinary functions** in a namespace; ranges algorithms are not functions, but function objects called **Niebloids** (customization point objects, or CPOs)—they look like functions when called, but have two key differences.

The most impactful practical difference: **Niebloids do not participate in ADL (Argument-Dependent Lookup)**. This means that if you write a `sort(x)` in a custom namespace, the compiler will never "conveniently" pull `std::ranges::sort` into the overload set just because of an argument's type. In legacy STL, this was a real risk of hijacking (if a type's associated namespace happened to have a `sort`, it could hide `std::sort`). We can verify this behavior:

```cpp
// Standard: C++20
#include <algorithm>
#include <vector>

namespace user {
    struct Tag {};
    void sort(Tag) {}   // 自定义命名空间里有个同名 sort

    void demo() {
        std::vector<int> v{3, 1, 2};
        sort(v);   // 既没 using std::ranges,也没用 ADL 把它拉进来
    }
}
```

The error indicates that `ranges::sort` was not found by ADL (it was not among the candidates found at all):

```text
adl.cpp:14:13: error: no matching function for call to 'sort(std::vector<int>&)'
   14 |         sort(v);
      |     ~~~~^~~
```

The motivation behind Niebloids is to plug this hole: algorithms cannot be hijacked by functions with the same name in the user's namespace, ensuring predictable behavior. Incidentally, because a Niebloid is an object, not a function, don't expect to pass its address around like a callback the way you did with the old `std::sort`—it is a closure object with an overloaded `operator()`, which is semantically different from a plain function pointer. Wrapping it in a lambda is safer if you need to pass it somewhere.

::: warning "Ranges Algorithms" are not "Old Algorithms with a `ranges::` Prefix"
These three changes are interconnected: Range + Sentinel parameters, Concept constraints, and Niebloids. This means ranges algorithms are not syntactic sugar for old algorithms, but a redesigned interface. The old `std::sort` is not deprecated (your existing code still runs), but in new code, use the ranges version whenever possible—less typing, clearer error messages, and immunity to ADL hijacking. It's a triple win.
:::

## The `fold` Family (C++23): Fixing the Return Type Trap of `accumulate`

Having covered the ranges transformation of algorithms, let's move on to new features in C++23. The first one we need to cover thoroughly is `fold`—it didn't appear out of thin air; it was created to address a real flaw in the old `std::accumulate`.

### The Return Type Trap of `accumulate`

`std::accumulate` lives in `<numeric>` and performs a "left fold": given an initial value and a binary operation, it folds the entire sequence from left to right into a single value. It has a rather subtle pitfall—**the return type is determined by the initial value, not by the element type**. If you write the initial value as `1` (an `int`), even if the sequence is full of `double`s, the entire calculation proceeds as `int`, silently truncating the fractional parts:

```cpp
// Standard: C++23
#include <algorithm>
#include <iostream>
#include <numeric>
#include <vector>

int main() {
    std::vector<double> vec{1.5, 2.5, 3.5, 4.5};   // 真实和 = 12.0

    // 初始值写成 1(int),返回类型被定死成 int,1.5/2.5... 全被截断
    double acc = std::accumulate(vec.begin(), vec.end(), 1);
    std::cout << "accumulate(vec, 1)      = " << acc << '\n';

    // fold_left 的返回类型由 f(init, *first) 决定,这里推回 double,不截断
    double fl = std::ranges::fold_left(vec, 1, std::plus{});
    std::cout << "fold_left(vec, 1, +)    = " << fl << '\n';
}
```

The resulting comparison is very stark:

```text
accumulate(vec, 1)      = 11
fold_left(vec, 1, +)    = 13
```

Regarding `accumulate`: the initial value `1` is an `int`, so `1 + 1.5` → `2` (truncation), `2 + 2.5` → `4`, `4 + 3.5` → `7`, and `7 + 4.5` → `11`. The entire calculation stays in `int`, and assigning the final result to `double acc` cannot recover the lost precision—the information was truncated at every addition step. With `fold_left`: the return type is deduced from `std::plus{}(1, 1.5)`, which is `double`. Thus, `1 + 1.5 + 2.5 + 3.5 + 4.5 = 13.0`, preserving precision. This single difference justifies the switch.

### Six Names, Twelve Overloads

The `fold` family is much more comprehensive than `accumulate`. `accumulate` only supports left folds, whereas `fold` handles both directions and distinguishes between "requiring an initial value" and "returning the final iterator alongside the result". These design choices are orthogonal, which would result in 8 names × 2 overloads = 16 functions. The proposal ultimately removed the "right fold + return iterator" group (see reasoning below), leaving **6 names and 12 overloads**:

| Name | Direction | Initial Value | Returns |
|---|---|---|---|
| `fold_left` | Left | Explicitly provided | Result |
| `fold_left_first` | Left | First element | `optional<Result>` |
| `fold_right` | Right | Explicitly provided | Result |
| `fold_right_last` | Right | Last element | `optional<Result>` |
| `fold_left_with_iter` | Left | Explicitly provided | `{End iterator, Result}` |
| `fold_left_first_with_iter` | Left | First element | `{End iterator, optional<Result>}` |

The naming convention is consistent: `left`/`right` indicates direction; without `first`/`last`, an initial value is explicitly required, while with them, the first/last element serves as the initial value; without `with_iter`, only the result is returned, while with it, the end iterator is returned as well. In daily use, we don't need to memorize the longer names; `fold_left` and `fold_right` alone cover 80% of use cases. The semantics of folding are visualized below (`f` is a binary operation):

```text
fold_left(r, init, f):           f(... f(f(init, r[0]), r[1]) ..., r[n-1])
fold_left_first(r, f):           f(... f(f(r[0], r[1]), r[2]) ..., r[n-1])
fold_right(r, init, f):          f(r[0], f(r[1], ... f(r[n-1], init) ...))
fold_right_last(r, f):           f(r[0], f(r[1], ... f(r[n-2], r[n-1]) ...))
```

### Design Details We Can't Avoid

Here are a few design details that might seem strange at first glance, but actually have good reasons behind them. Let's go through them one by one.

**Why do the `first`/`last` versions return `optional`?** Because they use the first or last element as the initial value. If an **empty range** is passed in, there is no first element available. Other algorithms (like `ranges::max`) have undefined behavior in this situation, but `fold` chooses to return an empty `optional`. This is also the first time the standard library has meaningfully used `optional` to express "this algorithm has no defined value for empty input." Let's test this:

```cpp
// Standard: C++23
std::vector<int> empty;
auto opt = std::ranges::fold_left_first(empty, std::plus{});
std::cout << "fold_left_first(空) has_value = " << opt.has_value() << '\n';
```

```text
fold_left_first(空) has_value = 0
```

**Why is there no `fold_right_with_iter`?** Because a right fold can be transformed into a left fold using `views::reverse`—there is no need to implement a dedicated right fold with an iterator. The specific equivalence is shown below (note that the two arguments of the binary operation must be swapped):

```cpp
fold_right(r, init, f)
  == fold_left(r | views::reverse, init,
               [](auto&& a, auto&& b){ return f(b, a); });
```

Let's verify this equivalence relationship with a practical test (using the non-commutative operation `f(a,b) = a*10+b`, which is order-sensitive and can reveal the difference between left and right):

```cpp
// Standard: C++23
#include <algorithm>
#include <iostream>
#include <ranges>
#include <vector>

int main() {
    std::vector<int> v{1, 2, 3, 4};
    auto f = [](auto a, auto b){ return a * 10 + b; };
    auto right  = std::ranges::fold_right(v, 0, f);
    auto as_left = std::ranges::fold_left(
        v | std::views::reverse, 0,
        [&](auto a, auto b){ return f(b, a); });
    std::cout << "fold_right:         " << right << '\n';
    std::cout << "fold_left(反转等价):  " << as_left << '\n';
}
```

```text
fold_right:         100
fold_left(反转等价):  100
```

The two are completely equivalent, so the equation holds. Therefore, the right fold + `with_iter` combination was removed—you can compose it yourself using `views::reverse`, so the standard library didn't need to reinvent the wheel.

**Why doesn't `fold` have a projection parameter?** Other ranges algorithms (like `sort`, `find`, and `contains`) accept a projection function, but `fold` does not. The reason is that `fold_left_first` needs to calculate the initial **value**, which requires applying the projection to an rvalue of the first element. However, projections in other algorithms only operate on references/lvalues. Converting an rvalue to an lvalue requires an extra copy, a performance cost that `fold` cannot accept. To keep things consistent, `fold_left`—which could have supported a projection—was also left without one. If you need a projection, wrap the range with `views::transform` before folding.

::: warning Header Change
The `fold` family resides in `<algorithm>`, not `<numeric>` (where `accumulate` lives). Including the wrong header will result in a "name not found" error.
:::

## Convenient Wrappers: `contains`, `find_last`, `starts_with`/`ends_with` (C++23)

While `fold` fixes an existing issue, this group fills long-standing gaps. The STL has lacked several "obvious" convenience functions for years, forcing developers to use awkward workarounds. C++23 finally addresses this.

### `contains` / `contains_subrange`: Eliminating `find() != end()`

Checking "if a value exists in a sequence" is one of the most frequent operations. For decades, the STL lacked `contains`, forcing everyone to write `find(v, x) != v.end()`. This translates a simple "is it in there?" into "is the found position at the end (i.e., not found)?", which is unnecessarily convoluted. C++20 added member `contains(key)` to associative containers like `set` and `map`, and C++23 finally completes the picture with the generic version `ranges::contains`. It also has a sibling for searching subranges, `ranges::contains_subrange`:

```cpp
// Standard: C++23
#include <algorithm>
#include <iostream>
#include <vector>

int main() {
    std::vector<int> v{1, 2, 3, 4, 5};
    std::vector<int> pat{2, 3};

    bool old_way = (std::ranges::find(v, 3) != v.end());   // 老反模式
    bool new_way = std::ranges::contains(v, 3);             // 一句话

    std::cout << "find!=end: " << old_way << "  contains: " << new_way << '\n';
    std::cout << "contains_subrange(v, {2,3}): "
              << std::ranges::contains_subrange(v, pat) << '\n';
    std::cout << "contains(v, 9): " << std::ranges::contains(v, 9) << '\n';
}
```

```text
find!=end: 1  contains: 1
contains_subrange(v, {2,3}): 1
contains(v, 9): 0
```

`contains` checks for a single element (internally it calls `ranges::find`), while `contains_subrange` checks for a subsequence (internally it calls `ranges::search`). These are not new algorithms, but convenient wrappers—but "convenience" is valuable in itself. Code reads much more naturally as `contains(v, 3)` compared to `find(v,3)!=v.end()`, and newcomers no longer need to puzzle out the inverted logic of `!= end()`.

### find_last: Reverse search returning a subrange

`std::find` only locates the **first** match. To find the **last** one, the old way is `ranges::find(v | views::reverse, x)`—which works, but requires wrapping in `reverse`, and then manually mapping the reversed position back to the original one, which is verbose. C++23 adds `ranges::find_last` (along with `_if` and `_if_not` variants), which gives us the result directly:

```cpp
// Standard: C++23
std::vector<int> w{1, 2, 3, 4, 3, 2, 1};
auto [it, end] = std::ranges::find_last(w, 3);
std::cout << "find_last(w, 3) 下标 = "
          << std::distance(w.begin(), it) << '\n';
```

```text
find_last(w, 3) 下标 = 4
```

Note that the return value is not just a single iterator, but a **`subrange`** (the found position + the end). Therefore, structured binding captures two values: `[it, end]`. This is a common pattern for new algorithms in the ranges era—returning the "found position" along with the "end of the range" to save you from calling `w.end()` again. When not found, `it == end`, so simply check that condition.

::: warning Don't expect a legacy std::find_last
`find_last` only has a `ranges::` version. The legacy `<algorithm>` header basically isn't getting new features anymore, so you'll need to use the ranges version to use this.
:::

### starts_with / ends_with

Checking if "this sequence starts/ends with that sequence" was also a long-missing operation. C++20 first added member functions `starts_with`/`ends_with` to `string`/`string_view`, and C++23 supplemented them with generic versions `ranges::starts_with` / `ranges::ends_with`, which work with any Range:

```cpp
// Standard: C++23
std::vector<int> v{1, 2, 3, 4, 5};
std::cout << "starts_with({1,2}): "
          << std::ranges::starts_with(v, (std::vector<int>{1, 2})) << '\n';
std::cout << "ends_with({4,5}): "
          << std::ranges::ends_with(v, (std::vector<int>{4, 5})) << '\n';
```

```text
starts_with({1,2}): 1
ends_with({4,5}): 1
```

Note that, just like with `contains_subrange`, **the longer sequence comes first, followed by the prefix or suffix to match**. Both functions also accept comparison predicates and projections (as the third and fourth arguments), making case-insensitive matching and similar tasks very convenient.

## C++23 New Ranges Adapters: Deep Dive + Cheat Sheet

C++23 added a batch of new members (nearly 15) to the ranges view library. The general mechanisms of views (laziness, piping, factory views) are covered in Volume 4, so we won't cover the background here. Instead, we will thoroughly explore the most commonly used adapters in engineering practice and provide a cheat sheet for the rest.

### `zip` / `zip_transform`: Iterating Multiple Sequences in Parallel

`zip` "zips" multiple ranges together, producing a range of `tuple`s—where each group contains elements from the same position in each range. We no longer need to manually manage shared indices when iterating two sequences in parallel:

```cpp
// Standard: C++23
std::vector<int>         vi{1, 2, 3};
std::vector<std::string> vs{"a", "b", "c"};
for (auto [a, b] : std::views::zip(vi, vs)) {
    std::cout << '(' << a << ',' << b << ")\n";
}
```

```text
(1,a)
(2,b)
(3,c)
```

`zip_transform` is equivalent to `zip` followed by `transform(apply)`, combining both steps into one:

```cpp
// Standard: C++23
for (auto s : std::views::zip_transform(std::plus{},
                                        vi, std::vector<int>{10, 20, 30})) {
    std::cout << s << '\n';   // 11 22 33
}
```

::: warning zip elements are reference tuples, not value tuples
The element reference type of `zip(vi, vs)` is `tuple<int&, string&>` (pointing to the original containers), whereas the value type is `tuple<int, string>`. This distinction is usually imperceptible (structured bindings work as usual), but we must be careful when dealing with move-only elements or sorting the original containers. `ranges::sort(views::zip(vi, vs))` leverages this reference semantics to "sort by one container and simultaneously reorder the other."
:::

### adjacent / pairwise: Grouping N adjacent elements

`adjacent<N>` packs N consecutive elements into a `tuple` (where N is a compile-time constant). The most common case is N=2, which has the alias `pairwise`. This is particularly handy for calculating adjacent differences or pairing neighbors:

```cpp
// Standard: C++23
std::vector<int> v{1, 2, 3, 4, 5};
// adjacent<3>: (1,2,3) (2,3,4) (3,4,5)
// pairwise 相邻差: 1 1 1 1
for (auto [a, b] : std::views::pairwise(v)) {
    std::cout << (b - a) << ' ';   // 1 1 1 1
}
```

`adjacent` looks similar to `slide`, described below. The difference is that the window size for `adjacent` is a **compile-time** constant (`adjacent<3>`), and the element type is a `tuple`. In contrast, the window size for `slide` is a **runtime** argument (`slide(3)`), and the element type is a `subrange`. If we can determine the window size at compile time, we should use `adjacent` for more specific types and better performance.

### chunk vs slide: Non-overlapping chunks vs. overlapping sliding windows

These two are the easiest to confuse. Both split a sequence into fixed-size windows, but the difference lies in whether the windows overlap:

- `chunk(n)`: **Non-overlapping** chunks, like pagination—`[1..n]`, `[n+1..2n]`, ..., where the last chunk might have fewer than `n` elements.
- `slide(n)`: **Overlapping** sliding windows, shifting right by one each time—`[1..n]`, `[2..n+1]`, `[3..n+2]`, ..., where every chunk contains exactly `n` elements (unless the sequence is shorter than `n`, in which case it is empty).

A practical comparison using the same sequence `1 2 3 4 5 6 7` with a window size of 3:

```cpp
// Standard: C++23
#include <algorithm>
#include <iostream>
#include <ranges>
#include <vector>

void dump(const auto& r, const char* lbl) {
    std::cout << lbl << ":\n";
    for (auto c : r) {
        std::cout << "  [";
        for (int x : c) std::cout << x << ' ';
        std::cout << "]\n";
    }
}

int main() {
    std::vector<int> seq{1, 2, 3, 4, 5, 6, 7};
    dump(std::views::chunk(seq, 3), "chunk(3)");
    dump(std::views::slide(seq, 3), "slide(3)");
}
```

```text
chunk(3):
  [1 2 3 ]
  [4 5 6 ]
  [7 ]
slide(3):
  [1 2 3 ]
  [2 3 4 ]
  [3 4 5 ]
  [4 5 6 ]
  [5 6 7 ]
```

`chunk(3)` yields three chunks (the last one contains only `7`), while `slide(3)` yields five chunks (each is full with three elements, sliding as a whole). Memory aid: **chunk is like cutting a cake (non-overlapping slices), slide is like a sliding window (overlapping frames)**. Use `chunk` when we need "batch processing" (pagination, binning), and use `slide` when we need "local context" (moving average, N-gram).

### stride: Take Every Nth Element

`stride(n)` selects every nth element, filling the long-standing gap in the STL for "strided subsets". In the old STL, to take every other element, we had to write a manual `for (i = 0; i < v.size(); i += 2)`; `stride(2)` replaces that with a single line:

```cpp
// Standard: C++23
std::vector<int> seq{1, 2, 3, 4, 5, 6, 7};
std::cout << "stride(2): ";
for (int x : std::views::stride(seq, 2)) std::cout << x << ' ';
std::cout << '\n';
```

```text
stride(2): 1 3 5 7
```

Even `views::iota(0) | stride(3)` yields an integer stream with a step size, like "0, 3, 6, 9, …". `iota` doesn't have a step parameter itself, so it relies on `stride` to fill that role. The step value must be a positive integer; zero or negative values are meaningless.

### repeat: Single-Element Repeater (The Unbounded Trap)

`repeat(x)` repeats a single element into an **infinite** range, while `repeat(x, n)` repeats it `n` times (bounded). It is a view factory (like `iota`), serving as the starting point of a pipeline, so we cannot pipe into it using `r |`.

```cpp
// Standard: C++23
for (int x : std::views::repeat(7, 3)) std::cout << x << ' ';   // 7 7 7
std::cout << '\n';
// 无界版必须 take 截断,否则死循环
for (int x : std::views::repeat(0) | std::views::take(4)) std::cout << x << ' ';
std::cout << '\n';   // 0 0 0 0
```

::: warning The Unbounded Trap of `repeat`
`repeat(x)` without a second argument is an infinite range. Using `for (auto a : views::repeat(1))` directly results in an **infinite loop**. You must either provide a second argument to limit the count, or truncate it using `| views::take(n)`. The same applies to `iota(N)`—infinite factory views must be paired with `take`.
:::

### Quick Reference for Other New Adapters

We won't dive into the remaining adapters; here is a table for quick reference. Some names underwent several revisions before being finalized (e.g., `as_rvalue` was originally `move`, `slide` was `sliding`, and `zip_transform` was `zip_with`), so just use the current names.

| Adapter | Purpose | One-Line Distinction |
|---|---|---|
| `join_with(delim)` | Flattens a range-of-ranges using a delimiter | Adds a delimiter compared to C++20's `join`; `{"ab","cd"} \| join_with('-')` → `ab-cd` |
| `chunk_by(pred)` | Starts a new chunk when a binary predicate returns false (GroupBy) | Chunks continuously based on a predicate, not by value; only splits "adjacent" satisfying segments |
| `as_rvalue` | Elements flow out as rvalues (range version of `std::move`) | Works with `ranges::to` to move elements into a new container |
| `as_const` | Elements are read-only (range version of `std::as_const`) | Protects elements from modification |

Let's test `join_with` by concatenating an array of strings into a single long string using a delimiter:

```cpp
// Standard: C++23
std::vector<std::string> words{"hello", "world", "cpp23"};
std::cout << "join_with('-'): ";
for (char ch : std::views::join_with(words, '-')) std::cout << ch;
std::cout << '\n';   // hello-world-cpp23
```

`chunk_by` uses a binary predicate, and a new chunk starts when the predicate returns false for two adjacent elements (consecutive identical values are grouped together):

```cpp
// Standard: C++23
std::vector<int> runs{1, 1, 2, 2, 2, 3, 1, 1};
for (auto c : std::views::chunk_by(runs, std::equal_to{})) {
    std::cout << '[';
    for (int x : c) std::cout << x;
    std::cout << "]\n";   // [11] [222] [3] [11]
}
```

## Compiler Support Status: GCC 16.1.1 Feature-by-Feature Test

As mentioned at the beginning of this article, many ranges tutorials online were written during the "standard stabilization" period of 2022. Back then, C++23 features were not yet implemented, and the pages were filled with "GCC not yet" and "Clang not yet". It is now 2026, and those status notes are **completely obsolete**. We used a local GCC 16.1.1 (`g++ (GCC) 16.1.1 20260430`) to test each feature individually, providing a current support table. Verification method: for each feature, we run a snippet of code that actually uses it; if it compiles and runs correctly, it counts as supported. We also record the value of the feature test macro.

| Feature | Header | Test Macro | GCC 16.1.1 | Notes |
|---|---|---|---|---|
| `ranges::fold` family | `<algorithm>` | `__cpp_lib_ranges_fold >= 202207L` | Supported | All 6 names are available |
| `ranges::contains` / `contains_subrange` | `<algorithm>` | `__cpp_lib_ranges_contains >= 202207L` | Supported | |
| `ranges::starts_with` / `ends_with` | `<algorithm>` | `__cpp_lib_ranges_starts_ends_with >= 202106L` | Supported | |
| `ranges::find_last` family | `<algorithm>` | `__cpp_lib_ranges_find_last >= 202207L` | Supported | Macro name is `ranges_find_last`, don't search incorrectly |
| `views::zip` / `zip_transform` | `<ranges>` | `__cpp_lib_ranges_zip >= 202110L` | Supported | |
| `views::adjacent` / `pairwise` | `<ranges>` | `__cpp_lib_ranges_zip >= 202110L` | Supported | Same proposal macro as zip |
| `views::chunk` | `<ranges>` | `__cpp_lib_ranges_chunk >= 202202L` | Supported | |
| `views::slide` | `<ranges>` | `__cpp_lib_ranges_slide >= 202202L` | Supported | |
| `views::stride` | `<ranges>` | `__cpp_lib_ranges_stride >= 202207L` | Supported | |
| `views::repeat` | `<ranges>` | `__cpp_lib_ranges_repeat >= 202207L` | Supported | |
| `views::join_with` | `<ranges>` | `__cpp_lib_ranges_join_with >= 202202L` | Supported | |
| `views::chunk_by` | `<ranges>` | `__cpp_lib_ranges_chunk_by >= 202202L` | Supported | |
| `views::as_rvalue` | `<ranges>` | `__cpp_lib_ranges_as_rvalue >= 202207L` | Supported | |
| `views::as_const` | `<ranges>` | `__cpp_lib_ranges_as_const >= 202311L` | Supported | |

The conclusion is straightforward: **GCC 16.1.1 supports all C++23 ranges algorithms and adapters discussed in this article**. Every row in the table above has corresponding code that has been compiled and run on this machine. If you are still using GCC 13/14, the new `<algorithm>` additions like `fold`/`contains`/`find_last` and `as_const` might be missing—upgrading to version 15 or later will fix this. Clang's libstdc++ support lags slightly behind (when Clang uses its own libc++, some adapters were implemented later than in GCC), so for cross-compiler projects, it is best to test the target toolchain before using them.

## Summary

Let's wrap up the key points of this article:

- **Three steps to Rangifying algorithms**: Parameters change from iterator pairs to Ranges (sentinels allow sequences like null-terminated strings that "stop when a condition is met" to be used); Concepts reject incorrect types at the call site (`ranges::sort(list)` errors are much more intuitive than the old `std::sort(list)`); Niebloids do not participate in ADL (algorithms cannot be hijacked by functions with the same name in user namespaces).
- **The `fold` family fixes the return type pitfall of `accumulate`**: The return type is determined by `f(init, *first)` and is no longer locked to the initial value type; 6 names with 12 overloads (left/right × with/without initial value × with/without _iter); `first`/`last` versions return `optional` (for empty ranges); no `fold_right_with_iter` (compose with `views::reverse`); no projection (projection of the first element as an rvalue is lossy).
- **Convenient wrappers to fill old gaps**: `contains`/`contains_subrange` eliminate `find()!=end()`; `find_last` returns a subrange (remember, only the `ranges::` version exists); `starts_with`/`ends_with` generalize string member functions.
- **C++23 new adapters**: `zip`/`zip_transform` (parallel traversal), `adjacent`/`pairwise` (compile-time window, tuple), `chunk` (non-overlapping chunks) vs `slide` (overlapping sliding window), `stride` (take every Nth), `repeat` (watch out for the unbounded trap, need `take` to truncate); plus `join_with`/`chunk_by`/`as_rvalue`/`as_const`.
- **GCC 16.1.1 support status**: All C++23 ranges algorithms (fold/contains/find_last/starts_ends_with) and adapters (zip/chunk/slide/stride/repeat/join_with/chunk_by/as_rvalue/as_const) discussed in this article are **fully supported**. Don't believe the "GCC not yet" claims in 2022-era resources.

In the next article, we will continue with the general mechanisms of ranges views—pipes `|`, lazy evaluation, and factory views—that part belongs to vol4. We will clarify exactly where views are "lazy" and how they mesh with algorithms to produce LINQ-style chained syntax.

## References

- [cppreference: Constrained algorithms (C++20)](https://en.cppreference.com/w/cpp/algorithm/ranges) — Overview of ranges algorithms and Niebloid explanation
- [cppreference: std::ranges::fold_left (C++23)](https://en.cppreference.com/w/cpp/algorithm/ranges/fold_left) — Signatures and return type rules for the six fold family names
- [cppreference: std::ranges::contains (C++23)](https://en.cppreference.com/w/cpp/algorithm/ranges/contains) — contains and contains_subrange
- [cppreference: std::ranges::find_last (C++23)](https://en.cppreference.com/w/cpp/algorithm/ranges/find_last) — Reverse search returning a subrange
- [cppreference: std::ranges::zip_view (C++23)](https://en.cppreference.com/w/cpp/ranges/zip_view) — zip family (zip / adjacent / pairwise and _transform versions)
- [cppreference: std::ranges::chunk_view / slide_view (C++23)](https://en.cppreference.com/w/cpp/ranges/chunk_view) — Semantic differences between chunking and sliding windows
- [cppreference: std::ranges::stride_view / repeat_view (C++23)](https://en.cppreference.com/w/cpp/ranges/stride_view) — Stride subsets and single-element repeat generators
- [P2322R6 fold](https://wg21.link/p2322r6), [P2302R4 contains](https://wg21.link/p2302r4), [P2214R1 C++23 Ranges Plan](https://wg21.link/p2214r1) — Original proposals and design motivations for each feature
