---
chapter: 7
cpp_standard:
- 20
- 23
description: 'A thorough look at the three steps that ranges-ified the algorithms (Range parameters, Concept constraints, Niebloids that opt out of ADL), how the C++23 fold family fixes the return-type trap of accumulate, how contains and find_last wipe out the find != end() antipattern, and hands-on testing of GCC 16.1.1 support for the new adapters such as zip, chunk, slide, stride, and repeat'
difficulty: advanced
order: 45
platform: host
prerequisites:
- 'Iterator Basics and Categories: The Glue Between Containers and Algorithms'
- 'Iterator Adapters: Reverse, Insertion, and Stream — Teaching Old Iterators New Tricks'
related:
- 'New Standard Containers: flat_map, inplace_vector, and mdspan'
reading_time_minutes: 22
tags:
- host
- cpp-modern
- advanced
- Ranges
title: 'Ranges Algorithms and the C++23 Newcomers: fold, contains, and New Adapters'
translation:
  source: documents/vol3-standard-library/iterators-algorithms/45-ranges-algorithms-and-adaptors-cpp23.md
  source_hash: 4329b9fe6e95dfddc52c097dd0c5ba66a004182d4b981273784ebe948215fdab
  translated_at: '2026-09-26T01:50:26+00:00'
  engine: anthropic
  token_count: 11000
---
# Ranges Algorithms and the C++23 Newcomers: fold, contains, and New Adapters

In the previous few articles we took iterators and iterator adapters apart — but on the algorithm side we were still writing the "old `<algorithm>`" way. This article is devoted to the modern evolution of that whole track: how C++20 ranges-ified the entire `<algorithm>` (parameters, Concepts, Niebloids — three steps), plus the key C++23 additions — how the `fold` family fixes `accumulate`'s old trap, how `contains`/`find_last` wipe out the `find() != end()` antipattern, and a batch of new ranges adapters (`zip`/`chunk`/`slide`/`stride`/`repeat`).

First, a boundary line, so we don't collide with another volume: views, the pipe `|`, lazy evaluation — those **general concepts** of ranges belong to vol4 (which covers ranges view pipelines in depth). This article does not unfold the general machinery; it sticks to two concrete topics — "the ranges-ification of the algorithms" and "the new C++23 algorithms/adapters". Materializing a view into a container with `ranges::to` belongs to the container track, covered by vol3's [New Standard Containers](../containers/10-new-containers-cpp23-26.md) and cppreference; here we only mention it in passing where it comes up.

## The Ranges-ification of the Algorithms: What Each of the Three Steps Changed

C++20 did not simply slap a `ranges::` prefix on the old algorithms. It changed three things at once, and each one maps to a difference you will actually run into.

### Step 1: Parameters Go from an Iterator Pair to a Range

The old style makes you hand over two iterators manually: `std::sort(v.begin(), v.end())`. The ranges version takes a Range directly: `std::ranges::sort(v)`. Typing half as much is the minor part; the real payoff hides in the **sentinel**.

The old STL requires the two iterators to be **the same type** — `begin()` and `end()` must return the same kind of iterator. That looks self-evident, but it actually blocks a very natural family of sequences: **C strings terminated by `\0`**. Their "end" is not a pointer position of the same type as the first iterator; it is the condition "stop when you hit `\0`" — before ranges, you either computed `strlen` yourself or wrapped things in a `std::string_view` first.

ranges abstracts "the end" into a **sentinel**: a sentinel can have a different type from the iterator, as long as it can be compared against the iterator for equality. As a result, sequences whose "length isn't known up front, we stop when a condition is met" can be fed straight to algorithms. `string_view` is the canonical example — its `end()` returns a sentinel, and `ranges::count` eats it directly:

```cpp
// Standard: C++20
#include <algorithm>
#include <iostream>
#include <string_view>
#include <vector>

int main() {
    // Range parameter: one argument covers the whole container
    std::vector<int> v{3, 1, 4, 1, 5, 9, 2, 6};
    std::ranges::sort(v);
    std::cout << "ranges::sort 后: ";
    for (auto x : v) std::cout << x << ' ';
    std::cout << '\n';

    // string_view's end() is a sentinel — a natural fit for "stop at \0"
    std::string_view sv = "hello";
    std::cout << "ranges::count(\"hello\", 'l') = "
              << std::ranges::count(sv, 'l') << '\n';
}
```

Run with `g++ -std=c++23 -O2` (native GCC 16.1.1):

```text
ranges::sort 后: 1 1 2 3 4 5 6 9
ranges::count("hello", 'l') = 2
```

### Step 2: Concepts Reject the Wrong Types at the Call Site

We planted this thread back when we covered iterator categories: `std::sort` wants random-access iterators, `std::list`'s iterators only reach bidirectional, so `std::sort` cannot be used on a `list`. Before C++20, that requirement existed only in the documentation — pass the wrong type, and the compiler would not tell you "wrong type" at the call site; it would silently instantiate the templates and finally spit out a long string of errors like "no `operator-` found", leaving the reader to trace backwards to figure out which step went wrong.

ranges algorithms move the requirement into the type system with Concepts. Call `ranges::sort(l)` on a `list` and the Concept fails **at the call site**, with an error that goes straight to the point. Put the two spellings side by side and the contrast is stark:

```cpp
// Standard: C++20
#include <algorithm>
#include <list>

int main() {
    std::list<int> l{3, 1, 4, 1, 5};
    std::ranges::sort(l);   // the ranges version: rejected right at the Concept layer
    std::sort(l.begin(), l.end());   // the old version: an operator- error from deep inside the templates
}
```

The error GCC 16.1.1 gives for `ranges::sort(l)` (first few lines):

```text
concept_reject.cpp:7:22: error: no match for call to
    '(const std::ranges::__sort_fn) (std::__cxx11::list<int>&)'
    7 |     std::ranges::sort(l);   // Concept 层直接拒绝
      |     ~~~~~~~~~~~~~~~~~^~~
  • candidate 1: ... requires (random_access_iterator<_Iter>) ...
                            ^^^^^^^^^^^^^^^^^^^^^^^^^^^
```

The old spelling `std::sort(l.begin(), l.end())` instead buries its error deep inside the templates:

```text
/usr/include/c++/16.1.1/bits/stl_algo.h: In instantiation of
'constexpr void std::__sort(_RandomAccessIterator, _RandomAccessIterator, _Compare)
   [with _RandomAccessIterator = _List_iterator<int>; ...]':
  required from here
stl_algo.h:1914:50: error: no match for 'operator-'
 (operand types are 'std::_List_iterator<int>' and 'std::_List_iterator<int>')
 1914 |                                 std::__lg(__last - __first) * 2,
```

One says at the call line, "I need random_access, and that is not what you gave me"; the other detours into `__sort`'s innards to say "`__last - __first` doesn't compute". The first you can locate at a glance; with the second you have to reason backwards — "a `list`'s iterators can't be subtracted" — before it clicks. That is the practical value of Concepts turning "requirements written in the docs" into "facts decidable at compile time". When a `list` wants sorting, it goes through its own member function `list::sort()` (covered in the previous article: a merge implementation, O(n log n)).

### Step 3: Niebloids — Algorithms That Opt Out of ADL

This step is sneakier, but baffling when you run into it. Old STL algorithms are **plain functions** living in a namespace; ranges algorithms are not functions at all — each is a function object called a **Niebloid** (a customization point object, CPO). It looks like something you can call like a function, but there are two key differences.

The one with the biggest practical impact: **Niebloids do not participate in ADL (argument-dependent lookup)**. That means: if you write a `sort(x)` inside your own custom namespace, the compiler will never "helpfully" drag `std::ranges::sort` into the overload set because of some argument's type — with the old STL algorithms this hijacking risk is real (if some type's associated namespace happens to contain a `sort`, it can hide `std::sort`). We can verify this live:

```cpp
// Standard: C++20
#include <algorithm>
#include <vector>

namespace user {
    struct Tag {};
    void sort(Tag) {}   // a same-named sort lives in the custom namespace

    void demo() {
        std::vector<int> v{3, 1, 2};
        sort(v);   // neither using std::ranges, nor pulled in via ADL
    }
}
```

The error shows that `ranges::sort` was not found via ADL (it isn't among the candidates at all):

```text
adl.cpp:14:13: error: no matching function for call to 'sort(std::vector<int>&)'
   14 |         sort(v);
      |     ~~~~^~~
```

The design motivation for Niebloids is precisely to plug that hole: algorithms cannot be hijacked by same-named functions in user namespaces, so behavior stays predictable. As a side note, since a Niebloid is an object rather than a function, don't expect to pass its address around as a callback the way you might with old `std::sort` — it is a closure object with overloaded `operator()`, whose semantics are not the same thing as a plain function pointer; when you need one, wrapping it in a lambda is the safer move.

::: warning "Ranges algorithms" are not "old algorithms with a ranges:: prefix"
The three steps work as one: Range + sentinel parameters, Concept constraints, Niebloids. That means ranges algorithms are not syntactic sugar over the old ones — they are a redesigned interface. The old `std::sort` is not deprecated (your existing code keeps running), but in new code, use the ranges version whenever you can: less typing, clearer errors, no ADL hijacking — a triple win.
:::

## The fold Family (C++23): Fixing accumulate's Return-Type Trap

With the ranges-ification covered, we move into the C++23 newcomers. The first one to dissect thoroughly is `fold` — it did not appear out of thin air; it exists to fix a genuine defect in old `std::accumulate`.

### The Return-Type Trap of accumulate

`std::accumulate` lives in `<numeric>` and does a "left fold": given an initial value and a binary operation, it folds the whole sequence into one value, left to right. It has a fairly sneaky trap — **the return type is decided by the initial value, not by the element type**. Write the initial value as `1` (an int) and even a sequence full of `double`s is computed in `int` the whole way through, the fractional parts silently truncated:

```cpp
// Standard: C++23
#include <algorithm>
#include <iostream>
#include <numeric>
#include <vector>

int main() {
    std::vector<double> vec{1.5, 2.5, 3.5, 4.5};   // true sum = 12.0

    // initial value written as 1 (int): the return type is pinned to int, 1.5/2.5... all truncated
    double acc = std::accumulate(vec.begin(), vec.end(), 1);
    std::cout << "accumulate(vec, 1)      = " << acc << '\n';

    // fold_left's return type comes from f(init, *first) — deduced back to double here, no truncation
    double fl = std::ranges::fold_left(vec, 1, std::plus{});
    std::cout << "fold_left(vec, 1, +)    = " << fl << '\n';
}
```

Run it, and the contrast is glaring:

```text
accumulate(vec, 1)      = 11
fold_left(vec, 1, +)    = 13
```

The `accumulate` line: the initial value `1` is an int, so `1 + 1.5` → `2` (truncated), `2 + 2.5` → `4`, `4 + 3.5` → `7`, `7 + 4.5` → `11` — int all the way; assigning the result to `double acc` at the end cannot rescue it, because the information was already truncated away at every addition. The `fold_left` line: the return type is that of `std::plus{}(1, 1.5)`, i.e. `double`, so `1 + 1.5 + 2.5 + 3.5 + 4.5 = 13.0`, no precision lost. That single difference alone justifies switching.

### Six Names, Twelve Overloads

The `fold` family is far more complete than `accumulate`. `accumulate` can only fold left; `fold` does both directions, and additionally distinguishes "with or without an initial value" and "with or without also returning the end iterator". Design-wise these are orthogonal choices — listed out in full that is 8 names × 2 overloads = 16; the proposal ended up dropping the "right fold + return the iterator too" group (reasoning below), leaving **6 names and 12 overloads**:

| Name | Direction | Initial value | Returns |
|---|---|---|---|
| `fold_left` | left | given explicitly | the result |
| `fold_left_first` | left | first element | `optional<result>` |
| `fold_right` | right | given explicitly | the result |
| `fold_right_last` | right | last element | `optional<result>` |
| `fold_left_with_iter` | left | given explicitly | `{end iterator, result}` |
| `fold_left_first_with_iter` | left | first element | `{end iterator, optional<result>}` |

The naming rules are quite regular: `left`/`right` is the direction; without `first`/`last` you supply the initial value explicitly, with them the first/last element serves as it; without `with_iter` you only get the result back, with it you also get the end iterator. For everyday use you won't remember names that long — knowing `fold_left` / `fold_right` covers eighty percent of scenarios. Drawn out, the fold semantics look like this (`f` is the binary operation):

```text
fold_left(r, init, f):           f(... f(f(init, r[0]), r[1]) ..., r[n-1])
fold_left_first(r, f):           f(... f(f(r[0], r[1]), r[2]) ..., r[n-1])
fold_right(r, init, f):          f(r[0], f(r[1], ... f(r[n-1], init) ...))
fold_right_last(r, f):           f(r[0], f(r[1], ... f(r[n-2], r[n-1]) ...))
```

### A Few Design Details You Cannot Dodge

Here are a few points that look strange but each has its reasons; let's take them one by one.

**Why do the `first`/`last` versions return `optional`?** Because they use the first/last element as the initial value — if what gets passed in is an **empty range**, there is no first element to use. Other algorithms (say `ranges::max`) simply make that case undefined behavior; `fold` chooses to return an empty `optional` — also the standard library's first meaningful use of `optional` to express "this algorithm has no defined value for empty input". Let's try it:

```cpp
// Standard: C++23
std::vector<int> empty;
auto opt = std::ranges::fold_left_first(empty, std::plus{});
std::cout << "fold_left_first(空) has_value = " << opt.has_value() << '\n';
```

```text
fold_left_first(空) has_value = 0
```

**Why is there no `fold_right_with_iter`?** Because a right fold can be turned into a left fold with `views::reverse` — there is no need for a dedicated iterator-returning right fold. The concrete equivalence (note the binary operation's two arguments must be swapped):

```cpp
fold_right(r, init, f)
  == fold_left(r | views::reverse, init,
               [](auto&& a, auto&& b){ return f(b, a); });
```

Let's verify this equivalence live (using the non-commutative operation `f(a,b) = a*10+b`, which is order-sensitive and can expose left/right differences):

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

The two come out exactly equal, so the equivalence holds. That is why the right-fold-plus-with_iter combination was deleted — you can piece it together yourself with `views::reverse`, and the standard library doesn't build the same thing twice.

**Why doesn't `fold` take a projection parameter?** Other ranges algorithms (`sort`, `find`, `contains`) can all hang a projection function on; `fold` alone cannot. The reason: `fold_left_first` has to compute the initial **value**, which would require applying the projection to an rvalue of the first element; other algorithms' projections are only required to apply to references/lvalues, and turning an rvalue into an lvalue costs an extra copy — an efficiency loss `fold` cannot accept. For uniformity, `fold_left`, which could have taken a projection, was denied one as well. If you need a projection, wrap a `views::transform` around it first, then fold.

::: warning The header changed
The `fold` family is not in `<numeric>` (even though `accumulate` is) — it is in `<algorithm>`. Include the wrong one and you get "name not found".
:::

## Convenience Wrappers: contains, find_last, starts_with/ends_with (C++23)

`fold` fixes an old pit; this group fills old gaps. The STL has long been missing a few convenience functions that "obviously should exist", forcing everyone to cobble together awkward spellings — C++23 finally filled them in.

### contains / contains_subrange: Eliminating `find() != end()`

Asking "is this value in the sequence" is one of the most frequent operations there is. For decades the STL shipped no `contains`, so everyone wrote `find(v, x) != v.end()` — translating a simple "is it there" into "is the position found equal to the end (i.e., not found)", one detour too many. C++20 first gave associative containers like `set`/`map` a member `contains(key)`; C++23 finally completed the picture with the general-purpose `ranges::contains`. It also has a sibling that searches for subsequences, `ranges::contains_subrange`:

```cpp
// Standard: C++23
#include <algorithm>
#include <iostream>
#include <vector>

int main() {
    std::vector<int> v{1, 2, 3, 4, 5};
    std::vector<int> pat{2, 3};

    bool old_way = (std::ranges::find(v, 3) != v.end());   // the old antipattern
    bool new_way = std::ranges::contains(v, 3);             // one clean line

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

`contains` looks up a single element (internally it just calls `ranges::find`); `contains_subrange` looks up a subsequence (internally `ranges::search`). These two are not new algorithms, they are convenience wrappers — but "convenience" is itself the value: in code, `contains(v, 3)` reads far more directly than `find(v,3)!=v.end()`, and newcomers no longer have to puzzle through the inverted logic of that `!= end()`.

### find_last: A Backward Search That Returns a subrange

`std::find` only finds the **first** match. To find the **last** one, the old approach was `ranges::find(v | views::reverse, x)` — workable, but you have to wrap a reverse around it and then convert the reversed position back into the original position yourself. Wordy. C++23 added `ranges::find_last` (plus the `_if` / `_if_not` variants, three in total) that hands it to you directly:

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

Note the return value is not a bare iterator but a **`subrange`** (the found position + the end), which is why the structured binding pulls out `[it, end]`. This is a common pattern in the ranges-era algorithms — they hand back "the found position" together with "the range's end", saving you another trip to `w.end()`. When nothing is found, `it == end`; a single check and you're done.

::: warning Don't count on an old std::find_last
`find_last` exists only as a `ranges::` version. The old `<algorithm>` basically isn't getting new additions anymore; to use it, you use the ranges version.
:::

### starts_with / ends_with

"Does this sequence start/end with that sequence" was another long-missing operation. C++20 first gave `string`/`string_view` the members `starts_with`/`ends_with`; C++23 then added the general versions `ranges::starts_with` / `ranges::ends_with`, usable with any Range:

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

Note that, as with `contains_subrange`, **the longer sequence comes first, the prefix/suffix to match comes second**. These two also accept a comparison predicate and projections (as the third and fourth arguments), which makes things like case-insensitive matching convenient.

## The New C++23 Ranges Adapters: Deep Dives on a Few, Plus a Cheat Sheet

C++23 added a batch of new members to the ranges view library (nearly 15 of them). The general machinery of views (laziness, pipes, factory views) is vol4's business, so we won't lay out background here — we just pick the few most commonly used in engineering and dissect those, then give a cheat sheet for the rest.

### zip / zip_transform: Traversing Multiple Sequences in Parallel

`zip` zips multiple ranges together like a zipper, producing a range of `tuple`s — each group holds the elements at the same position in each range. Traversing two sequences in parallel no longer requires hand-writing a shared index:

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

`zip_transform` is `zip` followed by a function (equivalent to `zip` + `transform(apply)`), in one step:

```cpp
// Standard: C++23
for (auto s : std::views::zip_transform(std::plus{},
                                        vi, std::vector<int>{10, 20, 30})) {
    std::cout << s << '\n';   // 11 22 33
}
```

::: warning zip's elements are reference tuples, not value tuples
The element reference type of `zip(vi, vs)` is `tuple<int&, string&>` (pointing into the original containers); only the value type is `tuple<int, string>`. This difference is imperceptible the vast majority of the time (structured bindings work as usual), but watch out when move-only elements are involved or when you sort the original containers. `ranges::sort(views::zip(vi, vs))` exploits this reference semantics to "sort by one container while the other gets reordered in lockstep".
:::

### adjacent / pairwise: Grouping N Adjacent Elements

`adjacent<N>` packs N consecutive elements into a `tuple` (N is a compile-time constant). The most common case is N=2, so there is an alias `pairwise` — particularly handy for adjacent differences and adjacent pairing:

```cpp
// Standard: C++23
std::vector<int> v{1, 2, 3, 4, 5};
// adjacent<3>: (1,2,3) (2,3,4) (3,4,5)
// pairwise adjacent differences: 1 1 1 1
for (auto [a, b] : std::views::pairwise(v)) {
    std::cout << (b - a) << ' ';   // 1 1 1 1
}
```

`adjacent` looks a lot like the `slide` we'll cover next; the difference: `adjacent`'s window size is fixed at **compile time** (`adjacent<3>`) and its element type is a `tuple`; `slide`'s window size is a **runtime** argument (`slide(3)`) and its element type is a `subrange`. If the window size can be pinned at compile time, use `adjacent` — more specific types, better performance.

### chunk vs slide: Non-Overlapping Blocks vs Overlapping Windows

These two are the easiest to confuse. Both slice a sequence into fixed-size windows; the difference is whether the windows overlap:

- `chunk(n)`: **non-overlapping** blocks, like pagination — `[1..n]`, `[n+1..2n]`, …, with the last block possibly shorter than n.
- `slide(n)`: **overlapping** sliding windows, shifting right one element at a time — `[1..n]`, `[2..n+1]`, `[3..n+2]`, …, each window a full n elements (empty when the sequence is shorter than n).

Let's compare them live, same sequence `1 2 3 4 5 6 7`, window 3:

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

`chunk(3)` yields 3 blocks (the last containing just `7`); `slide(3)` yields 5 windows (each a full 3, sliding right overall). Memory hook: **chunk is like slicing a cake (cut after cut, no overlap); slide is like a sliding window (frame after frame, overlapping)**. If the need is "batch processing" (pagination, bucketing), use chunk; if it is "looking at local context" (moving averages, N-grams), use slide.

### stride: Take One Every N

`stride(n)` takes one element every n, finally filling the STL's long-missing "subset with a step". In the old STL, taking every other element meant handwriting `for (i = 0; i < v.size(); i += 2)`; `stride(2)` replaces it in one line:

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

Even `views::iota(0) | stride(3)` gets you a stepped integer stream like "0, 3, 6, 9, …" — `iota` has no step parameter of its own; it leans on `stride` for that. The stride must be a positive integer; 0 and negatives are meaningless.

### repeat: A Single-Element Repeat Generator (and Its Unbounded Trap)

`repeat(x)` repeats a single element into an **infinite** range; `repeat(x, n)` repeats it n times (bounded). It is a view factory (like `iota`): it is itself a pipe entry point, and you cannot put `r |` in front of it.

```cpp
// Standard: C++23
for (int x : std::views::repeat(7, 3)) std::cout << x << ' ';   // 7 7 7
std::cout << '\n';
// the unbounded version must be truncated with take, or the loop never ends
for (int x : std::views::repeat(0) | std::views::take(4)) std::cout << x << ' ';
std::cout << '\n';   // 0 0 0 0
```

::: warning repeat's unbounded trap
`repeat(x)` without a second argument is an infinite range; `for (auto a : views::repeat(1))` on its own is an **infinite loop**. Either give the second argument to bound the count, or truncate with `| views::take(n)`. Same story for `iota(N)` — infinite factory views all need to be paired with `take`.
:::

### The Remaining New Adapters at a Glance

The remaining few we won't expand on; here is a table for reference. Some of the names went through several rounds of revision before settling (`as_rvalue` was originally called `move`, `slide` was `sliding`, `zip_transform` was `zip_with`) — just use the current names.

| Adapter | What it does | The one-line distinction |
|---|---|---|
| `join_with(delim)` | flattens a range-of-range with a separator inserted | like C++20's `join` plus a delimiter; `{"ab","cd"} \| join_with('-')` → `a-b-c...` — actually `ab-cd` |
| `chunk_by(pred)` | starts a new block when the binary predicate returns false (GroupBy) | chunks consecutively by predicate, not by value; only cuts segments of adjacent elements that satisfy it |
| `as_rvalue` | elements flow out as rvalues (the range version of `std::move`) | pairs with `ranges::to` to move elements into a new container |
| `as_const` | elements read-only (the range version of `std::as_const`) | protects elements from modification |

Let's try `join_with` for real, joining an array of strings into one long string with a separator:

```cpp
// Standard: C++23
std::vector<std::string> words{"hello", "world", "cpp23"};
std::cout << "join_with('-'): ";
for (char ch : std::views::join_with(words, '-')) std::cout << ch;
std::cout << '\n';   // hello-world-cpp23
```

`chunk_by` takes a binary predicate: when it returns false for two adjacent elements, a new block starts there (runs of equal values end up grouped together):

```cpp
// Standard: C++23
std::vector<int> runs{1, 1, 2, 2, 2, 3, 1, 1};
for (auto c : std::views::chunk_by(runs, std::equal_to{})) {
    std::cout << '[';
    for (int x : c) std::cout << x;
    std::cout << "]\n";   // [11] [222] [3] [11]
}
```

## Compiler Support Today: Feature-by-Feature Testing on GCC 16.1.1

As noted at the start of this article — many ranges tutorials online were written during 2022's "standard-finalization period", when none of the C++23 features were implemented yet, so those pages are full of "GCC not yet" and "Clang not yet". It is 2026 now, and those status labels are **all outdated**. We tested feature by feature on the native GCC 16.1.1 (`g++ (GCC) 16.1.1 20260430`) to produce a current support table. Verification method: for each feature, run a piece of code that actually uses it; if it compiles and runs correctly, it counts as supported. We also recorded the feature-test macro values.

| Feature | Header | Test macro | GCC 16.1.1 | Notes |
|---|---|---|---|---|
| `ranges::fold` family | `<algorithm>` | `__cpp_lib_ranges_fold >= 202207L` | Supported | all 6 names available |
| `ranges::contains` / `contains_subrange` | `<algorithm>` | `__cpp_lib_ranges_contains >= 202207L` | Supported | |
| `ranges::starts_with` / `ends_with` | `<algorithm>` | `__cpp_lib_ranges_starts_ends_with >= 202106L` | Supported | |
| `ranges::find_last` family | `<algorithm>` | `__cpp_lib_ranges_find_last >= 202207L` | Supported | the macro name is `ranges_find_last` — don't look up the wrong one |
| `views::zip` / `zip_transform` | `<ranges>` | `__cpp_lib_ranges_zip >= 202110L` | Supported | |
| `views::adjacent` / `pairwise` | `<ranges>` | `__cpp_lib_ranges_zip >= 202110L` | Supported | shares the same proposal macro as zip |
| `views::chunk` | `<ranges>` | `__cpp_lib_ranges_chunk >= 202202L` | Supported | |
| `views::slide` | `<ranges>` | `__cpp_lib_ranges_slide >= 202202L` | Supported | |
| `views::stride` | `<ranges>` | `__cpp_lib_ranges_stride >= 202207L` | Supported | |
| `views::repeat` | `<ranges>` | `__cpp_lib_ranges_repeat >= 202207L` | Supported | |
| `views::join_with` | `<ranges>` | `__cpp_lib_ranges_join_with >= 202202L` | Supported | |
| `views::chunk_by` | `<ranges>` | `__cpp_lib_ranges_chunk_by >= 202202L` | Supported | |
| `views::as_rvalue` | `<ranges>` | `__cpp_lib_ranges_as_rvalue >= 202207L` | Supported | |
| `views::as_const` | `<ranges>` | `__cpp_lib_ranges_as_const >= 202311L` | Supported | |

The conclusion is crisp: **every C++23 ranges algorithm and adapter covered in this article is supported by GCC 16.1.1**. Every row above has corresponding code that compiled and ran on this machine. If your GCC is still on 13/14, the `<algorithm>` newcomers `fold`/`contains`/`find_last` and `as_const` may still be missing — upgrade to 15 or newer and the set is complete. Clang's libstdc++ support lags slightly (when Clang uses its own libc++, some adapters were implemented later than GCC's); in cross-compiler projects, it is best to test the target toolchain before relying on them.

## Summary

Let's gather up the key points of this article:

- **The three steps of ranges-ification**: parameters go from iterator pairs to Ranges (sentinels let sequences that "run until a condition", like `\0`-terminated strings, work); Concepts reject wrong types at the call site (the `ranges::sort(list)` error is far more direct than old `std::sort(list)`'s); Niebloids don't participate in ADL (algorithms cannot be hijacked by same-named functions in user namespaces).
- **The `fold` family fixes `accumulate`'s return-type trap**: the return type is decided by `f(init, *first)`, no longer locked in by the initial value's type; 6 names, 12 overloads (left/right × with/without initial value × with/without with_iter); the `first`/`last` versions return `optional` (empty range); no `fold_right_with_iter` (assemble it with `views::reverse`); no projection (projecting the first element as an rvalue would be lossy).
- **Convenience wrappers fill old gaps**: `contains`/`contains_subrange` eliminate `find()!=end()`; `find_last` returns a subrange (don't forget it exists only as a `ranges::` version); `starts_with`/`ends_with` generalize the string member functions.
- **New C++23 adapters**: `zip`/`zip_transform` (parallel traversal), `adjacent`/`pairwise` (compile-time windows, tuples), `chunk` (non-overlapping blocks) vs `slide` (overlapping windows), `stride` (take one every N), `repeat` (mind the unbounded trap — truncate with `take`); plus `join_with`/`chunk_by`/`as_rvalue`/`as_const`.
- **GCC 16.1.1 support status**: the C++23 ranges algorithms covered here (fold/contains/find_last/starts_ends_with) and adapters (zip/chunk/slide/stride/repeat/join_with/chunk_by/as_rvalue/as_const) are **all supported**. Don't trust the "GCC not yet" labels in 2022-era material.

In the next article we go on to cover the general machinery of ranges views — the pipe `|`, lazy evaluation, factory views — that part belongs to vol4, where we make clear exactly where a view is "lazy", and how views mesh with algorithms into LINQ-style chained code.

## References

- [cppreference: Constrained algorithms (C++20)](https://en.cppreference.com/w/cpp/algorithm/ranges) — an overview of the ranges algorithms and the Niebloid explanation
- [cppreference: std::ranges::fold_left (C++23)](https://en.cppreference.com/w/cpp/algorithm/ranges/fold_left) — signatures of the six names in the fold family and the return-type rules
- [cppreference: std::ranges::contains (C++23)](https://en.cppreference.com/w/cpp/algorithm/ranges/contains) — contains and contains_subrange
- [cppreference: std::ranges::find_last (C++23)](https://en.cppreference.com/w/cpp/algorithm/ranges/find_last) — the backward search that returns a subrange
- [cppreference: std::ranges::zip_view (C++23)](https://en.cppreference.com/w/cpp/ranges/zip_view) — the zip family (zip / adjacent / pairwise and the _transform versions)
- [cppreference: std::ranges::chunk_view / slide_view (C++23)](https://en.cppreference.com/w/cpp/ranges/chunk_view) — the semantic difference between blocking and sliding windows
- [cppreference: std::ranges::stride_view / repeat_view (C++23)](https://en.cppreference.com/w/cpp/ranges/stride_view) — stepped subsets and the single-element repeat generator
- [P2322R6 fold](https://wg21.link/p2322r6), [P2302R4 contains](https://wg21.link/p2302r4), [P2214R1 A Plan for C++23 Ranges](https://wg21.link/p2214r1) — the original proposals and design motivations for each feature
